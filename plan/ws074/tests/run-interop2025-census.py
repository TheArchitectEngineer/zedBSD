#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Measure one bounded shard of eligible fixed Interop 2025 testharness IDs."""

import argparse
from collections import Counter
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parents[3]


def load_module(filename, name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def choose(selection, wpt, candidate_reason):
    """Classify every fixed ID and return unique eligible IDs in lexical order."""
    reasons = Counter()
    identifiers = []
    seen = set()
    for row in sorted(selection["selected"], key=lambda entry: entry["id"]):
        identifier = row["id"]
        if identifier in seen:
            raise ValueError("duplicate manifest ID: " + identifier)
        seen.add(identifier)
        reason = candidate_reason(row, wpt)
        reasons[reason] += 1
        if reason == "candidate":
            identifiers.append(identifier)
    return identifiers, dict(sorted(reasons.items()))


def append_checkpoint(path, record):
    with path.open("a", encoding="utf-8") as output:
        output.write(json.dumps(record, ensure_ascii=False, sort_keys=True) + "\n")
        output.flush()
        os.fsync(output.fileno())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wpt", type=Path, default=ROOT / "build/ws074-suites/wpt")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--program", type=Path,
                        default=ROOT / "build/ws074-host/plain/browser")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--offset", type=int, default=0)
    parser.add_argument("--limit", type=int, default=128)
    parser.add_argument("--settle-ms", type=int, default=5000)
    parser.add_argument("--process-timeout", type=int, default=20)
    parser.add_argument("--font-dir", type=Path,
                        help="explicit fixed fonts for pages that require layout")
    parser.add_argument("--gzip-raw", action="store_true",
                        help="preserve raw callback reports as gzip files")
    args = parser.parse_args()
    if (args.offset < 0 or args.limit < 1 or args.limit > 128 or
            args.settle_ms < 0 or args.settle_ms > 60000 or
            args.process_timeout < 1 or args.process_timeout > 90):
        parser.error("invalid shard or process bound")
    wpt = args.wpt.resolve()
    selection = json.loads(args.manifest.read_text(encoding="utf-8"))
    batch = load_module("run-interop2025-batch.py", "interop_batch")
    runner = load_module("run-interop2025-harness.py", "interop_harness")
    runner.font_arguments(args.font_dir)
    revision = subprocess.check_output(["git", "-C", str(wpt), "rev-parse", "HEAD"],
                                       text=True).strip()
    if revision != runner.WPT_COMMIT or selection["wpt_commit"] != revision:
        raise ValueError("WPT or manifest revision differs from fixed input")
    identifiers, reasons = choose(selection, wpt, batch.candidate_reason)
    selected = identifiers[args.offset:args.offset + args.limit]
    if not selected:
        raise ValueError("offset selects no eligible IDs")
    args.out.mkdir(parents=True, exist_ok=False)
    digest = hashlib.sha256("\n".join(identifiers).encode("utf-8")).hexdigest()
    header = {"kind": "header", "wpt_commit": revision,
              "metadata_commit": selection["metadata_commit"],
              "eligible": len(identifiers), "reason_counts": reasons,
              "eligible_sha256": digest, "offset": args.offset,
              "limit": args.limit, "selected": selected,
              "font_dir": str(args.font_dir) if args.font_dir else None}
    checkpoint = args.out / "progress.jsonl"
    append_checkpoint(checkpoint, header)
    results = []
    for index, identifier in enumerate(selected, start=1):
        path = args.out / ("test-%03d.json" % (args.offset + index))
        single = SimpleNamespace(wpt=wpt, manifest=args.manifest, program=args.program,
                                 test_id=identifier, out=path, settle_ms=args.settle_ms,
                                 process_timeout=args.process_timeout,
                                 font_dir=args.font_dir)
        try:
            runner.run(single)
            result = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError, subprocess.CalledProcessError) as error:
            result = {"test_id": identifier, "outcome": "runner_error",
                      "error": str(error)}
            path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        if args.gzip_raw:
            compressed = path.with_suffix(path.suffix + ".gz")
            with path.open("rb") as source, gzip.open(compressed, "wb") as output:
                for block in iter(lambda: source.read(65536), b""):
                    output.write(block)
            path.unlink()
            path = compressed
        row = {"kind": "result", "test_id": identifier,
               "outcome": result["outcome"], "report": path.name}
        results.append(row)
        append_checkpoint(checkpoint, row)
        print("[%d/%d] %s %s" % (index, len(selected), row["outcome"], identifier),
              flush=True)
    report = {"wpt_commit": revision, "metadata_commit": selection["metadata_commit"],
              "eligible": len(identifiers), "reason_counts": reasons,
              "eligible_sha256": digest, "offset": args.offset, "limit": args.limit,
              "font_dir": str(args.font_dir) if args.font_dir else None,
              "selected": selected, "results": results,
              "outcome_counts": dict(sorted(Counter(row["outcome"] for row in results).items()))}
    (args.out / "report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("run-interop2025-census: %s" % error, file=sys.stderr)
        sys.exit(1)
