"""Scan both stuck values at every AAG AND input pin and verify RAR candidates."""

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import re
import shutil
import subprocess
from tempfile import TemporaryDirectory


MODES = ("l0", "l1", "l2", "wire", "gate", "gate_nolearn", "gate_nofilter", "sub")
EXACT = re.compile(r"^(SAT|UNSAT|UNKNOWN) reason=(\S+)( redundant=1)?$")
SUMMARY = re.compile(r"^mode=(\w+) candidates=(\d+) trials=(\d+) .*? limited=([01])(?: |$)")
CANDIDATE = re.compile(r"^cand id=\d+ key=(\S+) cec=(accepted|rejected|indeterminate)$")


def gates_of(aag):
    lines = aag.read_text(encoding="utf-8").splitlines()
    kind, _, inputs, latches, outputs, ands, *_ = lines[0].split()
    if kind != "aag" or int(latches):
        raise ValueError("Expected combinational ASCII AAG")
    start = 1 + int(inputs) + int(outputs)
    gates = []
    for row in lines[start : start + int(ands)]:
        output, left, right = map(int, row.split())
        gates.append((output // 2, left, right))
    if len(gates) != int(ands):
        raise ValueError("AAG AND count does not match its gate rows")
    return gates


def run(exe, cwd, commands):
    process = subprocess.run(
        [str(exe)],
        input="\n".join(["read input.aag", *commands, "quit", ""]),
        cwd=cwd,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=True,
    )
    lines = process.stdout.splitlines()
    if process.stderr.strip() or not lines or not lines[0].startswith("read pi="):
        raise RuntimeError(process.stderr.strip() or process.stdout or "No CLI response")
    return lines[1:]


def exact_results(lines):
    return [(m[1], m[2], bool(m[3])) for line in lines if (m := EXACT.fullmatch(line))]


def scan_pin(exe, cwd, target, modes, seconds):
    var, pin, stuck, _ = target
    arg = f"{var} {pin} sa{stuck}"
    lines = run(exe, cwd, [
        f"fault_exact pin {arg}",
        *(f"rar_verify {arg} {mode} {seconds}" for mode in modes),
    ])
    (exact,) = exact_results(lines)
    searches = {}
    for line in lines:
        if match := SUMMARY.match(line):
            mode, count, trials, limited = match.groups()
            searches[mode] = (int(count), int(trials), limited == "1", {})
        elif match := CANDIDATE.fullmatch(line):
            key, verdict = match.groups()
            candidates = searches[mode][3]
            if key in candidates or not key.startswith(f"{var},{pin},{stuck},"):
                raise RuntimeError(f"Duplicate or mismatched key: {key}")
            candidates[key] = verdict
    if tuple(searches) != modes or any(count != len(items) for count, _, _, items in searches.values()):
        raise RuntimeError(f"Incomplete output for {arg}")
    return target, exact, searches


def accepted(search):
    return {key for key, verdict in search[3].items() if verdict == "accepted"}


def write_report(path, aag, gates, rows, output_faults, modes, seconds):
    accepted_by_mode = {mode: set() for mode in modes}
    keys = {}
    for _, _, searches in rows:
        for mode, search in searches.items():
            accepted_by_mode[mode].update(accepted(search))
            for key, verdict in search[3].items():
                keys.setdefault(key, []).append(f"{mode}（{verdict}）")
    distinct_accepted = set().union(*accepted_by_mode.values())
    lines = [
        "# V2 全電路替代候選掃描",
        "",
        f"輸入 `{aag.name}`。共 {len(gates)} 個 AIG AND、{len(rows) // 2} 個接收腳、{len(rows)} 個 pin fault。",
        f"每個接收腳查 sa0 與 sa1。每筆候選用 V2 的兩階段 CEC 驗證，每階段 timeout 為 {seconds} 秒。",
        "沿用 CLI 預設的接入範圍，包含目標直接接收 gate。",
        "",
        "## 各模式統計",
        "",
        "| 模式 | #wire | #tar 有通過候選的接收腳 | #alt 通過 CEC 的候選 | 搜尋回傳 | CEC 拒絕 | CEC 未判定 | 達試探上限的 fault |",
        "|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for mode in modes:
        pins = {(var, pin) for (var, pin, _, _), _, searches in rows if accepted(searches[mode])}
        searches = [searches[mode] for _, _, searches in rows]
        verdicts = Counter(v for search in searches for v in search[3].values())
        lines.append(
            f"| {mode} | {len(rows) // 2} | {len(pins)} | {verdicts['accepted']} | "
            f"{sum(s[0] for s in searches)} | {verdicts['rejected']} | "
            f"{verdicts['indeterminate']} | {sum(s[2] for s in searches)} |"
        )
    lines += [
        "",
        "同一接收腳的 sa0／sa1 在 #tar 合計一次。#alt 以帶有 fault、接入點與帶極性表達式的 candidate key 計數。",
        f"合計 {sum(map(len, accepted_by_mode.values()))} 筆模式輸出通過 CEC，跨模式去重後為 {len(distinct_accepted)} 筆。",
        "",
        "## 排除直接接收 gate 後的統計",
        "",
        "這張表對已驗證候選做事後篩選，僅保留接入 gate 與目標 gate 不同的 keys。",
        "搜尋時仍包含直接接收 gate，所以這些計數僅顯示接入範圍的影響。完整論文口徑需要在搜尋前限制接入點，並單獨量測搜尋時間。",
        "",
        "| 模式 | #tar | #alt |",
        "|---|---:|---:|",
    ]
    for mode, mode_keys in accepted_by_mode.items():
        farther = [key.split(",", 4) for key in mode_keys]
        farther = [key for key in farther if key[0] != key[3]]
        lines.append(f"| {mode} | {len({(k[0], k[1]) for k in farther})} | {len(farther)} |")
    lines += [
        "",
        "## 每個輸入腳與 stuck-at 值",
        "",
        "每一列的各模式欄位是 CEC 通過筆數。* 表示該模式的試探達上限。",
        "Exact SAT 表示原電路可偵測該 fault。UNSAT 且標為冗餘表示原本已可把該腳固定為指定常數。UNKNOWN 表示未判定。",
        "",
        "| AIG var | pin | 來源 raw literal | fault | Exact | " + " | ".join(modes) + " |",
        "|---:|---:|---:|---|---|" + "---:|" * len(modes),
    ]
    for (var, pin, stuck, raw), (state, reason, redundant), searches in rows:
        result = f"{state}（原本冗餘）" if redundant else state
        if state == "UNKNOWN":
            result += f"（{reason}）"
        cells = [str(len(accepted(searches[m]))) + ("*" if searches[m][2] else "") for m in modes]
        lines.append(f"| {var} | {pin} | {raw} | sa{stuck} | {result} | " + " | ".join(cells) + " |")

    by_gate = Counter(int(key.split(",", 4)[3]) for key in distinct_accepted)
    lines += [
        "", "## 每個 gate 的輸出與接入候選", "",
        "輸出 sa0／sa1 欄位是直接固定該 gate 輸出時的 exact 判定。",
        "接入候選欄位按接入 gate 計算通過 CEC 的不同 keys，同一 key 跨模式只算一次。",
        "", "| AIG var | 輸出 sa0 | 輸出 sa1 | 接入此 gate 的已驗證候選 |",
        "|---:|---|---|---:|",
    ]
    for i, (var, _, _) in enumerate(gates):
        verdicts = [f"{state}（原本冗餘）" if redundant else state
                    for state, _, redundant in output_faults[i * 2 : i * 2 + 2]]
        lines.append(f"| {var} | {' | '.join(verdicts)} | {by_gate[var]} |")
    lines += [
        "", "## 候選明細", "",
        "Key 依序為目標 var、pin、stuck 值、接入 gate var、類型和表達式。w 表示 wire，後面是接入 literal 的 raw 值。",
        "g 表示 gate，斜線左邊是 separator 的 AND 項，右邊是可選的 OR 項。改寫接入 separator 的反相。",
        "", "| Canonical key | 模式與 CEC 結果 |", "|---|---|",
    ]
    lines.extend(f"| `{key}` | {', '.join(labels)} |" for key, labels in keys.items())
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("aag", type=Path)
    parser.add_argument("--out", required=True, type=Path, help="Markdown report path")
    parser.add_argument("--exe", type=Path, default=Path("build-msvc/Release/satrar_v2.exe"))
    parser.add_argument("--modes", nargs="+", choices=MODES, default=MODES)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--cec-seconds", type=int, default=10)
    args = parser.parse_args()
    if args.workers < 1 or args.cec_seconds < 1:
        parser.error("--workers and --cec-seconds must be positive")
    aag, exe, output = args.aag.resolve(), args.exe.resolve(), args.out.resolve()
    if output.suffix.lower() != ".md":
        parser.error("--out must be a Markdown file")
    modes = tuple(dict.fromkeys(args.modes))
    gates = gates_of(aag)
    targets = [(var, pin, stuck, raw) for var, left, right in gates
               for pin, raw in enumerate((left, right)) for stuck in (0, 1)]
    with TemporaryDirectory(prefix="satrar-aag-scan-") as temp:
        cwd = Path(temp)
        shutil.copyfile(aag, cwd / "input.aag")
        output_faults = exact_results(run(exe, cwd, [
            f"fault_exact dom {var} sa{stuck}" for var, _, _ in gates for stuck in (0, 1)
        ]))
        if len(output_faults) != 2 * len(gates):
            raise RuntimeError("Incomplete gate-output fault scan")
        with ThreadPoolExecutor(max_workers=args.workers) as pool:
            rows = list(pool.map(lambda target: scan_pin(exe, cwd, target, modes, args.cec_seconds), targets))
    write_report(output, aag, gates, rows, output_faults, modes, args.cec_seconds)
    print(f"Wrote {output} ({len(targets)} pin faults; {len(modes)} modes)")


if __name__ == "__main__":
    main()
