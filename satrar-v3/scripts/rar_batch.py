"""Run Table I/II style SatRAR experiments with resumable per-shard results.

Every (circuit, mode) row keeps three independent facts: how far the *search* got
(`search_status`, `coverage`, `searched_wires`, `complete_wires`), whether the candidates were
*verified* (`verify_status`), and the process/input state. `status` is only a compact summary of
those fields kept for older readers; it never hides a second reason (see `reasons`).
"""

import argparse
import copy
import csv
import hashlib
import json
import os
import platform
import subprocess
import sys
import threading
import time
from pathlib import Path
from tempfile import NamedTemporaryFile
from urllib.error import URLError
from urllib.request import urlopen

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "satrar-cpp" / "scripts"))
from bench_to_aag import CONVERTER_VERSION, convert

ROOT = Path(__file__).resolve().parent.parent
SCHEMA_VERSION = 6
PIN_ORDER = "topological-fanin-v1"  # order of AND receiving pins used by `rar_batch <offset>`
TOPOLOGY_PROFILE = "aig"
MODES = "l0 l1 l2 wire gate gate_nolearn gate_nofilter sub".split()

# Field order of the CLI's RAR_SUMMARY line (src/app/CommandShell.cpp).
BASE_SUMMARY = ("#wire", "searched_wires", "complete_wires", "#tar", "#alt", "#T", "verified_targets",
                "verified_alts", "rejected", "unverified", "limited", "redundant_targets", "trials",
                "verify_seconds")
# Optional trailing profile seconds (older binaries print only BASE_SUMMARY; they are padded with 0).
PROFILE = ("prof_setup_seconds", "prof_projection_seconds", "prof_trials_seconds", "prof_canonical_seconds")
CEC_V5 = ("cec_abc_jobs", "cec_abc_batches", "cec_timeouts", "cec_cex_mismatches", "cec_wall_seconds")
CEC_V6 = ("cec_build_seconds", "cec_write_seconds", "cec_abc_seconds", "cec_cone_ands", "cec_cone_mismatches")
CEC = CEC_V5 + CEC_V6
SUMMARY = BASE_SUMMARY + PROFILE + CEC
FLOAT_FIELDS = {"#T", "verify_seconds", *PROFILE, "cec_wall_seconds",
                "cec_build_seconds", "cec_write_seconds", "cec_abc_seconds"}
TARGET = ("and_var", "pin", "stuck", "alternatives", "search_seconds", "limited", "target_conflict",
          "redundant_dominators", "trials", "sub_probes", "limit_step", "limit_scope")
CANDIDATE = ("and_var", "pin", "stuck", "expression", "outcome", "addition", "removal", "failed_stage",
             "verify_seconds", "end_to_end")
SEARCH_STATUSES = {"complete", "partial", "timeout", "process_error", "input_error"}
VERIFY_STATUSES = {"complete", "incomplete", "unchecked", "not_run"}
TABLE_I = ["wire", "l0", "l1", "l2"]
TABLE_II = ["wire", "gate", "gate_nolearn", "gate_nofilter"]
RESULT_HEAD = ["circuit", "mode", "status", "search_status", "verify_status", "coverage", "reasons",
               "topology_profile", "source_sha256", "aag_sha256", "original_wires", "paper_wire_count",
               "shards", "cached_shards", *SUMMARY, "wall_seconds"]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, data):
    with NamedTemporaryFile(dir=path.parent, mode="w", encoding="utf-8", delete=False) as file:
        temporary = Path(file.name)
        json.dump(data, file, ensure_ascii=False, indent=2)
        file.write("\n")
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


class TsvSink:
    """Stream rows into a temporary file and publish it atomically on commit."""

    def __init__(self, path, headings):
        self.path = path
        self.file = NamedTemporaryFile(dir=path.parent, mode="w", encoding="utf-8", newline="", delete=False)
        self.temporary = Path(self.file.name)
        self.writer = csv.writer(self.file, delimiter="\t")
        self.writer.writerow(headings)

    def rows(self, rows):
        self.writer.writerows(rows)

    def commit(self):
        self.file.close()
        self.temporary.replace(self.path)

    def abort(self):
        self.file.close()
        self.temporary.unlink(missing_ok=True)


