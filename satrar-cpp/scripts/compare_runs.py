"""Compare two rar_batch output directories ignoring timing columns.

Used to show that a sharded / resumed run equals a monolithic run: summary counts, target rows and
candidate rows must be identical once seconds and bookkeeping columns are dropped.
    python scripts/compare_runs.py DIR_A DIR_B
"""

import argparse
import csv
import sys
from pathlib import Path

IGNORED = {"#T", "verify_seconds", "wall_seconds", "shards", "cached_shards", "search_seconds",
           "prof_setup_seconds", "prof_projection_seconds", "prof_trials_seconds", "prof_canonical_seconds",
           "reasons"}


def rows(path):
    with Path(path).open(encoding="utf-8", newline="") as file:
        return list(csv.DictReader(file, delimiter="\t"))


def canonical(table, name):
    return sorted(tuple((k, v) for k, v in sorted(row.items()) if k not in IGNORED) for row in table)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("a")
    parser.add_argument("b")
    args = parser.parse_args()
    ok = True
    for name in ("summary.tsv", "targets.tsv", "candidates.tsv"):
        left, right = canonical(rows(Path(args.a) / name), name), canonical(rows(Path(args.b) / name), name)
        same = left == right
        ok &= same
        print(f"{name}: {len(left)} vs {len(right)} rows -> {'IDENTICAL' if same else 'DIFFERENT'}")
        if not same:
            for item in [x for x in left if x not in right][:5]:
                print("  only in A:", item)
            for item in [x for x in right if x not in left][:5]:
                print("  only in B:", item)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
