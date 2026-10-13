#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Run selected plain-HTTP Interop 2025 crashtests through the renderer."""

import argparse
import functools
import hashlib
import http.server
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

from PIL import Image


ROOT = Path(__file__).resolve().parents[3]
WPT_COMMIT = "2d66b9b7998bb58c336138c178323ddee857b586"
FONTS = ROOT / "userland/desktop/fonts"


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, _format, *args):
        del args


class QuietServer(http.server.ThreadingHTTPServer):
    daemon_threads = True


def classify(returncode, timed_out, image_valid):
    """Keep a renderer crash distinct from other unsuccessful outcomes."""
    if timed_out:
        return "timeout"
    if returncode is not None and returncode < 0:
        return "crash_signal"
    if returncode != 0:
        return "process_error"
    if not image_valid:
        return "no_image"
    return "pass"


def image_details(path):
    if not path.is_file():
        return None
    try:
        with Image.open(path) as image:
            size = list(image.size)
            image.verify()
        if size[0] == 0 or size[1] == 0:
            return None
    except (OSError, ValueError):
        return None
    return {"size": size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def selected_cases(manifest, focus):
    rows = [row for row in manifest["selected"]
            if row["type"] == "crashtest" and
            (focus == "all" or focus in row["focus"])]
    ids = [row["id"] for row in rows]
    if len(ids) != len(set(ids)):
        raise ValueError("duplicate selected crashtest ID")
    return rows


def run(args):
    revision = subprocess.check_output(
        ["git", "-C", str(args.wpt), "rev-parse", "HEAD"], text=True,
    ).strip()
    if revision != WPT_COMMIT:
        raise ValueError("WPT revision differs from the fixed input")
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    if manifest.get("wpt_commit") != WPT_COMMIT:
        raise ValueError("selection manifest has a different WPT revision")
    if not args.program.is_file():
        raise ValueError("browser program is absent")
    cases = selected_cases(manifest, args.focus)
    if not cases:
        raise ValueError("no crashtests selected for focus: " + args.focus)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    server = QuietServer(("127.0.0.1", 0),
                         functools.partial(QuietHandler, directory=str(args.wpt)))
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    results = []
    try:
        with tempfile.TemporaryDirectory(prefix="wpt-crash-", dir=args.out.parent) as raw:
            output = Path(raw) / "result.ppm"
            base = "http://127.0.0.1:%d" % server.server_port
            for row in cases:
                test_id = row["id"]
                record = {"test_id": test_id}
                if ("?" in test_id or ".https." in test_id or
                        not test_id.endswith(".html") or
                        not (args.wpt / row["source"]).is_file()):
                    record["outcome"] = "unsupported"
                    results.append(record)
                    continue
                output.unlink(missing_ok=True)
                command = [str(args.program), "--render", "--async",
                           "--settle-ms=%d" % args.settle_ms,
                           "--output=" + str(output), "--width=%d" % args.width,
                           "--height=%d" % args.height,
                           "--font=" + str(FONTS / "Mahora-Regular.ttf"),
                           "--mono-font=" + str(FONTS / "JetBrainsMono-Regular.ttf"),
                           "--fallback-font=" + str(FONTS / "DroidSansFallbackFull.ttf"),
                           base + test_id]
                try:
                    process = subprocess.run(command, capture_output=True,
                                             timeout=args.process_timeout)
                    stdout = process.stdout.decode("utf-8", errors="replace")
                    stderr = process.stderr.decode("utf-8", errors="replace")
                    returncode = process.returncode
                    timed_out = False
                except subprocess.TimeoutExpired as error:
                    stdout = (error.stdout or b"").decode("utf-8", errors="replace")
                    stderr = (error.stderr or b"").decode("utf-8", errors="replace")
                    returncode = None
                    timed_out = True
                image = image_details(output)
                record.update({"outcome": classify(returncode, timed_out, image is not None),
                               "returncode": returncode, "timed_out": timed_out,
                               "image": image, "stdout": stdout, "stderr": stderr,
                               "command": command})
                results.append(record)
                print("%s: %s" % (test_id, record["outcome"]))
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    counts = {}
    for record in results:
        counts[record["outcome"]] = counts.get(record["outcome"], 0) + 1
    report = {"schema": 1, "wpt_commit": revision,
              "metadata_commit": manifest["metadata_commit"],
              "focus": args.focus, "viewport": [args.width, args.height],
              "program_sha256": hashlib.sha256(args.program.read_bytes()).hexdigest(),
              "settle_ms": args.settle_ms,
              "process_timeout": args.process_timeout, "counts": counts,
              "results": results}
    args.out.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n",
                        encoding="utf-8")
    print(args.out)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wpt", type=Path, default=ROOT / "build/ws074-suites/wpt")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--program", type=Path,
                        default=ROOT / "build/ws074-host/plain/browser")
    parser.add_argument("--focus", default="details")
    parser.add_argument("--width", type=int, default=800)
    parser.add_argument("--height", type=int, default=600)
    parser.add_argument("--settle-ms", type=int, default=3000)
    parser.add_argument("--process-timeout", type=int, default=60)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, subprocess.CalledProcessError) as error:
        print("run-interop2025-crashtests: " + str(error), file=sys.stderr)
        sys.exit(1)