def write_tsv(path, headings, rows):
    sink = TsvSink(path, headings)
    try:
        sink.rows(rows)
        sink.commit()
    except BaseException:
        sink.abort()
        raise


def peak_working_set_mb():
    """Peak resident memory of this Python process (children are not included)."""
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes

        class Counters(ctypes.Structure):
            _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD),
                        ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
                        ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
                        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                        ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t)]

        counters = Counters()
        counters.cb = ctypes.sizeof(counters)
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        psapi = ctypes.WinDLL("psapi", use_last_error=True)
        kernel32.GetCurrentProcess.restype = wintypes.HANDLE
        psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
        psapi.GetProcessMemoryInfo.restype = wintypes.BOOL
        if not psapi.GetProcessMemoryInfo(kernel32.GetCurrentProcess(), ctypes.byref(counters), counters.cb):
            return None
        return counters.PeakWorkingSetSize / 2**20
    try:
        import resource
        usage = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        return usage / (1024 if sys.platform != "darwin" else 2**20)
    except ImportError:
        return None


def load_cases(manifest_path, only):
    if not manifest_path:
        return [{"id": "fig1", "path": ROOT / "tests/fixtures/paper/fig1.aag"}]
    manifest_path = manifest_path.resolve()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    parent = manifest_path.parent
    base = parent / manifest.get("base_directory", ".." if parent.name == "manifests" else ".")
    items = []
    seen = set()
    for case in manifest["circuits"]:
        name = case["id"]
        if not name or name in seen:
            raise ValueError(f"empty or duplicate circuit ID {name!r}")
        seen.add(name)
        if not only or name in only:
            items.append(dict(case, path=(base / case["path"]).resolve()))
    if only and not set(only) <= seen:
        raise ValueError("unrecognized circuit ID: " + ", ".join(sorted(set(only) - seen)))
    return items


class InputProblem(Exception):
    """Input could not be used; `reason` is missing_input, fetch_failed, blob_mismatch or conversion_error."""

    def __init__(self, reason, message):
        super().__init__(message)
        self.reason = reason


def git_blob_id(data):
    return hashlib.sha1(f"blob {len(data)}\0".encode() + data).hexdigest()


def read_source(case, fetch_missing):
    path = case["path"]
    expected_blob = case.get("git_blob")

    def check_blob(data):
        if expected_blob and git_blob_id(data) != expected_blob:
            raise InputProblem("blob_mismatch", f"{path.name} does not match its pinned Git blob "
                                                f"{expected_blob} (got {git_blob_id(data)}, {len(data)} bytes)")

    if path.is_file():
        data = path.read_bytes()
        check_blob(data)
        return data
    if not fetch_missing:
        raise InputProblem("missing_input", f"{path} is missing (run with --fetch-missing on a network "
                                            f"that can reach {case.get('source_url', 'the pinned source')})")

    source = case.get("source_url", "")
    if not expected_blob or not source.startswith("https://github.com/") or "/blob/" not in source:
        raise InputProblem("missing_input", "automatic retrieval requires a pinned GitHub source")
    url = source.replace("https://github.com/", "https://raw.githubusercontent.com/", 1).replace("/blob/", "/", 1)
    try:
        with urlopen(url, timeout=60) as response:
            data = response.read()
    except (URLError, OSError, TimeoutError) as error:
        raise InputProblem("fetch_failed", f"cannot fetch {url}: {error}") from error
    check_blob(data)  # validate before anything is written
    path.parent.mkdir(parents=True, exist_ok=True)
    with NamedTemporaryFile(dir=path.parent, delete=False) as temporary:
        filename = Path(temporary.name)
        temporary.write(data)
    try:
        if os.name == "nt":
            filename.rename(path)
        else:
            os.link(filename, path)
    except FileExistsError:
        data = path.read_bytes()  # another run published it first; keep the validated copy
        check_blob(data)
    finally:
        filename.unlink(missing_ok=True)
    return data


