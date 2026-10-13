#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Run a bounded deterministic shard of selected plain HTTP WPT harnesses."""

import argparse
from collections import Counter, defaultdict
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parents[3]


def load_module(filename, name):
    path = Path(__file__).with_name(filename)
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def choose(selection, wpt, candidate_reason, per_focus, max_tests):
    """Select each area's first eligible IDs, sharing overlaps only once."""
    areas = sorted(selection["focus_counts"])
    candidates = defaultdict(list)
    reasons = {area: Counter() for area in areas}
    for row in sorted(selection["selected"], key=lambda entry: entry["id"]):
        reason = candidate_reason(row, wpt)
        for area in row["focus"]:
            if area not in reasons:
                raise ValueError("unknown focus area " + area)
            reasons[area][reason] += 1
            if reason == "candidate":
                candidates[area].append(row["id"])
    selected = []
    seen = set()
    area_ids = {area: [] for area in areas}
    for position in range(per_focus):
        for area in areas:
            if position >= len(candidates[area]):
                continue
            identifier = candidates[area][position]
            area_ids[area].append(identifier)
            if identifier not in seen and len(selected) < max_tests:
                seen.add(identifier)
                selected.append(identifier)
    if len(selected) > max_tests:
        raise ValueError("shard exceeds its bound")
    area_ids = {area: [identifier for identifier in ids if identifier in seen]
                for area, ids in area_ids.items()}
    return selected, area_ids, {area: dict(sorted(count.items()))
                                for area, count in reasons.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wpt", type=Path, default=ROOT / "build/ws074-suites/wpt")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--program", type=Path,
                        default=ROOT / "build/ws074-host/plain/browser")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--per-focus", type=int, default=8)
    parser.add_argument("--max-tests", type=int, default=128)
    parser.add_argument("--settle-ms", type=int, default=5000)
    parser.add_argument("--process-timeout", type=int, default=20)
    args = parser.parse_args()
    if (args.per_focus < 1 or args.per_focus > 128 or
            args.max_tests < 1 or args.max_tests > 128 or
            args.settle_ms < 0 or args.settle_ms > 60000 or
            args.process_timeout < 1 or args.process_timeout > 90):
        parser.error("invalid shard or process bound")
    wpt = args.wpt.resolve()
    selection = json.loads(args.manifest.read_text(encoding="utf-8"))
    batch = load_module("run-interop2025-batch.py", "interop_batch")
    runner = load_module("run-interop2025-harness.py", "interop_harness")
    revision = subprocess.check_output(["git", "-C", str(wpt), "rev-parse", "HEAD"],
                                       text=True).strip()
    if revision != runner.WPT_COMMIT or selection["wpt_commit"] != revision:
        raise ValueError("WPT or manifest revision differs from fixed input")
    identifiers, area_ids, reasons = choose(selection, wpt, batch.candidate_reason,
                                            args.per_focus, args.max_tests)
    args.out.mkdir(parents=True, exist_ok=True)
    results = []
    for index, identifier in enumerate(identifiers, start=1):
        path = args.out / ("test-%03d.json" % index)
        single = SimpleNamespace(wpt=wpt, manifest=args.manifest, program=args.program,
                                 test_id=identifier, out=path, settle_ms=args.settle_ms,
                                 process_timeout=args.process_timeout)
        try:
            runner.run(single)
            result = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError, subprocess.CalledProcessError) as error:
            result = {"test_id": identifier, "outcome": "runner_error", "error": str(error)}
            path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        results.append({"test_id": identifier, "outcome": result["outcome"],
                        "report": path.name})
        print("[%d/%d] %s %s" % (index, len(identifiers), result["outcome"], identifier),
              flush=True)
    report = {"wpt_commit": revision, "metadata_commit": selection["metadata_commit"],
              "selection": {"per_focus": args.per_focus, "max_tests": args.max_tests,
                            "area_ids": area_ids, "reason_counts": reasons},
              "execution_count": len(results), "results": results,
              "outcome_counts": dict(sorted(Counter(row["outcome"] for row in results).items()))}
    (args.out / "report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(args.out / "report.json")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("run-interop2025-shard: %s" % error, file=sys.stderr)
        sys.exit(1)
