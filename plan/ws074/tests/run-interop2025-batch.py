#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Run at most one selected plain HTTP testharness document per focus area."""

import argparse
from collections import Counter, defaultdict
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
from urllib.parse import urlsplit


ROOT = Path(__file__).resolve().parents[3]
RUNNER_PATH = Path(__file__).with_name("run-interop2025-harness.py")
DOCUMENT_MARKERS = ("test_driver", "testdriver.js", "get_host_info", "{{", "new Worker(",
                    "serviceWorker")
PATH_MARKERS = (".worker.", ".sharedworker.", ".serviceworker.", ".any.",
                ".sub.", ".testdriver.")


def load_runner():
    spec = importlib.util.spec_from_file_location("interop_harness", RUNNER_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def candidate_reason(row, wpt):
    if row["type"] != "testharness":
        return "other_type"
    identifier = row["id"]
    address = urlsplit(identifier)
    test_path = address.path
    if address.scheme or address.netloc or not test_path.startswith("/"):
        return "unsupported_origin"
    if ".https." in test_path:
        return "https"
    if not test_path.endswith(".html"):
        return "not_html"
    if row["source"] != test_path.lstrip("/"):
        runner = load_runner()
        if runner.generated_source(row) is not None:
            if (wpt / row["source"]).is_file():
                return "candidate"
            return "source_absent"
        return "generated_context"
    if any(marker in test_path for marker in PATH_MARKERS):
        return "worker_or_substitution_path"
    source = wpt / row["source"]
    if not source.is_file():
        return "source_absent"
    body = source.read_text(encoding="utf-8", errors="replace")
    if any(marker in body for marker in DOCUMENT_MARKERS):
        return "requires_driver_origin_or_worker"
    return "candidate"


def choose(selection, wpt):
    """Pick the first eligible ID for each area, running overlap only once."""
    areas = sorted(selection["focus_counts"])
    selected = []
    reasons = {area: Counter() for area in areas}
    chosen = {}
    for row in sorted(selection["selected"], key=lambda item: item["id"]):
        reason = candidate_reason(row, wpt)
        for area in row["focus"]:
            if area not in reasons:
                raise ValueError("unrecognized focus area " + area)
            reasons[area][reason] += 1
            if reason == "candidate" and area not in chosen:
                chosen[area] = row["id"]
                if row["id"] not in selected:
                    selected.append(row["id"])
    return selected, chosen, {name: dict(sorted(count.items()))
                              for name, count in reasons.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wpt", type=Path, default=ROOT / "build/ws074-suites/wpt")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--program", type=Path,
                        default=ROOT / "build/ws074-host/plain/browser")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--settle-ms", type=int, default=5000)
    parser.add_argument("--process-timeout", type=int, default=20)
    args = parser.parse_args()
    if args.settle_ms < 0 or args.settle_ms > 60000 or args.process_timeout < 1:
        parser.error("invalid time budget")
    wpt = args.wpt.resolve()
    selection = json.loads(args.manifest.read_text(encoding="utf-8"))
    ids, chosen, reasons = choose(selection, wpt)
    if len(ids) > len(selection["focus_counts"]):
        raise ValueError("batch exceeds one test per focus area")
    runner = load_runner()
    args.out.mkdir(parents=True, exist_ok=True)
    executed = {}
    for index, identifier in enumerate(ids, start=1):
        output = args.out / ("test-%02d.json" % index)
        single = SimpleNamespace(wpt=wpt, manifest=args.manifest, program=args.program,
                                 test_id=identifier, out=output,
                                 settle_ms=args.settle_ms,
                                 process_timeout=args.process_timeout)
        try:
            runner.run(single)
            result = json.loads(output.read_text(encoding="utf-8"))
        except (OSError, ValueError, subprocess.CalledProcessError) as error:
            result = {"test_id": identifier, "outcome": "runner_error",
                      "error": str(error)}
            output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        executed[identifier] = {"outcome": result["outcome"], "report": output.name}
        print("[%d/%d] %s %s" % (index, len(ids), result["outcome"], identifier))
    focus = {}
    for area in sorted(reasons):
        identifier = chosen.get(area)
        focus[area] = {"test_id": identifier,
                       "outcome": executed[identifier]["outcome"] if identifier else "unrun",
                       "reason_counts": reasons[area]}
    report = {"wpt_commit": selection["wpt_commit"],
              "metadata_commit": selection["metadata_commit"],
              "selection": "lexicographic first eligible ID per focus area, at most one each",
              "execution_count": len(executed), "executed": executed,
              "focus": focus,
              "outcome_counts": dict(sorted(Counter(row["outcome"] for row in executed.values()).items())),
              "limitations": "Single HTTP origin, physical HTML source only; unrun focus areas remain in the fixed manifest."}
    (args.out / "report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(args.out / "report.json")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print("run-interop2025-batch: %s" % error, file=sys.stderr)
        sys.exit(1)
