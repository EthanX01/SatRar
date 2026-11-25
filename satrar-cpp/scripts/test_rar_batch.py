"""Bookkeeping tests for scripts/rar_batch.py using a scripted `execute` (no satrar process needed).

Run: python -m unittest scripts/test_rar_batch.py
"""

import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rar_batch as rb  # noqa: E402

TOTAL = 16  # AND receiving pins of the scripted circuit (8 AND gates)


def args(**overrides):
    base = dict(wire_limit=0, shard_wires=4, solver_timeout=15, case_timeout=60, scope="paper",
                discover_only=False, summary_only=False, max_trials=100)
    base.update(overrides)
    return SimpleNamespace(**base)


def scripted(offset, limit, search="complete", verify="complete", limited=0, unverified=0, message=""):
    """A shard outcome shaped like rb.execute's: one candidate per wire, two stuck rows per wire."""
    if search in {"timeout", "process_error"}:
        return {"schema": rb.SCHEMA_VERSION, "offset": offset, "limit": limit, "search_status": search,
                "verify_status": "not_run", "message": message, "targets": [], "candidates": [], "summary": [],
                "wall_seconds": 0.1}
    n = limit or TOTAL
    targets = [[str(offset + i), "0", str(s), "1", "0.001", "0", "0", "0", "3", "0", "none", "none"]
               for i in range(n) for s in (0, 1)]
    candidates = [[str(offset + i), "0", "1", f"attach={offset + i}", "verified_equivalent", "equivalent",
                   "equivalent", "none", "0.01"] for i in range(n)]
    summary = [TOTAL, n, n - (1 if limited else 0), n, n, 0.5, n - unverified, n - unverified, 0, unverified,
               limited, 0, 3 * n, 0.2, 0.0, 0.0, 0.0, 0.0]
    return {"schema": rb.SCHEMA_VERSION, "offset": offset, "limit": limit, "search_status": search,
            "verify_status": verify, "message": message, "targets": targets, "candidates": candidates,
            "summary": summary, "wall_seconds": 0.1}


