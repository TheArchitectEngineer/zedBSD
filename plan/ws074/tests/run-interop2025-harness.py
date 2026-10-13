#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Capture one selected WPT testharness page through browser --run.

This first stage serves one HTTP origin. It preserves WPT's files and appends
result callbacks only to the response for testharnessreport.js. Unsupported
environment requirements must be reported as unrun by later coverage work.
"""

import argparse
import functools
import hashlib
import http.server
import json
from pathlib import Path
import subprocess
import sys
import threading


ROOT = Path(__file__).resolve().parents[3]
WPT_COMMIT = "2d66b9b7998bb58c336138c178323ddee857b586"
RESULT_PREFIX = "WS074_WPT_RESULT "
COMPLETE_PREFIX = "WS074_WPT_COMPLETE "
REPORT_SCRIPT = r"""
(function () {
    function test_result(test) {
        return {name: String(test.name), status: test.status,
                message: test.message ? String(test.message) : ""};
    }
    add_result_callback(function (test) {
        console.log("WS074_WPT_RESULT " + JSON.stringify(test_result(test)));
    });
    add_completion_callback(function (tests, harness_status) {
        var results = [];
        for (var i = 0; i < tests.length; i++) {
            results.push(test_result(tests[i]));
        }
        console.log("WS074_WPT_COMPLETE " + JSON.stringify({
            tests: results,
            status: harness_status.status,
            message: harness_status.message ? String(harness_status.message) : ""
        }));
    });
}());
"""
TEST_STATUSES = {0: "PASS", 1: "FAIL", 2: "TIMEOUT", 3: "NOTRUN",
                 4: "PRECONDITION_FAILED"}
HARNESS_STATUSES = {0: "OK", 1: "ERROR", 2: "TIMEOUT",
                    3: "PRECONDITION_FAILED"}
FONT_NAMES = ("Mahora-Regular.ttf", "JetBrainsMono-Regular.ttf",
              "DroidSansFallbackFull.ttf")


def font_arguments(directory):
    """Pass a complete explicit font set to pages that need layout."""
    if directory is None:
        return []
    paths = [directory / name for name in FONT_NAMES]
    if not all(path.is_file() for path in paths):
        raise ValueError("fixed font directory is missing a required font")
    return ["--font=" + str(paths[0]), "--mono-font=" + str(paths[1]),
            "--fallback-font=" + str(paths[2])]


class WptHandler(http.server.SimpleHTTPRequestHandler):
    """Serve the pinned WPT tree, adding only harness reporting callbacks."""

    def log_message(self, _format, *args):
        del args

    def do_GET(self):
        if self.path.split("?", 1)[0] != "/resources/testharnessreport.js":
            return super().do_GET()
        original = (Path(self.directory) / "resources/testharnessreport.js").read_bytes()
        body = original + b"\n" + REPORT_SCRIPT.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/javascript; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class WptServer(http.server.ThreadingHTTPServer):
    daemon_threads = True


def classify_output(stdout, returncode, timed_out=False):
    """Separate transport failures, missing completion and WPT test results."""
    results = []
    complete = []
    errors = []
    for line in stdout.splitlines():
        try:
            if line.startswith(RESULT_PREFIX):
                results.append(json.loads(line[len(RESULT_PREFIX):]))
            elif line.startswith(COMPLETE_PREFIX):
                complete.append(json.loads(line[len(COMPLETE_PREFIX):]))
        except json.JSONDecodeError as error:
            errors.append(str(error))
    if timed_out:
        outcome = "process_timeout"
    elif returncode != 0:
        outcome = "process_error"
    elif errors:
        outcome = "invalid_result_json"
    elif not stdout.strip():
        outcome = "no_output"
    elif len(complete) != 1:
        outcome = "no_completion" if not complete else "multiple_completions"
    else:
        status = complete[0].get("status")
        tests = complete[0].get("tests")
        if status not in HARNESS_STATUSES or not isinstance(tests, list):
            outcome = "invalid_completion"
        elif not all(isinstance(test, dict) for test in tests):
            outcome = "invalid_test_result"
        elif status == 2 or any(test.get("status") == 2 for test in tests):
            outcome = "timeout"
        elif status != 0:
            outcome = "harness_error"
        elif not tests:
            outcome = "zero_tests"
        elif any(test.get("status") not in TEST_STATUSES for test in tests):
            outcome = "invalid_test_status"
        elif any(test["status"] != 0 for test in tests):
            outcome = "fail"
        else:
            outcome = "pass"
    return {"outcome": outcome, "result_callbacks": results,
            "completion": complete[0] if len(complete) == 1 else None,
            "json_errors": errors}


def run(args):
    wpt = args.wpt.resolve()
    revision = subprocess.check_output(
        ["git", "-C", str(wpt), "rev-parse", "HEAD"], text=True,
    ).strip()
    if revision != WPT_COMMIT:
        raise ValueError("WPT revision differs from the fixed input")
    selected = json.loads(args.manifest.read_text(encoding="utf-8"))
    if selected.get("wpt_commit") != WPT_COMMIT:
        raise ValueError("selection manifest has a different WPT revision")
    matches = [row for row in selected["selected"] if row["id"] == args.test_id]
    if len(matches) != 1 or matches[0]["type"] != "testharness":
        raise ValueError("test ID is not a single selected testharness case")
    if ("?" in args.test_id or ".https." in args.test_id or
            not args.test_id.endswith(".html")):
        raise ValueError("this first-stage runner supports only plain HTTP HTML IDs")
    if not (wpt / matches[0]["source"]).is_file():
        raise ValueError("selected WPT source is absent")
    if not args.program.is_file():
        raise ValueError("browser program is absent")
    server = WptServer(("127.0.0.1", 0), functools.partial(WptHandler, directory=str(wpt)))
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    url = "http://127.0.0.1:%d%s" % (server.server_port, args.test_id)
    command = ([str(args.program), "--run", "--settle-ms=%d" % args.settle_ms] +
               font_arguments(getattr(args, "font_dir", None)) + [url])
    try:
        try:
            process = subprocess.run(command, capture_output=True, text=True,
                                     errors="replace", timeout=args.process_timeout)
            stdout, stderr, returncode, timed_out = (
                process.stdout, process.stderr, process.returncode, False)
        except subprocess.TimeoutExpired as error:
            stdout = (error.stdout or b"").decode("utf-8", errors="replace")
            stderr = (error.stderr or b"").decode("utf-8", errors="replace")
            returncode, timed_out = None, True
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    result = classify_output(stdout, returncode, timed_out)
    result.update({"test_id": args.test_id, "focus": matches[0]["focus"],
                   "wpt_commit": revision, "metadata_commit": selected["metadata_commit"],
                   "program_sha256": hashlib.sha256(args.program.read_bytes()).hexdigest(),
                   "command": command, "returncode": returncode,
                   "stdout": stdout, "stderr": stderr,
                   "settle_ms": args.settle_ms, "process_timeout": args.process_timeout})
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n",
                        encoding="utf-8")
    print("%s: %s (%s)" % (args.test_id, result["outcome"], args.out))
    return 0 if result["outcome"] == "pass" else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wpt", type=Path, default=ROOT / "build/ws074-suites/wpt")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--program", type=Path,
                        default=ROOT / "build/ws074-host/plain/browser")
    parser.add_argument("--test-id", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--settle-ms", type=int, default=5000)
    parser.add_argument("--process-timeout", type=int, default=20)
    parser.add_argument("--font-dir", type=Path,
                        help="directory with the three fixed host fonts for layout")
    args = parser.parse_args()
    if args.settle_ms < 0 or args.settle_ms > 60000 or args.process_timeout < 1:
        parser.error("invalid time budget")
    try:
        return run(args)
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        print("run-interop2025-harness: %s" % error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
