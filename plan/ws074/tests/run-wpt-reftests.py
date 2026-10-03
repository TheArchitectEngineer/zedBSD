#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Run deterministic, script-free WPT reftests with the browser renderer.

The pinned WPT checkout stays below build/ws074-suites.  Tests and their
references are served from one local HTTP origin so root-relative resources
and Ahem work.  A test passes when its pixels equal any rel=match reference
and differ from every rel=mismatch reference, within its fuzzy allowance.
"""

import argparse
from collections import defaultdict, deque
import contextlib
import functools
from html.parser import HTMLParser
import http.server
import json
import os
from pathlib import Path
import re
import socketserver
import subprocess
import sys
import tempfile
import threading
from urllib.parse import quote, urljoin, urlsplit

try:
    from PIL import Image, ImageChops, ImageEnhance
except ImportError as error:
    raise SystemExit("run-wpt-reftests: Pillow is required") from error


ROOT = Path(__file__).resolve().parents[3]
WPT = ROOT / "build/ws074-suites/wpt"
WPT_COMMIT = "2d66b9b7998bb58c336138c178323ddee857b586"
FONT_NAMES = ("Inter.ttf", "JetBrainsMono-Regular.ttf",
              "DroidSansFallbackFull.ttf")
ACTIVE_SCRIPT = re.compile(r"<script\b|reftest-wait", re.I)
FUZZY_VALUE = re.compile(r"(maxDifference|totalPixels)\s*=\s*(\d+)(?:-(\d+))?")


class ReftestMetadata(HTMLParser):
    """Read links and metadata without interpreting the test document."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.references = []
        self.flags = set()
        self.fuzzy = []

    def handle_starttag(self, tag, attrs):
        values = {name.lower(): value or "" for name, value in attrs}
        if tag.lower() == "link":
            relations = values.get("rel", "").lower().split()
            href = values.get("href")
            for relation in ("match", "mismatch"):
                if relation in relations and href:
                    self.references.append((relation, href))
        if tag.lower() != "meta":
            return
        name = values.get("name", "").lower()
        content = values.get("content", "")
        if name == "flags":
            self.flags.update(content.lower().split())
        elif name == "fuzzy":
            self.fuzzy.append(content)


class QuietServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    """Serve WPT files locally without writing one line per resource."""

    def log_message(self, _format, *args):
        del args


def font_directory():
    for directory in (ROOT / "userland/desktop/fonts",):
        if all((directory / name).is_file() for name in FONT_NAMES):
            return directory
    raise RuntimeError("comparison fonts were not found")


def verify_suite():
    if not (WPT / ".git").is_dir():
        raise RuntimeError("fetch WPT first with fetch-suites.sh wpt")
    result = subprocess.run(
        ["git", "-C", str(WPT), "rev-parse", "HEAD"],
        capture_output=True, text=True, check=True,
    )
    actual = result.stdout.strip()
    if actual != WPT_COMMIT:
        raise RuntimeError("WPT is %s, expected %s" % (actual, WPT_COMMIT))


def read_metadata(path):
    text = path.read_text(encoding="utf-8", errors="replace")
    parser = ReftestMetadata()
    parser.feed(text)
    return text, parser


def resolve_reference(test, href):
    """Resolve one same-suite reference and retain its query string."""
    parsed = urlsplit(href)
    if parsed.scheme or parsed.netloc:
        return None
    test_url = "/" + test.relative_to(WPT).as_posix()
    resolved = urlsplit(urljoin(test_url, href))
    relative = resolved.path.lstrip("/")
    path = (WPT / relative).resolve()
    try:
        path.relative_to(WPT.resolve())
    except ValueError:
        return None
    suffix = ("?" + resolved.query) if resolved.query else ""
    return path, relative + suffix


def fuzzy_limits(values, reference):
    """Return the largest allowed channel difference and pixel count."""
    maximum = 0
    pixels = 0
    reference_name = urlsplit(reference).path.rsplit("/", 1)[-1]
    for value in values:
        rule = value
        if ":" in value and value.split(":", 1)[0].endswith(
                (".html", ".htm", ".xht", ".xhtml", ".svg")):
            name, rule = value.split(":", 1)
            if name.rsplit("/", 1)[-1] != reference_name:
                continue
        for match in FUZZY_VALUE.finditer(rule):
            upper = int(match.group(3) or match.group(2))
            if match.group(1) == "maxDifference":
                maximum = max(maximum, upper)
            else:
                pixels = max(pixels, upper)
    return maximum, pixels