def prepare(case, directory, fetch_missing=False):
    raw = read_source(case, fetch_missing)
    source_digest = sha(raw)
    if case["path"].suffix.lower() == ".aag":
        return case["path"], source_digest, source_digest, {"format": "aag", "topology_profile": TOPOLOGY_PROFILE}
    if case["path"].suffix.lower() != ".bench":
        raise InputProblem("conversion_error", "provide an ASCII .aag or gate-level .bench netlist")
    try:
        encoded, mapping = convert(raw.decode("utf-8-sig"), case.get("cut_latches", False))
    except ValueError as error:
        raise InputProblem("conversion_error", str(error)) from error
    cooked = encoded.encode("utf-8")
    digest = sha(cooked)
    destination = directory / (digest + ".aag")
    if not destination.exists():
        destination.write_bytes(cooked)
    write_json(directory / (source_digest + "-" + digest + ".map.json"), {
        "source_sha256": source_digest, "aag_sha256": digest, "git_blob": case.get("git_blob"),
        "source_url": case.get("source_url"), "cut_latches_requested": case.get("cut_latches", False),
        "paper_wire_count": case.get("paper_wire_count"),
        "paper_wire_count_text": case.get("paper_wire_count_text"),
        "conversion": "scripts/bench_to_aag.py", **mapping,
    })
    return destination, source_digest, digest, {
        "format": "bench", "original_wires": mapping["original_wires"],
        "topology_profile": mapping["topology_profile"], "aig_wires": mapping["aig_wires"],
        "converter_version": mapping["converter_version"],
    }


def verify_status_of(args, counts):
    if args.discover_only:
        return "unchecked"
    return "incomplete" if counts["unverified"] else "complete"


def and_count(path):
    with Path(path).open("rb") as file:
        header = file.readline().split()
    if len(header) < 6 or not header[5].isdigit():
        return 0
    return int(header[5])


def bind_cec(args, circuit):
    """Large circuits default to one end-to-end ABC job per candidate and one worker per core."""
    bound = copy.copy(args)
    large = and_count(circuit) > 3000
    if bound.cec_stages is None:
        bound.cec_stages = "e2e" if large else "two"
    if bound.cec_batch is None:
        bound.cec_batch = 64 if large else 32
    if bound.cec_jobs is None:
        bound.cec_jobs = (os.cpu_count() or 1) if large else 1
    return bound


def append_progress(path, circuit, mode, offset, line):
    if path is None:
        return
    new = not path.exists()
    with path.open("a", encoding="utf-8", newline="") as file:
        if new:
            file.write("time\tcircuit\tmode\toffset\tprogress\n")
        file.write(f"{time.strftime('%H:%M:%S')}\t{Path(circuit).stem}\t{mode}\t{offset}\t{line}\n")


