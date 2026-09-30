#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Run the pinned WPT copies of Acid2 and Acid3 with browser.

Acid2 normally waits for a click and scrolls its face into view.  Its WPT
wrapper performs that scroll in an iframe.  Headless browser lays out after
scripts settle, so this runner makes a temporary copy which hides the intro
and removes the 100em pre-scroll margin.  The actual test rules are untouched.
Acid3 is run unchanged; its DOM score, console exceptions and reference image
difference are recorded separately.
"""

import argparse
import contextlib
import functools
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
from urllib.parse import quote, urlsplit

try:
    from PIL import Image, ImageChops, ImageEnhance
except ImportError as error:
    raise SystemExit("run-acid-tests: Pillow is required") from error


ROOT = Path(__file__).resolve().parents[3]
WPT = ROOT / "build/ws074-suites/wpt"
ACID = WPT / "acid"
WPT_COMMIT = "2d66b9b7998bb58c336138c178323ddee857b586"
FONT_NAMES = ("Inter.ttf", "JetBrainsMono-Regular.ttf",
              "DroidSansFallbackFull.ttf")


class AcidServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


class AcidHandler(http.server.SimpleHTTPRequestHandler):
    """Serve the Acid resources and apply their WPT .headers MIME types."""

    def log_message(self, _format, *args):
        del args

    def guess_type(self, path):
        headers = Path(path + ".headers")
        if headers.is_file():
            for line in headers.read_text(
                    encoding="utf-8", errors="replace").splitlines():
                if line.lower().startswith("content-type:"):
                    return line.split(":", 1)[1].strip()
        return super().guess_type(path)

    def do_GET(self):
        if "pipe=status(404)" in urlsplit(self.path).query:
            self.send_error(404)
            return
        super().do_GET()


def fonts():
    for directory in (ROOT / "build/ws035-fonts",
                      ROOT / "userland/desktop/fonts"):
        if all((directory / name).is_file() for name in FONT_NAMES):
            return directory
    raise RuntimeError("comparison fonts were not found")


def verify_suite():
    result = subprocess.run(
        ["git", "-C", str(WPT), "rev-parse", "HEAD"],
        capture_output=True, text=True, check=True,
    )
    if result.stdout.strip() != WPT_COMMIT:
        raise RuntimeError("the WPT checkout is not at the pinned commit")
    if not (ACID / "acid2/test.html").is_file():
        raise RuntimeError("fetch WPT with its acid/ sparse path first")


@contextlib.contextmanager
def server():
    handler = functools.partial(AcidHandler, directory=str(WPT))
    instance = AcidServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=instance.serve_forever, daemon=True)
    thread.start()
    try:
        yield "http://127.0.0.1:%d/" % instance.server_port
    finally:
        instance.shutdown()
        instance.server_close()
        thread.join()


def command_base(program, font_dir, width, height, data_home, mode):
    command = [
        str(program), mode, "--async", "--width=%d" % width,
        "--height=%d" % height,
        "--font=" + str(font_dir / FONT_NAMES[0]),
        "--mono-font=" + str(font_dir / FONT_NAMES[1]),
        "--fallback-font=" + str(font_dir / FONT_NAMES[2]),
    ]
    environment = dict(os.environ)
    environment["XDG_DATA_HOME"] = str(data_home)
    return command, environment


def render(program, font_dir, width, height, data_home, url, output):
    ppm = output.with_suffix(".ppm")
    command, environment = command_base(
        program, font_dir, width, height, data_home, "--render"
    )
    command.insert(2, "--output=" + str(ppm))
    command.append(url)
    result = subprocess.run(
        command, capture_output=True, text=True, timeout=90, env=environment,
    )
    if result.returncode != 0 or not ppm.is_file():
        raise RuntimeError((result.stderr or result.stdout).strip()[-1200:])
    with Image.open(ppm) as image:
        converted = image.convert("RGB")
        converted.save(output)
    ppm.unlink()
    return [line for line in result.stderr.splitlines() if "Uncaught" in line]


def run_console(program, font_dir, width, height, data_home, url):
    command, environment = command_base(
        program, font_dir, width, height, data_home, "--run"
    )
    command.append(url)
    result = subprocess.run(
        command, capture_output=True, text=True, timeout=90, env=environment,
    )
    return result.returncode, result.stdout.splitlines(), result.stderr.splitlines()


def dump_dom(program, font_dir, width, height, data_home, url):
    command, environment = command_base(
        program, font_dir, width, height, data_home, "--dump=dom"
    )
    command.append(url)
    result = subprocess.run(
        command, capture_output=True, text=True, timeout=90, env=environment,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip()[-1200:])
    return result.stdout, result.stderr.splitlines()


def dump_page(program, font_dir, width, height, data_home, url, kind, output):
    command, environment = command_base(
        program, font_dir, width, height, data_home, "--dump=" + kind
    )
    command.append(url)
    result = subprocess.run(
        command, capture_output=True, text=True, timeout=90, env=environment,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip()[-1200:])
    output.write_text(result.stdout, encoding="utf-8")


def image_result(test_path, reference_path, side_path):
    with Image.open(test_path) as source:
        test = source.convert("RGB")
    with Image.open(reference_path) as source:
        reference = source.convert("RGB")
    difference = ImageChops.difference(test, reference)
    maximum = max(high for _low, high in difference.getextrema())
    samples = getattr(difference, "get_flattened_data", difference.getdata)()
    different = sum(pixel != (0, 0, 0) for pixel in samples)
    bright = ImageEnhance.Brightness(difference).enhance(4.0)
    side = Image.new("RGB", (test.width * 3, test.height), "white")
    side.paste(test, (0, 0))
    side.paste(reference, (test.width, 0))
    side.paste(bright, (test.width * 2, 0))
    side.save(side_path)
    return {
        "pass": maximum == 0,
        "max_difference": maximum,
        "different_pixels": different,
        "total_pixels": test.width * test.height,
        "agreement": 100.0 * (test.width * test.height - different) /
        (test.width * test.height),
        "test_image": test_path.name,
        "reference_image": reference_path.name,
        "side_image": side_path.name,
    }


def acid2_harness():
    source = (ACID / "acid2/test.html").read_text(encoding="utf-8")
    harness = ("<style>.intro{display:none!important}"
               "#top{margin-top:0!important}</style>")
    source = source.replace("</head>", harness + "</head>")
    descriptor, name = tempfile.mkstemp(
        prefix="zedbsd-acid2-", suffix=".html", dir=ACID / "acid2"
    )
    with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
        stream.write(source)
    return Path(name)


def acid3_reference():
    source = (ACID / "acid3/reference.sub.html").read_text(encoding="utf-8")
    source = source.replace("{{host}}", "invalid.test")
    descriptor, name = tempfile.mkstemp(
        prefix="zedbsd-acid3-reference-", suffix=".html",
        dir=ACID / "acid3",
    )
    with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
        stream.write(source)
    return Path(name)


def acid3_score(dom):
    lines = dom.splitlines()
    for index, line in enumerate(lines):
        if 'id="score"' not in line:
            continue
        for candidate in lines[index + 1:index + 8]:
            match = re.search(r'"(.*)"\s*$', candidate.strip())
            if match:
                return match.group(1)
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", type=Path,
                        default=ROOT / "build/ws074-host/plain/browser")
    parser.add_argument("--out", type=Path,
                        default=ROOT / "build/ws074-acid")
    args = parser.parse_args()
    verify_suite()
    args.out.mkdir(parents=True, exist_ok=True)
    font_dir = fonts()
    data_home = args.out / "data"
    data_home.mkdir(exist_ok=True)
    acid2 = acid2_harness()
    acid3_ref = acid3_reference()
    try:
        with server() as base:
            acid2_url = base + quote(acid2.relative_to(WPT).as_posix())
            acid2_reference_url = base + "acid/acid2/reference.html"
            render(args.program, font_dir, 400, 300, data_home, acid2_url,
                   args.out / "acid2-test.png")
            render(args.program, font_dir, 400, 300, data_home,
                   acid2_reference_url, args.out / "acid2-reference.png")
            acid2_result = image_result(
                args.out / "acid2-test.png", args.out / "acid2-reference.png",
                args.out / "acid2-side.png",
            )
            for kind in ("dom", "style", "layout", "paint"):
                dump_page(
                    args.program, font_dir, 400, 300, data_home, acid2_url,
                    kind, args.out / ("acid2-test." + kind),
                )
                dump_page(
                    args.program, font_dir, 400, 300, data_home,
                    acid2_reference_url, kind,
                    args.out / ("acid2-reference." + kind),
                )

            acid3_url = base + "acid/acid3/test.html"
            acid3_reference_url = base + quote(
                acid3_ref.relative_to(WPT).as_posix()
            )
            render_errors = render(
                args.program, font_dir, 800, 600, data_home, acid3_url,
                args.out / "acid3-test.png",
            )
            render(args.program, font_dir, 800, 600, data_home,
                   acid3_reference_url, args.out / "acid3-reference.png")
            acid3_result = image_result(
                args.out / "acid3-test.png", args.out / "acid3-reference.png",
                args.out / "acid3-side.png",
            )
            status, console, console_errors = run_console(
                args.program, font_dir, 800, 600, data_home, acid3_url
            )
            dom, dom_errors = dump_dom(
                args.program, font_dir, 800, 600, data_home, acid3_url
            )
            (args.out / "acid3.dom").write_text(dom, encoding="utf-8")
            all_errors = []
            for line in render_errors + console + console_errors + dom_errors:
                if "Uncaught" in line and line not in all_errors:
                    all_errors.append(line)
            acid3_result.update({
                "score": acid3_score(dom),
                "run_status": status,
                "exceptions": all_errors,
                "dom": "acid3.dom",
            })
    finally:
        acid2.unlink(missing_ok=True)
        acid3_ref.unlink(missing_ok=True)
    report = {
        "schema": 1,
        "suite": "WPT historical Acid tests",
        "commit": WPT_COMMIT,
        "acid2": acid2_result,
        "acid3": acid3_result,
        "note": ("Historical diagnostics only; WPT warns that these tests are "
                 "not a standards certification."),
    }
    report_path = args.out / "report.json"
    report_path.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print("Acid2: %.2f%% pixels, %s" % (
        acid2_result["agreement"],
        "PASS" if acid2_result["pass"] else "FAIL",
    ))
    print("Acid3: score %s, %.2f%% pixels, %s" % (
        acid3_result["score"], acid3_result["agreement"],
        "PASS" if acid3_result["pass"] else "FAIL",
    ))
    if acid3_result["exceptions"]:
        print("Acid3 first exception: " + acid3_result["exceptions"][0])
    print(report_path)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print("run-acid-tests: " + str(error), file=sys.stderr)
        sys.exit(1)
