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
import re
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
        if hasattr(self.server, "requests"):
            self.server.requests.append(self.path)
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


def capture_boundary(console, deadline):
    markers = []
    for line in console.splitlines():
        if line.startswith("READY_CAPTURE "):
            markers.append(line)
    if len(markers) != 1:
        return dict(valid=False, markers=markers)
    match = re.fullmatch(r"READY_CAPTURE budget=(\d+) acknowledged=(\d+)", markers[0])
    if match is None:
        return dict(valid=False, markers=markers)
    budget, acknowledged = map(int, match.groups())
    return dict(valid=acknowledged == 1 and 0 <= budget <= deadline,
                budget=budget, acknowledged=acknowledged, markers=markers)


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
    output.with_suffix(".stdout.bin").write_bytes(stdout)
    output.with_suffix(".stderr.bin").write_bytes(stderr)
    stdout = stdout.decode("utf-8", errors="replace")
    stderr = stderr.decode("utf-8", errors="replace")
    ready = CRASH.parse_readiness(stdout + "\n" + stderr, nonce, url)
    if ready["status"] == "ready" and ready["complete"][0].get("probe_removed") is not True:
        ready["status"] = "adapter_present"
    image = CRASH.image_details(output)
    if image is not None:
        try:
            with Image.open(output) as decoded:
                decoded.load()
        except (OSError, ValueError):
            image = None
    outcome = CRASH.classify(returncode, timed_out, image is not None, ready)
    boundary = capture_boundary(stderr, deadline)
    if outcome == "pass" and not boundary["valid"]:
        outcome = "capture_boundary_invalid"
    if outcome == "pass" and image["size"] != [width, height]:
        outcome = "capture_size_invalid"
    return dict(url=url, nonce=nonce, outcome=outcome, readiness=ready, image=image,
                returncode=returncode, timed_out=timed_out, stdout=stdout, stderr=stderr, command=command,
                capture_boundary=boundary)


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


class Unsupported(RuntimeError):
    pass


class CaptureFailure(RuntimeError):
    def __init__(self, outcome, resource):
        super().__init__(outcome + ": " + resource)
        self.outcome = outcome


def resolve_id(current, href):
    from urllib.parse import urljoin
    resolved = urlsplit(urljoin("http://wpt.invalid" + current, href))
    if resolved.scheme != "http" or resolved.netloc != "wpt.invalid":
        raise Unsupported("external reference")
    value = resolved.path
    if resolved.query:
        value += "?" + resolved.query
    if resolved.fragment:
        value += "#" + resolved.fragment
    return value


def fuzzy_ranges(values, current, reference, references, relation=None):
    # Manifest metadata selects an exact reference key before its unkeyed default.
    parsed = {}
    for content in values:
        key = None
        if ":" in content:
            prefix, content = content.rsplit(":", 1)
            key = resolve_id(current, prefix)
            candidates = [kind for kind, resource in references if resource == key]
            if not candidates:
                raise Unsupported("fuzzy key is not a declared reference")
            key = (key, candidates[0])
        if key in parsed:
            raise Unsupported("duplicate fuzzy key")
        fields = content.split(";")
        if len(fields) != 2:
            raise Unsupported("fuzzy metadata needs two ranges")
        named, positional = {}, []
        for field in fields:
            name = None
            if "=" in field:
                name, field = [part.strip() for part in field.split("=", 1)]
                if name not in ("maxDifference", "totalPixels") or name in named:
                    raise Unsupported("invalid fuzzy field")
            match = re.fullmatch(r"\s*(\d+)\s*(?:-\s*(\d+)\s*)?", field)
            if match is None:
                raise Unsupported("invalid fuzzy range")
            lower = int(match.group(1))
            upper = int(match.group(2) or match.group(1))
            if lower > upper:
                raise Unsupported("reversed fuzzy range")
            if name:
                named[name] = (lower, upper)
            else:
                positional.append((lower, upper))
        ranges = []
        for name in ("maxDifference", "totalPixels"):
            if name in named:
                ranges.append(named.pop(name))
            elif positional:
                ranges.append(positional.pop(0))
            else:
                raise Unsupported("missing fuzzy field")
        if named or positional:
            raise Unsupported("unconsumed fuzzy field")
        parsed[key] = ranges
    if relation is None:
        relation = next((kind for kind, resource in references if resource == reference), None)
    return parsed.get((reference, relation), parsed.get(None))


def compare_images(left, right, fuzzy):
    from PIL import ImageChops, ImageStat
    if left.size != right.size:
        raise Unsupported("capture dimensions differ")
    diff = ImageChops.difference(left.convert("RGB"), right.convert("RGB"))
    box = diff.getbbox()
    if box is None:
        maximum, pixels = 0, 0
    else:
        # The pinned executor measures its luminance mask with ImageStat.
        cropped = diff.crop(box)
        stat = ImageStat.Stat(cropped, cropped.convert("L", dither=None))
        maximum = max(high for _low, high in stat.extrema)
        pixels = stat.count[0]
    if fuzzy is None or fuzzy == [(0, 0), (0, 0)]:
        equal = pixels == 0 and maximum == 0
    else:
        channel, count = fuzzy
        equal = ((pixels == 0 and count[0] == 0) or
                 (maximum == 0 and channel[0] == 0) or
                 (channel[0] <= maximum <= channel[1] and count[0] <= pixels <= count[1]))
    return equal, dict(max_difference=maximum, different_pixels=pixels, fuzzy=fuzzy)


