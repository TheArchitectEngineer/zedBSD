#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Capture scripted WPT reftests at the first official ready boundary."""

import functools
import gzip
import hashlib
import http.server
import importlib.util
import json
import secrets
import subprocess
import threading
from pathlib import Path
from urllib.parse import quote, urlsplit

from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
WPT_COMMIT = "2d66b9b7998bb58c336138c178323ddee857b586"
WAIT_PATH = "tools/wptrunner/wptrunner/executors/test-wait.js"
FONTS = ROOT / "userland/desktop/fonts"


def load_tool(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


CRASH = load_tool("scripted_wait_protocol", "run-interop2025-crashtests.py")
STATIC = load_tool("scripted_reftest_metadata", "run-wpt-reftests.py")


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, _format, *_args):
        pass

    def do_GET(self):
        # A full encoded query identifies its own adapter and nonce.
        entry = self.server.probes.get(self.path)
        if entry is None:
            return super().do_GET()
        body, mime = entry
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True


def wait_script(wpt):
    source = subprocess.check_output(["git", "-C", str(wpt), "show", WPT_COMMIT + ":" + WAIT_PATH])
    text = source.decode("utf-8")
    if text.count("%(classname)s") != 1 or "</script" in text.lower():
        raise ValueError("unexpected pinned wait script shape")
    formatted = text.replace("%(classname)s", "reftest-wait")
    return formatted, dict(path=WAIT_PATH, commit=WPT_COMMIT,
                          source_sha256=hashlib.sha256(source).hexdigest(),
                          formatted_sha256=hashlib.sha256(formatted.encode()).hexdigest())


def probe(script, nonce, url, xml=False):
    def literal(value):
        return json.dumps(value).replace("<", "\\u003c")

    marker = "ready-probe-" + nonce
    prefix = """(function () {
var nonce=__NONCE__, originalURL=__URL__, marker=__MARKER__;
var node=document.getElementById(marker);
var removed=false;
if (node) {
  node.parentNode.removeChild(node);
  removed=document.getElementById(marker)===null;
}
if (window.top!==window) return;
function failed(error) {
  console.log(__ERROR__+JSON.stringify({nonce:nonce,message:String(error)}));
}
function start() {
  try {
    (function () {
"""
    suffix = """
    }).apply(window,[originalURL,function(reply) {
      console.log(__READY__+JSON.stringify({nonce:nonce,reply:reply,
        ready_state:document.readyState,probe_removed:removed}));
    }]);
  } catch(error) { failed(error); }
}
function afterLoad() { setTimeout(start,0); }
try {
  if (document.readyState==='complete') afterLoad();
  else addEventListener('load',afterLoad,{once:true});
} catch(error) { failed(error); }
}());"""
    for key, value in [("__NONCE__", nonce), ("__URL__", url), ("__MARKER__", marker),
                       ("__ERROR__", CRASH.ERROR_PREFIX), ("__READY__", CRASH.READY_PREFIX)]:
        prefix = prefix.replace(key, literal(value))
        suffix = suffix.replace(key, literal(value))
    code = prefix + script + suffix
    if xml:
        if "]]>" in code:
            raise ValueError("CDATA boundary in adapter")
        body = '<script xmlns="http://www.w3.org/1999/xhtml" id="%s"><![CDATA[%s]]></script>' % (marker, code)
    else:
        body = '<script id="%s">%s</script>' % (marker, code)
    return body.encode("utf-8")


def inject(original, adapter):
    body, position = CRASH.inject_probe(original, adapter)
    assert body[:position] == original[:position]
    assert body[position + len(adapter):] == original[position:]
    return body, position


def expected_message(nonce, url):
    return CRASH.READY_PREFIX + json.dumps(dict(
        nonce=nonce, reply=[url, "complete", []], ready_state="complete", probe_removed=True),
        separators=(",", ":"), ensure_ascii=False)


def capture(program, url, output, nonce, deadline=5000, timeout=20, width=800, height=600):
    command = [str(program), url, str(output), expected_message(nonce, url),
               str(FONTS / "Mahora-Regular.ttf"), str(FONTS / "JetBrainsMono-Regular.ttf"),
               str(FONTS / "DroidSansFallbackFull.ttf"), str(width), str(height), str(deadline)]
    timed_out = False
    try:
        run = subprocess.run(command, capture_output=True, timeout=timeout)
        stdout, stderr, returncode = run.stdout, run.stderr, run.returncode
    except subprocess.TimeoutExpired as error:
        stdout, stderr = error.stdout or b"", error.stderr or b""
        returncode, timed_out = None, True
    stdout = stdout.decode("utf-8", errors="replace")
    stderr = stderr.decode("utf-8", errors="replace")
    ready = CRASH.parse_readiness(stdout + "\n" + stderr, nonce, url)
    if ready["status"] == "ready" and not ready["complete"][0].get("probe_removed"):
        ready["status"] = "adapter_present"
    image = CRASH.image_details(output)
    outcome = CRASH.classify(returncode, timed_out, image is not None, ready)
    return dict(url=url, nonce=nonce, outcome=outcome, readiness=ready, image=image,
                returncode=returncode, timed_out=timed_out, stdout=stdout, stderr=stderr, command=command)


def retain_capture(directory, stem, record):
    for stream in ("stdout", "stderr"):
        (directory / (stem + "." + stream)).write_text(record[stream])
    if record["image"]:
        source = Path(record["image"]["file"])
        with Image.open(source) as image:
            rgb = image.convert("RGB")
            pixels = rgb.tobytes()
            target = directory / (stem + ".png")
            rgb.save(target)
        with Image.open(target) as image:
            if image.convert("RGB").tobytes() != pixels:
                raise ValueError("lossless image read-back differs")
        record["png"] = dict(file=str(target), sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
                             rgb_sha256=hashlib.sha256(pixels).hexdigest())
    (directory / (stem + ".json")).write_text(json.dumps(record, indent=2) + "\n")