class Fixture(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.circuit = self.dir / "c.aag"
        self.circuit.write_text("aag 10 2 0 1 8\n")  # only the header is read for the pin count
        self.cache = self.dir / "runs"
        self.cache.mkdir()
        self.identity = {"mode": "wire", "source_sha256": "s", "aag_sha256": "a"}
        self.calls = []
        self.original = rb.execute

    def tearDown(self):
        rb.execute = self.original
        self.tmp.cleanup()

    def patch(self, script):
        def fake(executable, circuit, mode, a, offset=0, limit=None):
            self.calls.append((offset, limit))
            return script(offset, limit)
        rb.execute = fake

    def run_cached(self, a):
        return rb.run_cached(None, self.circuit, "wire", a, self.cache, self.identity)


class Tests(Fixture):
    def test_limit_and_unverified_are_both_kept(self):
        self.patch(lambda o, l: scripted(o, l, search="partial", verify="incomplete", limited=1, unverified=2))
        outcome = self.run_cached(args())
        self.assertEqual(outcome["search_status"], "partial")
        self.assertEqual(outcome["verify_status"], "incomplete")
        self.assertIn("search:partial@0+4", outcome["reasons"])
        self.assertIn("verify:incomplete@0+4", outcome["reasons"])
        self.assertEqual(outcome["status"], "search_limited")  # compact summary never hides the second reason
        self.assertEqual(outcome["coverage"], "partial")        # a limited pin is entered but not complete
        self.assertEqual(outcome["summary"][1], TOTAL)           # searched
        self.assertEqual(outcome["summary"][2], TOTAL - 4)       # complete: one limited pin per shard

    def test_second_shard_timeout_keeps_other_shards_and_resume_matches_monolithic(self):
        failing = {"on": True}

        def script(offset, limit):
            if offset == 4 and failing["on"]:
                return scripted(offset, limit, search="timeout", message="no result within 60s")
            return scripted(offset, limit)

        self.patch(script)
        first = self.run_cached(args())
        self.assertEqual(self.calls, [(0, 4), (4, 4), (8, 4), (12, 4)])  # later shards still ran
        self.assertEqual(first["search_status"], "timeout")
        self.assertEqual(first["coverage"], "partial")
        self.assertEqual(first["summary"][1], 12)
        self.assertEqual(len(list(self.cache.glob("*.json"))), 3)  # only finished shards are checkpointed

        failing["on"] = False
        self.calls.clear()
        resumed = self.run_cached(args())
        self.assertEqual(self.calls, [(4, 4)])  # only the missing range is recomputed
        self.assertEqual(resumed["cached_shards"], 3)
        self.assertEqual(resumed["coverage"], "full")

        # Same non-time content as one uninterrupted, unsharded scan.
        other = self.dir / "other"
        other.mkdir()
        rb.execute = lambda *a_, **k_: scripted(0, 0)
        mono = rb.run_cached(None, self.circuit, "wire", args(shard_wires=0), other, self.identity)
        self.assertEqual(mono["summary"][1:5], resumed["summary"][1:5])
        self.assertEqual(sorted(tuple(r[1:]) for r in mono["candidates"]),
                         sorted(tuple(r[1:]) for r in resumed["candidates"]))
        self.assertEqual(sorted(tuple(r[1:]) for r in mono["targets"]),
                         sorted(tuple(r[1:]) for r in resumed["targets"]))
    def test_shard_failure_reasons_do_not_overwrite_each_other(self):
        def script(offset, limit):
            if offset == 0:
                return scripted(offset, limit, search="timeout", message="slow")
            if offset == 4:
                return scripted(offset, limit, search="process_error", message="crash")
            return scripted(offset, limit, search="partial", verify="incomplete", limited=1, unverified=1)

        self.patch(script)
        outcome = self.run_cached(args())
        for tag in ("search:timeout@0+4", "search:process_error@4+4", "search:partial@8+4", "verify:incomplete@12+4"):
            self.assertIn(tag, outcome["reasons"])
        self.assertEqual(outcome["search_status"], "timeout")

    def test_corrupt_or_forged_checkpoints_are_quarantined(self):
        good = scripted(0, 4)
        forged = {
            "truncated": None,
            "wrong schema": dict(good, schema=1),
            "negative count": dict(good, summary=[TOTAL, 4, 4, 4, -1, 0.5, 4, 4, 0, 0, 0, 0, 12, 0.2, 0.0, 0.0, 0.0, 0.0]),
            "wrong range": dict(good, offset=8),
            "status lies": dict(good, search_status="complete", summary=[TOTAL, 4, 4, 4, 4, 0.5, 4, 4, 0, 0, 2, 0, 12, 0.2, 0.0, 0.0, 0.0, 0.0]),
            "duplicate candidates": dict(good, candidates=good["candidates"] + good["candidates"][:1]),
            "coverage exceeds": dict(good, summary=[TOTAL, 4, 5, 4, 4, 0.5, 4, 4, 0, 0, 0, 0, 12, 0.2, 0.0, 0.0, 0.0, 0.0]),
        }
        for name, payload in forged.items():
            with self.subTest(name):
                self.calls.clear()
                for old in self.cache.iterdir():
                    old.unlink()
                a = args()
                key = rb.sha(json.dumps(dict(self.identity, offset=0, limit=4), sort_keys=True).encode())
                path = self.cache / (key + ".json")
                path.write_text('{"schema": 3, "offs' if payload is None else json.dumps(payload))
                self.patch(lambda o, l: scripted(o, l))
                self.run_cached(a)
                self.assertIn((0, 4), self.calls, "forged checkpoint must not be reused")
                self.assertTrue(list(self.cache.glob("*.corrupt-*")), "bad checkpoint must be kept for inspection")
                for leftover in self.cache.glob("*.corrupt-*"):
                    leftover.unlink()

    def test_wire_limit_is_a_smoke_run_not_full(self):
        self.patch(lambda o, l: scripted(o, l))
        outcome = self.run_cached(args(shard_wires=0, wire_limit=4))
        self.assertEqual(outcome["coverage"], "smoke")
        self.assertEqual(outcome["search_status"], "complete")
        self.assertEqual(outcome["summary"][0], TOTAL)  # #wire is the circuit's, not the sample's

    def test_tables_keep_per_mode_coverage(self):
        def row(mode, status, coverage, searched, complete, tar):
            out = {name: "" for name in rb.RESULT_HEAD}
            out.update(circuit="c", mode=mode, status=status, search_status="x", verify_status="y",
                       coverage=coverage, topology_profile="aig", original_wires="20", paper_wire_count="19",
                       **{"#wire": 16, "searched_wires": searched, "complete_wires": complete, "#tar": tar,
                          "#alt": tar, "#T": 0.1})
            return [out[name] for name in rb.RESULT_HEAD]

        results = [row("wire", "ok", "full", 16, 16, 5), row("l0", "timeout", "partial", 8, 4, 1)]
        rb.write_tables(self.dir, results)
        table = list(csv.DictReader((self.dir / "table_I.tsv").open(encoding="utf-8"), delimiter="\t"))[0]
        self.assertEqual((table["wire:coverage"], table["wire:searched_wires"], table["wire:complete_wires"]),
                         ("full", "16", "16"))
        self.assertEqual((table["l0:status"], table["l0:coverage"], table["l0:searched_wires"],
                          table["l0:complete_wires"]), ("timeout", "partial", "8", "4"))
        self.assertEqual(table["l1:status"], "not_run")
        self.assertEqual(table["paper_wire_count"], "19")

    def test_input_blob_mismatch_is_not_a_missing_input(self):
        path = self.dir / "x.bench"
        path.write_bytes(b"INPUT(a)\n")
        with self.assertRaises(rb.InputProblem) as caught:
            rb.read_source({"path": path, "git_blob": "0" * 40}, False)
        self.assertEqual(caught.exception.reason, "blob_mismatch")
        with self.assertRaises(rb.InputProblem) as caught:
            rb.read_source({"path": self.dir / "absent.bench"}, False)
        self.assertEqual(caught.exception.reason, "missing_input")
        data = b"INPUT(a)\nOUTPUT(z)\nz = NOT(a)\n"
        good = self.dir / "ok.bench"
        good.write_bytes(data)
        self.assertEqual(rb.read_source({"path": good, "git_blob": rb.git_blob_id(data)}, False), data)

if __name__ == "__main__":
    unittest.main()