def evaluate_graph(root, get_node, get_capture, max_depth=16):
    edges = []

    def descend(current, ancestry):
        if len(ancestry) > max_depth:
            raise Unsupported("reference depth bound")
        node = get_node(current)
        left = get_capture(current)
        if left["outcome"] != "pass":
            raise CaptureFailure(left["outcome"], current)
        if not node["references"]:
            return True
        for relation, reference in node["references"]:
            right = get_capture(reference)
            if right["outcome"] != "pass":
                raise CaptureFailure(right["outcome"], reference)
            fuzzy = fuzzy_ranges(node["fuzzy"], current, reference, node["references"], relation)
            with Image.open(left["image"]["file"]) as a, Image.open(right["image"]["file"]) as b:
                equal, metrics = compare_images(a, b, fuzzy)
            passed = equal if relation == "match" else not equal
            edge = dict(source=current, reference=reference, relation=relation,
                        equal=equal, passed=passed, **metrics)
            edges.append(edge)
            if not passed:
                continue
            if reference in ancestry:
                raise Unsupported("reference cycle")
            if descend(reference, ancestry + [reference]):
                return True
        return False

    if not get_node(root)["references"]:
        raise Unsupported("selected reftest has no reference")
    try:
        passed = descend(root, [root])
    except (Unsupported, CaptureFailure) as error:
        error.edges = edges
        raise
    return passed, edges


def resource_path(wpt, resource):
    from urllib.parse import unquote
    path = (wpt / unquote(urlsplit(resource).path).lstrip("/")).resolve()
    try:
        path.relative_to(wpt)
    except ValueError as error:
        raise Unsupported("resource leaves WPT root") from error
    return path


def request_limits(wpt, resources):
    limits = []
    for resource in resources:
        path = resource_path(wpt, resource)
        if path.name in ("testdriver.js", "testdriver-vendor.js"):
            limits.append([resource, "testdriver_remote_actions"])
        if ".sub." in path.name or path.suffix in (".py", ".asis"):
            limits.append([resource, "generated_server_resource"])
        ancestors = [path.with_name(path.name + ".headers")]
        parent = path.parent
        while parent == wpt or wpt in parent.parents:
            ancestors.append(parent / "__dir__.headers")
            if parent == wpt:
                break
            parent = parent.parent
        if any(item.is_file() for item in ancestors):
            limits.append([resource, "unimplemented_response_headers"])
    return limits


