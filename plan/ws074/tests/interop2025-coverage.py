#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Classify Interop 2025 test IDs against the current static reftest runner."""

import argparse
from collections import Counter, defaultdict
import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
RUNNER = Path(__file__).with_name("run-wpt-reftests.py")


def load_runner():
    spec = importlib.util.spec_from_file_location("ws074_reftest_runner", RUNNER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def classify(row, runner):
    kind = row["type"]
    if kind == "testharness":
        return "unsupported_testharness"
    if kind == "crashtest":
        return "unsupported_crashtest"
    if kind != "reftest":
        return "unsupported_type"
    if "?" in row["id"]:
        return "unsupported_variant_url"
    if ".https." in row["id"]:
        return "unsupported_https_origin"
    path = runner.WPT / row["source"]
    if not path.is_file():
        return "source_absent"
    try:
        text, metadata = runner.read_metadata(path)
        if not metadata.references:
            return "reference_absent"
        if runner.ACTIVE_SCRIPT.search(text):
            return "unsupported_scripted_reftest"
        if metadata.flags.intersection({"animated", "interact", "paged", "speech"}):
            return "unsupported_non_static_flag"
        for _relation, href in metadata.references:
            resolved = runner.resolve_reference(path, href)
            if resolved is None:
                return "unsupported_external_reference"
            reference_path, _reference_url = resolved
            if not reference_path.is_file():
                return "reference_file_absent"
            reference_text = reference_path.read_text(encoding="utf-8", errors="replace")
            if runner.ACTIVE_SCRIPT.search(reference_text):
                return "unsupported_scripted_reference"
    except (OSError, ValueError, UnicodeError):
        return "metadata_error"
    return "static_reftest_candidate"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    selection = json.loads(args.manifest.read_text(encoding="utf-8"))
    runner = load_runner()
    runner.verify_suite()
    results = []
    focus_counts = defaultdict(Counter)
    for row in selection["selected"]:
        reason = classify(row, runner)
        results.append({"id": row["id"], "focus": row["focus"], "type": row["type"],
                        "runner_class": reason})
        for name in row["focus"]:
            focus_counts[name][reason] += 1
    counts = Counter(row["runner_class"] for row in results)
    report = {
        "wpt_commit": selection["wpt_commit"],
        "metadata_commit": selection["metadata_commit"],
        "selected_count": len(results),
        "runner_classes": dict(sorted(counts.items())),
        "focus_classes": {name: dict(sorted(count.items()))
                          for name, count in sorted(focus_counts.items())},
        "results": results,
        "limitations": [
            "Static candidate is not a PASS or a guarantee of WPT infrastructure parity.",
            "Current runner serves one HTTP origin and does not implement official WPT testharness, crashtest or HTTPS semantics.",
        ],
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n",
                        encoding="utf-8")
    print("Interop 2025 runner inventory: %d selected" % len(results))
    for name, count in sorted(counts.items()):
        print("%s: %d" % (name, count))


if __name__ == "__main__":
    main()
