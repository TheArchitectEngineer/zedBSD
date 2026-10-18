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
import re
import secrets
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from urllib.parse import urlsplit

from PIL import Image


ROOT = Path(__file__).resolve().parents[3]
WPT_COMMIT = "2d66b9b7998bb58c336138c178323ddee857b586"
FONTS = ROOT / "userland/desktop/fonts"
WAIT_PATH = "tools/wptrunner/wptrunner/executors/test-wait.js"
READY_PREFIX = "WPT_CRASH_READY "
ERROR_PREFIX = "WPT_CRASH_WAIT_ERROR "


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, _format, *args):
        del args

    def do_GET(self):
        path = urlsplit(self.path).path
        body = self.server.probes.get(path)
        if body is None:
            return super().do_GET()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class QuietServer(http.server.ThreadingHTTPServer):
    daemon_threads = True


def classify(returncode, timed_out, image_valid, readiness=None):
    """Require the official wait protocol in addition to a living renderer."""
    if timed_out:
        return "timeout"
    if returncode is not None and returncode < 0:
        return "crash_signal"
    if returncode != 0:
        return "process_error"
    if not image_valid:
        return "no_image"
    if readiness is None:
        return "readiness_missing"
    if readiness["status"] != "ready":
        return "readiness_" + readiness["status"]
    return "pass"


def parse_readiness(console, nonce, url):
    """Reject malformed, mixed-page and repeated completion messages."""
    complete = []
    errors = []
    invalid = []
    for line in console.splitlines():
        # Renderer mode labels console callbacks; --run writes their text directly.
        if line.startswith("console: "):
            line = line[len("console: "):]
        prefix = next((item for item in (READY_PREFIX, ERROR_PREFIX)
                       if line.startswith(item)), None)
        if prefix is None:
            continue
        try:
            message = json.loads(line[len(prefix):])
        except json.JSONDecodeError as error:
            invalid.append(str(error))
            continue
        if not isinstance(message, dict) or message.get("nonce") != nonce:
            invalid.append("unexpected message identity")
        elif prefix == ERROR_PREFIX:
            errors.append(message)
        elif (message.get("reply") != [url, "complete", []] or
              message.get("ready_state") != "complete"):
            invalid.append("unexpected completion or load state")
        else:
            complete.append(message)
    status = "ready"
    if invalid:
        status = "invalid"
    elif errors:
        status = "error"
    elif len(complete) != 1:
        status = "missing" if not complete else "multiple"
    return {"status": status, "complete": complete,
            "errors": errors, "invalid": invalid}


def load_wait_script(wpt):
    """Execute the pinned third-party fixture without vendoring its source."""
    source = subprocess.check_output(
        ["git", "-C", str(wpt), "show", WPT_COMMIT + ":" + WAIT_PATH],
    )
    text = source.decode("utf-8")
    if text.count("%(classname)s") != 1 or "</script" in text.lower():
        raise ValueError("unexpected pinned wait script shape")
    formatted = text.replace("%(classname)s", "test-wait")
    return formatted, {"path": WAIT_PATH, "commit": WPT_COMMIT,
                       "source_sha256": hashlib.sha256(source).hexdigest(),
                       "formatted_sha256": hashlib.sha256(formatted.encode()).hexdigest()}


def make_probe(wait_script, nonce, url):
    """Schedule the unmodified official wait after the page's load handlers."""
    def literal(text):
        return json.dumps(text).replace("<", "\\u003c")

    prefix = """<script>(function () {
if (window.top !== window) return;
var nonce = __NONCE__, originalURL = __URL__;
function failed(error) {
  console.log(__ERROR__ + JSON.stringify({nonce:nonce, message:String(error)}));
}
function start() {
  try {
    (function () {
"""
    suffix = """
    }).apply(window, [originalURL, function (reply) {
      console.log(__READY__ + JSON.stringify({nonce:nonce, reply:reply,
                                         ready_state:document.readyState}));
    }]);
  } catch (error) { failed(error); }
}
function afterLoad() { setTimeout(start, 0); }
try {
  if (document.readyState === "complete") afterLoad();
  else addEventListener("load", afterLoad, {once:true});
} catch (error) { failed(error); }
}());</script>"""
    for key, value in [("__NONCE__", nonce), ("__URL__", url),
                       ("__ERROR__", ERROR_PREFIX), ("__READY__", READY_PREFIX)]:
        prefix = prefix.replace(key, literal(value))
        suffix = suffix.replace(key, literal(value))
    return (prefix + wait_script + suffix).encode("utf-8")