def run(args):
    import time
    from urllib.parse import urlunsplit
    wpt = args.wpt.resolve()
    STATIC.WPT = wpt
    runner_source = Path(__file__).read_bytes()
    runner_digest = hashlib.sha256(runner_source).hexdigest()
    revision = subprocess.check_output(["git", "-C", str(wpt), "rev-parse", "HEAD"], text=True).strip()
    manifest = json.loads(args.manifest.read_text())
    if revision != WPT_COMMIT or manifest["wpt_commit"] != WPT_COMMIT:
        raise ValueError("fixed WPT revision differs")
    static, _counts, omitted = STATIC.discover(".", manifest["selected"])
    omitted_ids = {item["id"] for item in omitted}
    rows = [item for item in manifest["selected"] if item["id"] in omitted_ids]
    static_ids = {"/" + item["path"] for item in static}
    all_ids = {item["id"] for item in manifest["selected"] if item["type"] == "reftest"}
    if (len(rows) != 413 or len(static_ids) != 1561 or static_ids & omitted_ids or
            static_ids | omitted_ids != all_ids or len(all_ids) != 1974):
        raise ValueError("fixed scripted/static partition differs")
    args.out.mkdir(parents=True, exist_ok=True)
    raw = Path(__import__("tempfile").mkdtemp(prefix="retained-", dir=args.out)).resolve()
    (raw / "runner-source.py").write_bytes(runner_source)
    script, wait_metadata = wait_script(wpt)
    server = Server(("127.0.0.1", 0), functools.partial(Handler, directory=str(wpt)))
    server.probes, server.requests = {}, []
    base = "http://127.0.0.1:%d" % server.server_port
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    nodes, results, capture_records = {}, [], []
    progress = raw / "progress.jsonl"
    started = time.monotonic()

    def checkpoint(record):
        with progress.open("a") as stream:
            stream.write(json.dumps(record) + "\n")
            stream.flush()
            __import__("os").fsync(stream.fileno())

    def get_node(resource):
        if resource in nodes:
            return nodes[resource]
        path = resource_path(wpt, resource)
        if path.suffix.lower() not in (".html", ".htm", ".xht", ".xhtml"):
            raise Unsupported("unimplemented reference document format")
        original = path.read_bytes()
        relative = path.relative_to(wpt).as_posix()
        pinned = subprocess.check_output(["git", "-C", str(wpt), "show", WPT_COMMIT + ":" + relative])
        if original != pinned:
            raise ValueError("resource differs from pinned bytes: " + relative)
        metadata = STATIC.ReftestMetadata()
        metadata.feed(original.decode("utf-8", errors="replace"))
        if metadata.flags.intersection({"animated", "interact", "paged", "speech"}):
            raise Unsupported("unimplemented reference flag")
        node = dict(path=path, original=original, fuzzy=metadata.fuzzy,
                    references=[(relation, resolve_id(resource, href)) for relation, href in metadata.references])
        nodes[resource] = node
        return node

    try:
        checkpoint(dict(kind="header", selected=413, static=1561, union=1974,
                        selected_sha256=hashlib.sha256("\n".join(sorted(omitted_ids)).encode()).hexdigest()))
        for row in rows:
            if time.monotonic() - started >= args.budget_seconds:
                break
            checkpoint(dict(kind="start", test_id=row["id"]))
            cached = {}

            def get_capture(resource):
                if resource in cached:
                    return cached[resource]
                node = get_node(resource)
                wire = quote(resource, safe="/?=&%")
                url = base + wire
                parsed = urlsplit(wire)
                request = urlunsplit(("", "", parsed.path, parsed.query, ""))
                nonce = secrets.token_hex(16)
                adapter = probe(script, nonce, url, xml=node["path"].suffix.lower() in (".xht", ".xhtml"))
                body, position = inject(node["original"], adapter)
                stem = "capture-%05d" % (len(capture_records) + 1)
                (raw / (stem + ".instrumented")).write_bytes(body)
                server.probes = {request: (body, "application/xhtml+xml" if node["path"].suffix.lower() in (".xht", ".xhtml") else "text/html")}
                first_request = len(server.requests)
                record = capture(args.program, url, raw / (stem + ".ppm"), nonce,
                                 deadline=args.deadline_ms, timeout=args.process_timeout)
                requests = server.requests[first_request:]
                limits = request_limits(wpt, requests)
                if ".https." in node["path"].name:
                    limits.append([resource, "https_environment"])
                if limits and record["outcome"] == "pass":
                    record["outcome"] = "unsupported_environment"
                record.update(resource=resource, record=stem + ".json", requests=requests, unsupported=limits,
                              source_sha256=hashlib.sha256(node["original"]).hexdigest(),
                              instrumented_sha256=hashlib.sha256(body).hexdigest(), insertion_offset=position)
                retain_capture(raw, stem, record)
                capture_records.append(dict(resource=resource, record=stem + ".json", outcome=record["outcome"]))
                cached[resource] = record
                return record

            record = dict(test_id=row["id"], focus=row["focus"], captures=[])
            try:
                passed, edges = evaluate_graph(row["id"], get_node, get_capture)
                record.update(outcome="pass" if passed else "fail", edges=edges)
            except CaptureFailure as error:
                record.update(outcome=error.outcome, error=str(error), edges=getattr(error, "edges", []))
            except (Unsupported, FileNotFoundError) as error:
                record.update(outcome="unsupported", error=str(error), edges=getattr(error, "edges", []))
            record["captures"] = [dict(resource=key, outcome=value["outcome"],
                                        record=value["record"]) for key, value in cached.items()]
            results.append(record)
            checkpoint(dict(kind="result", record=record))
            print("[%d/413] %s %s" % (len(results), record["outcome"], record["test_id"]), flush=True)
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    counts = {}
    for record in results:
        counts[record["outcome"]] = counts.get(record["outcome"], 0) + 1
    report = dict(schema=1, pass_semantics="native_first_official_ready_and_reference_leaf",
                  results=results, captures=capture_records, counts=counts,
                  selected=413, completed=len(results), unexecuted=[row["id"] for row in rows[len(results):]],
                  raw_directory=str(raw), checkpoint=str(progress), wait_script=wait_metadata,
                  wpt_commit=revision, metadata_commit=manifest["metadata_commit"],
                  viewport=[800, 600], deadline_ms=args.deadline_ms, process_timeout=args.process_timeout,
                  program_sha256=hashlib.sha256(args.program.read_bytes()).hexdigest(),
                  engine_library_sha256=hashlib.sha256((args.program.parent / "libbrowser.so").read_bytes()).hexdigest(),
                  partition=dict(scripted=413, static=1561, union=1974),
                  manifest_sha256=hashlib.sha256(args.manifest.read_bytes()).hexdigest(),
                  runner_sha256=runner_digest,
                  source_changed_during_run=hashlib.sha256(Path(__file__).read_bytes()).hexdigest() != runner_digest,
                  selected_sha256=hashlib.sha256("\n".join(sorted(omitted_ids)).encode()).hexdigest(),
                  captured_cases=sum(bool(row["captures"]) for row in results),
                  seconds=time.monotonic() - started)
    (args.out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(counts)
    return 0


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wpt", type=Path, default=ROOT / "build/ws074-suites/wpt")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--program", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--deadline-ms", type=int, default=5000)
    parser.add_argument("--process-timeout", type=int, default=20)
    parser.add_argument("--budget-seconds", type=int, default=2400)
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