def discover(relative_root):
    """Find static reftests and explain the ones omitted from this pass."""
    directory = (WPT / relative_root).resolve()
    if not directory.is_dir():
        raise RuntimeError("WPT root is absent: " + relative_root)
    candidates = []
    skipped = defaultdict(int)
    extensions = {".html", ".htm", ".xht", ".xhtml", ".svg"}
    for path in sorted(directory.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in extensions:
            continue
        text, metadata = read_metadata(path)
        if not metadata.references:
            continue
        if ACTIVE_SCRIPT.search(text):
            skipped["script"] += 1
            continue
        if metadata.flags.intersection({"animated", "interact", "paged", "speech"}):
            skipped["non_static_flag"] += 1
            continue
        references = []
        reason = None
        for relation, href in metadata.references:
            resolved = resolve_reference(path, href)
            if resolved is None:
                reason = "external_reference"
                break
            reference_path, reference_url = resolved
            if not reference_path.is_file():
                reason = "missing_reference"
                break
            reference_text = reference_path.read_text(
                encoding="utf-8", errors="replace"
            )
            if ACTIVE_SCRIPT.search(reference_text):
                reason = "script_reference"
                break
            references.append((relation, reference_url))
        if reason:
            skipped[reason] += 1
            continue
        candidates.append({
            "path": path.relative_to(WPT).as_posix(),
            "references": references,
            "fuzzy": metadata.fuzzy,
            "flags": sorted(metadata.flags),
        })
    return candidates, dict(sorted(skipped.items()))


def stratified(candidates, relative_root, limit):
    """Take a repeatable round-robin sample across first-level sections."""
    if limit <= 0 or len(candidates) <= limit:
        return candidates
    root_parts = Path(relative_root).parts
    groups = defaultdict(list)
    for candidate in candidates:
        parts = Path(candidate["path"]).parts[len(root_parts):]
        group = parts[0] if len(parts) > 1 else "."
        groups[group].append(candidate)
    queues = {name: deque(items) for name, items in groups.items()}
    selected = []
    while len(selected) < limit:
        progress = False
        for name in sorted(queues):
            if queues[name] and len(selected) < limit:
                selected.append(queues[name].popleft())
                progress = True
        if not progress:
            break
    return selected


@contextlib.contextmanager
def suite_server():
    handler = functools.partial(QuietHandler, directory=str(WPT))
    server = QuietServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield "http://127.0.0.1:%d/" % server.server_port
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


def render(program, fonts, width, height, url, output, data_home):
    command = [
        str(program), "--render", "--async", "--output=" + str(output),
        "--width=%d" % width, "--height=%d" % height,
        "--font=" + str(fonts / FONT_NAMES[0]),
        "--mono-font=" + str(fonts / FONT_NAMES[1]),
        "--fallback-font=" + str(fonts / FONT_NAMES[2]), url,
    ]
    environment = dict(os.environ)
    environment["XDG_DATA_HOME"] = str(data_home)
    result = subprocess.run(
        command, capture_output=True, text=True, timeout=60,
        env=environment,
    )
    if result.returncode != 0 or not output.is_file():
        message = (result.stderr or result.stdout).strip()[-1200:]
        raise RuntimeError(message or "renderer made no image")
    with Image.open(output) as image:
        return image.convert("RGB").copy(), [
            line for line in result.stderr.splitlines() if "Uncaught" in line
        ]


def compare(left, right, maximum, pixels):
    if left.size != right.size:
        return False, {"size_mismatch": [left.size, right.size]}
    difference = ImageChops.difference(left, right)
    extrema = difference.getextrema()
    max_difference = max(high for _low, high in extrema)
    samples = getattr(difference, "get_flattened_data", difference.getdata)()
    different = sum(pixel != (0, 0, 0) for pixel in samples)
    return max_difference <= maximum and different <= pixels, {
        "max_difference": max_difference,
        "different_pixels": different,
        "allowed_max_difference": maximum,
        "allowed_different_pixels": pixels,
    }


def save_failure(directory, number, test_name, test, reference, difference):
    name = "%03d-%s" % (number, Path(test_name).stem[:45])
    bright = ImageEnhance.Brightness(difference).enhance(4.0)
    side = Image.new("RGB", (test.width * 3, test.height), "white")
    side.paste(test, (0, 0))
    side.paste(reference, (test.width, 0))
    side.paste(bright, (test.width * 2, 0))
    path = directory / (name + ".png")
    side.save(path)
    return path.name


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", type=Path,
                        default=ROOT / "build/ws074-host/plain/browser")
    parser.add_argument("--root", default="css/CSS2")
    parser.add_argument("--limit", type=int, default=100,
                        help="0 runs every eligible test")
    parser.add_argument("--width", type=int, default=800)
    parser.add_argument("--height", type=int, default=600)
    parser.add_argument("--save-failures", type=int, default=20)
    parser.add_argument("--out", type=Path,
                        default=ROOT / "build/ws074-wpt-reftest")
    args = parser.parse_args()
    verify_suite()
    if not args.program.is_file():
        raise RuntimeError("browser is absent: " + str(args.program))
    fonts = font_directory()
    args.out.mkdir(parents=True, exist_ok=True)
    failures = args.out / "failures"
    failures.mkdir(exist_ok=True)
    candidates, skipped = discover(args.root)
    selected = stratified(candidates, args.root, args.limit)
    results = []
    counts = defaultdict(int)
    with tempfile.TemporaryDirectory(prefix="wpt-render-", dir=args.out) as raw:
        temporary = Path(raw)
        data_home = temporary / "data"
        data_home.mkdir()
        with suite_server() as base:
            for index, candidate in enumerate(selected, 1):
                test_url = base + quote(candidate["path"], safe="/?=&")
                record = {"test": candidate["path"], "references": []}
                try:
                    test_image, errors = render(
                        args.program, fonts, args.width, args.height, test_url,
                        temporary / "test.ppm", data_home,
                    )
                    record["script_errors"] = errors
                    match_results = []
                    mismatch_results = []
                    first_failed_images = None
                    for relation, reference in candidate["references"]:
                        reference_url = base + quote(reference, safe="/?=&")
                        reference_image, reference_errors = render(
                            args.program, fonts, args.width, args.height,
                            reference_url, temporary / "reference.ppm", data_home,
                        )
                        maximum, pixels = fuzzy_limits(candidate["fuzzy"], reference)
                        equal, metrics = compare(
                            test_image, reference_image, maximum, pixels
                        )
                        item = {"relation": relation, "path": reference,
                                "equal": equal, **metrics}
                        if reference_errors:
                            item["script_errors"] = reference_errors
                        record["references"].append(item)
                        if relation == "match":
                            match_results.append(equal)
                        else:
                            mismatch_results.append(not equal)
                        if not equal and first_failed_images is None:
                            first_failed_images = (
                                test_image, reference_image,
                                ImageChops.difference(test_image, reference_image),
                            )
                    passed = ((not match_results or any(match_results)) and
                              all(mismatch_results))
                    record["status"] = "pass" if passed else "fail"
                    if (not passed and counts["fail"] < args.save_failures and
                            first_failed_images is not None):
                        record["image"] = save_failure(
                            failures, counts["fail"] + 1,
                            candidate["path"], *first_failed_images
                        )
                except (RuntimeError, subprocess.TimeoutExpired) as error:
                    record["status"] = "error"
                    record["error"] = str(error)
                counts[record["status"]] += 1
                results.append(record)
                print("[%d/%d] %s %s" % (
                    index, len(selected), record["status"], candidate["path"]
                ))
    report = {
        "schema": 1,
        "suite": "wpt",
        "commit": WPT_COMMIT,
        "root": args.root,
        "viewport": [args.width, args.height],
        "eligible": len(candidates),
        "selected": len(selected),
        "selection": "first-level round-robin, path-sorted",
        "skipped": skipped,
        "counts": dict(sorted(counts.items())),
        "results": results,
    }
    report_path = args.out / "report.json"
    report_path.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print("WPT reftest: %d pass, %d fail, %d error of %d" % (
        counts["pass"], counts["fail"], counts["error"], len(selected)
    ))
    print(report_path)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print("run-wpt-reftests: " + str(error), file=sys.stderr)
        sys.exit(1)