def inject_probe(body, probe):
    """Keep original bytes and insert the adapter in the initial head only."""
    script = re.search(br"<script(?:\s|>)", body, re.I)
    boundary = len(body) if script is None else script.start()
    position = 0
    for pattern in (br"<head(?:\s[^>]*)?>", br"<html(?:\s[^>]*)?>",
                    br"<!doctype\b[^>]*>"):
        match = re.search(pattern, body, re.I)
        if match is not None and match.start() <= boundary:
            position = match.end()
            break
    return body[:position] + probe + body[position:], position


def render_case(args, test_id, url, output, nonce):
    """Capture the final paint and native console after the protocol runs."""
    command = [str(args.program), "--render", "--async",
               "--settle-ms=%d" % args.settle_ms,
               "--output=" + str(output), "--width=%d" % args.width,
               "--height=%d" % args.height,
               "--font=" + str(FONTS / "Mahora-Regular.ttf"),
               "--mono-font=" + str(FONTS / "JetBrainsMono-Regular.ttf"),
               "--fallback-font=" + str(FONTS / "DroidSansFallbackFull.ttf"), url]
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
    readiness = parse_readiness(stdout + "\n" + stderr, nonce, url)
    image = image_details(output)
    return {"test_id": test_id,
            "outcome": classify(returncode, timed_out, image is not None, readiness),
            "returncode": returncode, "timed_out": timed_out, "image": image,
            "readiness": readiness, "nonce": nonce, "stdout": stdout,
            "stderr": stderr, "command": command}


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
    return {"file": str(path), "size": size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def retain_case(directory, checkpoint, index, record):
    """Retain one complete record before publishing its progress checkpoint."""
    stem = "case-%03d" % (index + 1)
    for name in ("stdout", "stderr"):
        (directory / (stem + "." + name)).write_text(record.get(name, ""), encoding="utf-8")
    path = directory / (stem + ".json")
    path.write_text(json.dumps(record, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    with checkpoint.open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(record, ensure_ascii=False) + "\n")


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
    wait_script, wait_metadata = load_wait_script(args.wpt)
    server = QuietServer(("127.0.0.1", 0),
                         functools.partial(QuietHandler, directory=str(args.wpt)))
    server.probes = {}
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    results = []
    try:
        raw = Path(tempfile.mkdtemp(prefix="wpt-crash-", dir=args.out.parent)).resolve()
        base = "http://127.0.0.1:%d" % server.server_port
        checkpoint = raw / "progress.jsonl"
        for index, row in enumerate(cases):
            test_id = row["id"]
            record = {"test_id": test_id}
            if ("?" in test_id or ".https." in test_id or
                    not test_id.endswith(".html") or
                    not (args.wpt / row["source"]).is_file()):
                record["outcome"] = "unsupported"
                results.append(record)
                retain_case(raw, checkpoint, index, record)
                continue
            original = (args.wpt / row["source"]).read_bytes()
            pinned = subprocess.check_output(
                ["git", "-C", str(args.wpt), "show", WPT_COMMIT + ":" + row["source"]],
            )
            if original != pinned or test_id != "/" + row["source"]:
                raise ValueError("selected source differs from the pinned plain page")
            nonce = secrets.token_hex(16)
            url = base + test_id
            probe = make_probe(wait_script, nonce, url)
            body, position = inject_probe(original, probe)
            server.probes = {test_id: body}
            output = raw / ("case-%03d.ppm" % (index + 1))
            record = render_case(args, test_id, url, output, nonce)
            record.update({"source_sha256": hashlib.sha256(original).hexdigest(),
                           "instrumented_sha256": hashlib.sha256(body).hexdigest(),
                           "insertion_offset": position})
            results.append(record)
            retain_case(raw, checkpoint, index, record)
            print("%s: %s" % (test_id, record["outcome"]))
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    counts = {}
    for record in results:
        counts[record["outcome"]] = counts.get(record["outcome"], 0) + 1
    library = args.program.resolve().parent / "libbrowser.so"
    library_hash = hashlib.sha256(library.read_bytes()).hexdigest() if library.is_file() else None
    report = {"schema": 2, "pass_semantics": "native_official_wait_and_paint",
              "wait_script": wait_metadata,
              "selected_sha256": hashlib.sha256("\n".join(sorted(row["id"] for row in cases)).encode()).hexdigest(),
              "raw_directory": str(raw), "checkpoint": str(checkpoint),
              "engine_library_sha256": library_hash,
              "wpt_commit": revision,
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
    if (not 0 <= args.settle_ms <= 60000 or args.process_timeout < 1 or
            args.width < 1 or args.height < 1):
        parser.error("invalid time or viewport bound")
    return run(args)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, subprocess.CalledProcessError) as error:
        print("run-interop2025-crashtests: " + str(error), file=sys.stderr)
        sys.exit(1)