def execute(executable, circuit, mode, args, offset=0, limit=None):
    """Run one shard. Stdout is streamed so RAR_PROGRESS can be saved while the run is alive."""
    check = "discover" if args.discover_only else "verify"
    detail = "summary" if args.summary_only else "all_rows"
    limit = args.wire_limit if limit is None else limit
    command = (f'set abc_exe "{args.abc}"\nset cec_stages {args.cec_stages}\n'
               f'set cec_builder {args.cec_builder}\n'
               f'set cec_cone {"on" if args.cec_cone else "off"}\n'
               f'set cec_audit {args.cec_audit}\n'
               f'set cec_diagnose {"on" if args.diagnose_rejected else "off"}\n'
               f'set cec_batch {args.cec_batch}\nset cec_jobs {args.cec_jobs}\n'
               f'set cec_keep {"on" if args.keep_cec_files else "off"}\n'
               f'read "{circuit}"\nrar_batch {mode} {limit} {args.solver_timeout} '
               f'{args.scope} {check} {detail} {args.max_trials} {offset}\nquit\n')
    outcome = {"schema": SCHEMA_VERSION, "offset": offset, "limit": limit, "message": "",
               "targets": [], "candidates": [], "summary": []}
    started = time.monotonic()
    rows = {"RAR_TARGET": [], "RAR_CANDIDATE": [], "RAR_SUMMARY": []}
    collected = []
    with NamedTemporaryFile(mode="w+", encoding="utf-8", errors="replace", suffix=".err") as err:
        process = subprocess.Popen([str(executable), "--shell"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=err, text=True, encoding="utf-8", errors="replace")

        def read_stdout():
            for line in process.stdout:
                collected.append(line)
                stripped = line.rstrip("\n").lstrip("> ")
                if stripped.startswith("RAR_PROGRESS"):
                    append_progress(getattr(args, "progress", None), circuit, mode, offset, stripped)

        reader = threading.Thread(target=read_stdout)
        reader.start()
        try:
            process.stdin.write(command)
            process.stdin.close()
            reader.join(args.case_timeout)
            if reader.is_alive():
                process.kill()
                reader.join()
                outcome.update(search_status="timeout", verify_status="not_run",
                               message=f"no result within {args.case_timeout}s",
                               wall_seconds=time.monotonic() - started)
                return outcome
            returncode = process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            reader.join()
            outcome.update(search_status="timeout", verify_status="not_run",
                           message=f"no result within {args.case_timeout}s",
                           wall_seconds=time.monotonic() - started)
            return outcome
        err.seek(0)
        stderr_tail = err.read()[-2000:].strip()
    for line in collected:
        record = line.rstrip("\n").lstrip("> ").split("\t")
        if record and record[0] in rows:
            rows[record[0]].append(record[1:])
    result = subprocess.CompletedProcess([str(executable)], returncode)
    outcome["wall_seconds"] = time.monotonic() - started
    summary = rows["RAR_SUMMARY"]
    width = len(summary[0]) if len(summary) == 1 else 0
    if width == len(BASE_SUMMARY):
        summary[0] = summary[0] + ["0"] * (len(PROFILE) + len(CEC))
    elif width == len(BASE_SUMMARY) + len(PROFILE):
        summary[0] = summary[0] + ["0"] * len(CEC)
    elif width == len(BASE_SUMMARY) + len(PROFILE) + len(CEC_V5):
        summary[0] = summary[0] + ["0"] * len(CEC_V6)
    if result.returncode or len(summary) != 1 or len(summary[0]) != len(SUMMARY):
        outcome.update(search_status="process_error", verify_status="not_run",
                       message=stderr_tail or f"exit code {result.returncode}, malformed RAR_SUMMARY")
        return outcome
    try:
        values = [float(v) if field in FLOAT_FIELDS else int(v) for field, v in zip(SUMMARY, summary[0])]
    except ValueError:
        outcome.update(search_status="process_error", verify_status="not_run", message="non-numeric RAR_SUMMARY")
        return outcome
    counts = dict(zip(SUMMARY, values))
    if counts["cec_cone_mismatches"]:
        outcome.update(search_status="process_error", verify_status="incomplete", summary=values,
                       targets=rows["RAR_TARGET"], candidates=rows["RAR_CANDIDATE"],
                       message="cec_cone_mismatch")
        return outcome
    outcome["summary"] = values
    outcome["targets"] = rows["RAR_TARGET"]
    outcome["candidates"] = rows["RAR_CANDIDATE"]
    outcome["search_status"] = "partial" if counts["limited"] else "complete"
    outcome["verify_status"] = verify_status_of(args, counts)
    return outcome


def validate_outcome(outcome, args, offset, limit, total_wires):
    """Return an error string when a checkpoint or fresh shard is not internally consistent."""
    try:
        if outcome.get("schema") != SCHEMA_VERSION:
            return "schema version mismatch"
        if outcome["search_status"] not in SEARCH_STATUSES or outcome["verify_status"] not in VERIFY_STATUSES:
            return "unknown status"
        if (outcome["offset"], outcome["limit"]) != (offset, limit):
            return "range mismatch"
        summary = outcome["summary"]
        if outcome["search_status"] in {"timeout", "process_error", "input_error"}:
            if outcome.get("message") == "cec_cone_mismatch":
                return None
            return None if not summary else "failed shard carries counts"
        if len(summary) != len(SUMMARY):
            return "wrong summary arity"
        counts = dict(zip(SUMMARY, summary))
        for field, value in counts.items():
            if field in FLOAT_FIELDS:
                if not isinstance(value, (int, float)) or value < 0:
                    return f"invalid {field}"
            elif not isinstance(value, int) or isinstance(value, bool) or value < 0:
                return f"invalid {field}"
        if counts["complete_wires"] > counts["searched_wires"] or counts["#tar"] > counts["searched_wires"]:
            return "coverage exceeds searched wires"
        expected = min(total_wires - offset, limit) if limit else total_wires - offset
        if counts["searched_wires"] != max(expected, 0):
            return f"searched {counts['searched_wires']} wires, expected {max(expected, 0)}"
        if counts["rejected"] + counts["unverified"] + counts["verified_alts"] > counts["#alt"]:
            return "verification counts exceed #alt"
        if (outcome["search_status"] == "partial") != (counts["limited"] > 0):
            return "search status disagrees with limited count"
        if not args.summary_only:
            keys = [tuple(row[:4]) for row in outcome["candidates"]]
            if len(keys) != len(set(keys)):
                return "duplicate candidate rows"
            if len(keys) != counts["#alt"]:
                return "candidate rows disagree with #alt"
            if len(outcome["targets"]) != 2 * counts["searched_wires"]:
                return "target rows disagree with searched wires"
    except (KeyError, TypeError) as error:
        return f"malformed outcome ({error})"
    return None


def reusable(outcome):
    """Only fully finished shards are checkpointed; unfinished work is always retried."""
    return outcome["search_status"] == "complete" and outcome["verify_status"] in {"complete", "unchecked"}


def total_wire_count(path):
    with path.open("rb") as file:
        header = file.readline().split()
    if len(header) < 6 or header[0] != b"aag" or not header[5].isdigit():
        raise InputProblem("conversion_error", "not an ASCII AIGER header")
    return 2 * int(header[5])


def run_cached(executable, path, mode, args, cache, identity):
    """Run all shards of one (circuit, mode); a failed shard never stops the others."""
    total = total_wire_count(path)
    end = min(total, args.wire_limit) if args.wire_limit else total
    if args.shard_wires:
        ranges = [(start, min(args.shard_wires, end - start)) for start in range(0, end, args.shard_wires)] or [(0, 0)]
    else:
        ranges = [(0, args.wire_limit)]
    shards = []
    for offset, limit in ranges:
        key = sha(json.dumps(dict(identity, offset=offset, limit=limit), sort_keys=True).encode())
        checkpoint = cache / (key + ".json")
        outcome, cached = None, False
        if checkpoint.exists():
            try:
                candidate = json.loads(checkpoint.read_text(encoding="utf-8"))
                problem = validate_outcome(candidate, args, offset, limit, total)
            except (OSError, ValueError) as error:
                candidate, problem = None, f"unreadable ({error})"
            if problem is None and candidate is not None and reusable(candidate):
                outcome, cached = candidate, True
            else:
                quarantine = checkpoint.with_suffix(f".corrupt-{int(time.time())}")
                checkpoint.replace(quarantine)
                print(f"  checkpoint {checkpoint.name} rejected ({problem or 'not reusable'}); recomputing",
                      file=sys.stderr)
        if outcome is None:
            outcome = execute(executable, path, mode, args, offset, limit)
            problem = validate_outcome(outcome, args, offset, limit, total)
            if problem:
                outcome.update(search_status="process_error", verify_status="not_run", summary=[],
                               targets=[], candidates=[], message=f"inconsistent shard result: {problem}")
            elif reusable(outcome):
                write_json(checkpoint, outcome)
        outcome["cached"] = cached
        shards.append(outcome)
    return aggregate(shards, args, total)


def aggregate(shards, args, total):
    """Combine shard outcomes without letting a later failure replace an earlier reason."""
    reasons = []
    for shard in shards:
        tag = f"{shard['offset']}+{shard['limit']}"
        if shard["search_status"] != "complete":
            reasons.append(f"search:{shard['search_status']}@{tag}")
        if shard["verify_status"] == "incomplete":
            reasons.append(f"verify:incomplete@{tag}")
        if shard.get("message"):
            reasons.append(f"msg@{tag}:{shard['message'][:120].replace(chr(9), ' ')}")
    done = [s for s in shards if s["summary"]]
    summary = [0.0 if field in FLOAT_FIELDS else 0 for field in SUMMARY]
    for shard in done:
        for i, value in enumerate(shard["summary"]):
            summary[i] += value
    summary[0] = total  # #wire is a property of the circuit, not a sum over shards
    targets = [row for shard in done for row in shard["targets"]]
    candidates = [row for shard in done for row in shard["candidates"]]
    keys = [tuple(row[:4]) for row in candidates]
    if len(keys) != len(set(keys)):
        reasons.append("process:duplicate candidates across shards")

    statuses = {shard["search_status"] for shard in shards}
    if "timeout" in statuses:
        search = "timeout"
    elif "process_error" in statuses or "process:duplicate candidates across shards" in reasons:
        search = "process_error"
    elif "partial" in statuses:
        search = "partial"
    else:
        search = "complete"
    verify_values = {shard["verify_status"] for shard in shards}
    if "incomplete" in verify_values:
        verify = "incomplete"
    elif "not_run" in verify_values:
        verify = "not_run"
    elif verify_values == {"unchecked"}:
        verify = "unchecked"
    else:
        verify = "complete"
    # Coverage: full needs contiguous ranges over every pin, both stuck values finished, no limit hit.
    ranges = sorted((s["offset"], s["limit"]) for s in shards)
    contiguous = all(ranges[i][0] + ranges[i][1] == ranges[i + 1][0] for i in range(len(ranges) - 1))
    covers = bool(ranges) and ranges[0][0] == 0
    searched, complete = int(summary[1]), int(summary[2])
    if args.wire_limit and args.wire_limit < total:
        coverage = "smoke"
    elif contiguous and covers and len(done) == len(shards) and searched == total and complete == total:
        coverage = "full"
    else:
        coverage = "partial"
    status = ("process_error" if search == "process_error" else "timeout" if search == "timeout" else
              "search_limited" if search == "partial" else
              "verification_incomplete" if verify in {"incomplete", "not_run"} else "ok")
    return {"status": status, "search_status": search, "verify_status": verify, "coverage": coverage,
            "reasons": ";".join(reasons), "summary": summary, "targets": targets, "candidates": candidates,
            "shards": len(shards), "cached_shards": sum(1 for s in shards if s.get("cached")),
            "wall_seconds": sum(s.get("wall_seconds", 0.0) for s in shards if not s.get("cached"))}


def number(value):
    return f"{value:.6g}" if isinstance(value, float) else value


def result_row(case, mode, outcome, original, digest, conversion):
    summary = outcome["summary"] or [""] * len(SUMMARY)
    return [case["id"], mode, outcome["status"], outcome["search_status"], outcome["verify_status"],
            outcome["coverage"], outcome["reasons"], conversion.get("topology_profile", TOPOLOGY_PROFILE),
            original, digest, conversion.get("original_wires", ""), case.get("paper_wire_count_text", ""),
            outcome["shards"], outcome["cached_shards"], *[number(v) for v in summary],
            number(outcome["wall_seconds"])]


def write_tables(out, results):
    index = {name: i for i, name in enumerate(RESULT_HEAD)}
    for table, modes in [("table_I", TABLE_I), ("table_II", TABLE_II)]:
        by_case = {}
        for row in results:
            by_case.setdefault(row[0], {})[row[1]] = row
        headings = ["circuit", "#wire", "paper_wire_count", "original_wires", "topology_profile"]
        headings.extend(f"{mode}:{metric}" for mode in modes
                        for metric in ("status", "coverage", "searched_wires", "complete_wires", "#tar", "#alt", "#T"))
        rows = []
        for name, found in by_case.items():
            example = next(iter(found.values()))
            values = [name, example[index["#wire"]], example[index["paper_wire_count"]],
                      example[index["original_wires"]], example[index["topology_profile"]]]
            for mode in modes:
                row = found.get(mode)
                if row:
                    values.extend([row[index["status"]], row[index["coverage"]], row[index["searched_wires"]],
                                   row[index["complete_wires"]], row[index["#tar"]], row[index["#alt"]],
                                   row[index["#T"]]])
                else:
                    values.extend(["not_run", "", "", "", "", "", ""])
            rows.append(values)
        write_tsv(out / (table + ".tsv"), headings, rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "build-msvc/Release/satrar.exe")
    parser.add_argument("--abc", type=Path, help="ABC executable (default: SATRAR_ABC_EXE or the v1 tools copy)")
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--case", action="append", default=[])
    parser.add_argument("--modes", nargs="+", choices=MODES, default=MODES)
    parser.add_argument("--wire-limit", type=int, default=0, help="0 means all AND receiving pins")
    parser.add_argument("--shard-wires", type=int, default=0, help="checkpoint consecutive groups of this many wires")
    parser.add_argument("--solver-timeout", type=int, default=15, help="seconds per CEC stage")
    parser.add_argument("--case-timeout", type=int, default=300, help="seconds per circuit/mode (per shard when sharded)")
    parser.add_argument("--scope", choices=["paper", "all"], default="paper")
    parser.add_argument("--discover-only", action="store_true", help="measure discovery without accepting candidates")
    parser.add_argument("--summary-only", action="store_true", help="skip individual target and candidate rows")
    parser.add_argument("--max-trials", type=int, default=20000, help="search trials per stuck-at case")
    parser.add_argument("--fetch-missing", action="store_true", help="retrieve missing pinned input files")
    parser.add_argument("--cec-stages", choices=["two", "e2e"], default=None,
                        help="default: e2e when the circuit has more than 3000 AND gates, else two")
    parser.add_argument("--cec-builder", choices=["fast", "reference"], default="fast")
    parser.add_argument("--cec-cone", action="store_true", help="send ABC only the outputs the candidate can change")
    parser.add_argument("--cec-audit", type=int, default=0, help="with --cec-cone, recheck every Nth candidate in full")
    parser.add_argument("--diagnose-rejected", action="store_true",
                        help="on an e2e rejection, also record the two-stage reference verdicts")
    parser.add_argument("--cec-batch", type=int, default=None, help="default: 64 on large circuits, else 32")
    parser.add_argument("--cec-jobs", type=int, default=None,
                        help="default: one worker per core on large circuits, else 1")
    parser.add_argument("--keep-cec-files", action="store_true")
    parser.add_argument("--out", type=Path, default=ROOT / "results/satrar_batch")
    args = parser.parse_args()
    if args.wire_limit < 0 or args.shard_wires < 0 or args.solver_timeout < 1 or args.case_timeout < 1:
        parser.error("use a nonnegative wire limit and positive timeouts")
    if (args.cec_batch is not None and args.cec_batch < 1) or (args.cec_jobs is not None and args.cec_jobs < 1):
        parser.error("cec batch size and job count must be positive")
    if args.cec_audit < 0:
        parser.error("cec audit interval must be nonnegative")
    if not 1 <= args.max_trials <= 2**32 - 1:
        parser.error("max-trials must be between 1 and 4294967295")
    executable = args.exe.resolve(strict=True)
    default_abc = ROOT.parent / "satrar-cpp" / "tools" / "abc" / "bin" / ("yosys-abc.exe" if os.name == "nt" else "yosys-abc")
    abc = (args.abc or Path(os.getenv("SATRAR_ABC_EXE") or default_abc)).resolve(strict=True)
    args.abc = abc
    os.environ["SATRAR_ABC_EXE"] = str(abc)
    abc_banner = subprocess.check_output([str(abc), "-s", "-c", "quit"], text=True, stderr=subprocess.STDOUT).strip()
    fingerprint = {"schema": SCHEMA_VERSION, "converter_version": CONVERTER_VERSION,
                   "topology_profile": TOPOLOGY_PROFILE, "pin_order": PIN_ORDER,
                   "exe_sha256": sha(executable.read_bytes()), "abc_sha256": sha(abc.read_bytes()),
                   "abc_banner": abc_banner, "host": platform.platform(),
                   "wire_limit": args.wire_limit, "solver_timeout": args.solver_timeout,
                   "case_timeout": args.case_timeout, "scope": args.scope, "verify": not args.discover_only,
                   "summary_only": args.summary_only, "max_trials": args.max_trials,
                   "shard_wires": args.shard_wires, "decision_depth": 2,
                   "cec_stages": args.cec_stages or "auto", "cec_batch": args.cec_batch or "auto",
                   "cec_jobs": args.cec_jobs or "auto", "cec_builder": args.cec_builder,
                   "cec_cone": args.cec_cone, "cec_audit": args.cec_audit,
                   "diagnose_rejected": args.diagnose_rejected, "keep_cec_files": args.keep_cec_files}
    args.progress = args.out / "progress.tsv"
    cache = args.out / "runs"
    inputs = args.out / "converted"
    cache.mkdir(parents=True, exist_ok=True)
    inputs.mkdir(parents=True, exist_ok=True)
    write_json(args.out / "settings.json", fingerprint)

    targets = TsvSink(args.out / "targets.tsv", ["circuit", "mode", *TARGET])
    candidates = TsvSink(args.out / "candidates.tsv", ["circuit", "mode", *CANDIDATE])
    results = []
    started = time.monotonic()
    try:
        for case in load_cases(args.manifest, args.case):
            try:
                path, original, digest, conversion = prepare(case, inputs, args.fetch_missing)
            except (OSError, InputProblem) as error:
                reason = error.reason if isinstance(error, InputProblem) else "missing_input"
                for mode in args.modes:
                    row = [case["id"], mode, "input_error", "input_error", "not_run", "none",
                           f"input:{reason}:{str(error)[:160]}", TOPOLOGY_PROFILE, "", "", "",
                           case.get("paper_wire_count_text", ""), 0, 0, *[""] * len(SUMMARY), ""]
                    results.append(row)
                print(f"{case['id']}: {reason}: {error}", file=sys.stderr)
                continue
            for mode in args.modes:
                bound = bind_cec(args, path)
                identity = dict(fingerprint, mode=mode, source_sha256=original, aag_sha256=digest,
                                cec_stages=bound.cec_stages, cec_batch=bound.cec_batch, cec_jobs=bound.cec_jobs)
                outcome = run_cached(executable, path, mode, bound, cache, identity)
                prefix = [case["id"], mode]
                results.append(result_row(case, mode, outcome, original, digest, conversion))
                targets.rows(prefix + row for row in outcome["targets"])
                candidates.rows(prefix + row for row in outcome["candidates"])
                outcome["targets"] = outcome["candidates"] = None  # release before the next mode
                print(f"{case['id']} {mode} {outcome['status']} search={outcome['search_status']} "
                      f"verify={outcome['verify_status']} coverage={outcome['coverage']}")
    except BaseException:
        targets.abort()
        candidates.abort()
        raise
    targets.commit()
    candidates.commit()
    write_tsv(args.out / "summary.tsv", RESULT_HEAD, results)
    write_tables(args.out, results)
    write_json(args.out / "run_report.json", {
        "wall_seconds": time.monotonic() - started,
        "peak_working_set_mb_python_process": peak_working_set_mb(),
        "note": "peak memory covers this Python process only; the satrar child process is not included",
        "rows": len(results)})
    return 0 if results and all(row[2] == "ok" for row in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
