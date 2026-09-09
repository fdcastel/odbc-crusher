#!/usr/bin/env python3
"""Diff two odbc-crusher JSON reports probe by probe.

IMPROVEMENT_PLAN_V2.md **T2**.

The question this answers is "did that change detect anything?", and until now
it was answered by an ad-hoc script in a scratch directory. It is the tool
behind the H17 measurement: two runs of the same driver at different builds,
differing only in the install step, diffed by `test_name`.

    python tools/compare_reports.py OLD.json NEW.json

Buckets, in the order they matter:

  FIXED           FAIL/ERROR in OLD, PASS in NEW.  This is the detection count
                  — the number a change to the probe suite has to move.
  REGRESSED       the reverse.  Zero is the only acceptable value.
  STILL FAILING   FAIL/ERROR in both.  Real findings the change did not address.
  ONLY IN ONE     a probe present in one report and absent from the other.
                  Almost always a category that crashed and discarded its
                  probes; invisible in the summary counts, which is why S3
                  exists and why this bucket is printed even when empty.
  OTHER CHANGES   any other status transition (PASS->SKIP, SKIP->FAIL, …).

Exit code is 0 unless --require-no-regressions is given and REGRESSED is
non-empty, so it can gate CI without failing an ordinary comparison.
"""

from __future__ import annotations

import argparse
import json
import sys
from typing import Dict, Tuple

FAILING = {"FAIL", "ERROR"}


def load(path: str) -> Tuple[Dict[str, dict], dict]:
    """Index a report's probes by test_name, and hand back its summary."""
    with open(path, encoding="utf-8") as fh:
        report = json.load(fh)

    probes: Dict[str, dict] = {}
    for category in report.get("categories", []):
        for test in category.get("tests", []):
            name = test.get("test_name", "")
            if not name:
                continue
            # A crashed category contributes one synthetic "<name> (DRIVER
            # CRASH)" entry. Keep it: its presence in exactly one report is the
            # single most important thing this tool can report.
            probes[name] = dict(test, category=category.get("name", "?"))
    return probes, report.get("summary", {}) or {}


def rate(summary: dict) -> str:
    if not summary:
        return "no summary"
    passed = summary.get("passed", 0)
    scored = summary.get("scored", summary.get("total_tests", 0))
    pct = (100.0 * passed / scored) if scored else 0.0
    return f"{passed}/{scored} ({pct:.1f}%)"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("old", help="baseline crusher-report.json")
    ap.add_argument("new", help="candidate crusher-report.json")
    ap.add_argument("--require-no-regressions", action="store_true",
                    help="exit 1 if any probe went PASS -> FAIL/ERROR")
    args = ap.parse_args()

    old, old_sum = load(args.old)
    new, new_sum = load(args.new)

    print(f"OLD  {args.old}\n     {len(old)} probes, pass rate {rate(old_sum)}")
    print(f"NEW  {args.new}\n     {len(new)} probes, pass rate {rate(new_sum)}")

    fixed, regressed, still, other = [], [], [], []
    for name, o in old.items():
        n = new.get(name)
        if n is None:
            continue
        a, b = o.get("status"), n.get("status")
        if a == b:
            continue
        if a in FAILING and b == "PASS":
            fixed.append((name, o, a))
        elif a == "PASS" and b in FAILING:
            regressed.append((name, n, b))
        else:
            other.append((name, a, b))

    for name, o in old.items():
        n = new.get(name)
        if n is not None and o.get("status") in FAILING and n.get("status") in FAILING:
            still.append((name, o))

    only_old = [n for n in old if n not in new]
    only_new = [n for n in new if n not in old]

    def head(title: str, count: int) -> None:
        print(f"\n=== {title} ({count}) ===")

    head("FIXED - failing in OLD, passing in NEW", len(fixed))
    for name, o, a in sorted(fixed):
        print(f"  {name:<52} {o['category']:<26} {a} -> PASS")

    head("REGRESSED - passing in OLD, failing in NEW", len(regressed))
    for name, n, b in sorted(regressed):
        print(f"  {name:<52} {n['category']:<26} PASS -> {b}")
        actual = (n.get("actual") or "").strip().replace("\n", " ")
        if actual:
            print(f"      {actual[:160]}")

    head("STILL FAILING in both", len(still))
    for name, o in sorted(still):
        print(f"  {name:<52} {o['category']}")

    head("ONLY IN ONE REPORT", len(only_old) + len(only_new))
    # A category that crashed takes its remaining probes with it, and the
    # summary counts cannot show that — the totals simply differ.
    for name in sorted(only_old):
        print(f"  only in OLD: {name}  ({old[name]['category']})")
    for name in sorted(only_new):
        print(f"  only in NEW: {name}  ({new[name]['category']})")

    head("OTHER STATUS CHANGES", len(other))
    for name, a, b in sorted(other):
        print(f"  {name:<52} {a} -> {b}")

    print(f"\nfixed {len(fixed)}   regressed {len(regressed)}   "
          f"still-failing {len(still)}   only-in-one {len(only_old) + len(only_new)}")

    if args.require_no_regressions and regressed:
        print("\nFAILED: regressions present", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
