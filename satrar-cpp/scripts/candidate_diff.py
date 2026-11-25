"""Compare the candidate sets of two rar_batch runs by canonical key (target, attachment, operands).

The evidence label is deliberately not part of the key: relabeling (e.g. Corollary-2.1 -> Theorem-1)
is a provenance change, not a different rewrite. Usage:
    python scripts/candidate_diff.py OLD_DIR NEW_DIR [--mode gate_nofilter]
"""

import argparse
import csv
import re
from collections import defaultdict
from pathlib import Path


def key_of(row):
    expression = re.sub(r"\brule=\S+\s*", "", row["expression"]).strip()
    return (row["and_var"], row["pin"], row["stuck"], expression)


def load(directory):
    sets = defaultdict(dict)
    with (Path(directory) / "candidates.tsv").open(encoding="utf-8", newline="") as file:
        for row in csv.DictReader(file, delimiter="\t"):
            sets[(row["circuit"], row["mode"])][key_of(row)] = row
    return sets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("old")
    parser.add_argument("new")
    parser.add_argument("--mode", action="append", default=[])
    args = parser.parse_args()
    old, new = load(args.old), load(args.new)
    for group in sorted(set(old) | set(new)):
        if args.mode and group[1] not in args.mode:
            continue
        removed = sorted(set(old.get(group, {})) - set(new.get(group, {})))
        added = sorted(set(new.get(group, {})) - set(old.get(group, {})))
        print(f"{group[0]} {group[1]}: old={len(old.get(group, {}))} new={len(new.get(group, {}))} "
              f"removed={len(removed)} added={len(added)}")
        for sign, keys, source in (("-", removed, old), ("+", added, new)):
            for key in keys:
                row = source[group][key]
                print(f"  {sign} target={key[0]}/{key[1]}/sa{key[2]} {key[3]} "
                      f"[{row['outcome']} add={row['addition']} rem={row['removal']}]")


if __name__ == "__main__":
    main()
