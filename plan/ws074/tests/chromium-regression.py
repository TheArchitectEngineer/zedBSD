#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Run reproducible browser-to-Chromium image comparisons.

The default run compares the saved, localized, script-free Amazon top and search
pages.  It never contacts the site unless --prepare needs to make the first
capture, or --refresh-capture is explicitly given.  Images and a machine-readable
report are written below build/.

  chromium-regression.py --prepare
  chromium-regression.py --baseline build/ws074-compare/report.json
"""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

try:
    from PIL import Image, ImageChops
except ImportError as error:
    raise SystemExit(
        "chromium-regression: Pillow is required; install python3-pil"
    ) from error


ROOT = Path(__file__).resolve().parents[3]
AGENT = "browser/0.1 (Kei)"
THRESHOLD = 16
FONT_NAMES = (
    "Inter.ttf",
    "JetBrainsMono-Regular.ttf",
    "DroidSansFallbackFull.ttf",
)


def sha256(path):
    """Return the SHA-256 digest of one file."""
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run(command, **kwargs):
    """Run a command and include useful output when it fails."""
    result = subprocess.run(command, **kwargs)
    if result.returncode != 0:
        stderr = getattr(result, "stderr", "") or ""
        raise RuntimeError("command failed: %s\n%s" % (" ".join(command), stderr[-2000:]))
    return result


def find_chromium(requested):
    """Find the explicitly selected or normal Debian Chromium executable."""
    names = [requested, os.environ.get("CHROMIUM"), "chromium", "chromium-browser"]
    for name in names:
        if not name:
            continue
        found = shutil.which(name)
        if found:
            return Path(found).resolve()
        path = Path(name).expanduser()
        if path.is_file() and os.access(path, os.X_OK):
            return path.resolve()
    raise RuntimeError(
        "Chromium was not found; install chromium or pass --chromium PATH"
    )


def font_directory():
    """Use the packaged font artifacts when present, otherwise their sources."""
    candidates = (ROOT / "build/ws035-fonts", ROOT / "userland/desktop/fonts")
    for candidate in candidates:
        if all((candidate / name).is_file() for name in FONT_NAMES):
            return candidate
    raise RuntimeError("the three WS074 comparison fonts were not found")


def command_version(command):
    """Return the first version line printed by a command."""
    result = subprocess.run(
        command,
        capture_output=True,
        text=True,
        timeout=30,
    )
    output = (result.stdout + result.stderr).strip().splitlines()
    return output[0] if output else "unknown"


def prepare(args, capture_dir):
    """Build the host browser and make a capture and Chromium font setup."""
    run(["sh", str(ROOT / "plan/ws074/tests/host-build.sh"), "plain"], cwd=ROOT)
    capture = ROOT / "plan/ws074/tests/amazon-capture.py"
    command = [sys.executable, str(capture), "--out", str(capture_dir)]
    if args.refresh_capture:
        command.append("--refetch")
    run(command, cwd=ROOT)
    run(["sh", str(ROOT / "plan/ws074/tests/chrome-fonts.sh")], cwd=ROOT)


def page_arguments(args, capture_dir):
    """Resolve requested pages, or the two stable Amazon capture variants."""
    if args.page:
        pages = []
        for item in args.page:
            if "=" not in item:
                raise RuntimeError("--page must be TAG=PATH")
            tag, raw_path = item.split("=", 1)
            path = Path(raw_path).expanduser().resolve()
            if not tag or not path.is_file():
                raise RuntimeError("page does not exist: %s" % raw_path)
            pages.append((tag, path))
        return pages
    suffix = "-local.html" if args.dynamic else "-local-noscript.html"
    pages = [(tag, capture_dir / (tag + suffix)) for tag in ("top", "search")]
    missing = [str(path) for _, path in pages if not path.is_file()]
    if missing:
        raise RuntimeError(
            "saved capture is missing; run this command once with --prepare: %s"
            % ", ".join(missing)
        )
    return pages


def render_browser(program, page, width, height, fonts, output, data_home):
    """Render one page with the WS074 CPU renderer."""
    ppm = output.with_suffix(".ppm")
    env = dict(os.environ)
    env["XDG_DATA_HOME"] = str(data_home)
    env["TZ"] = "UTC"
    command = [
        str(program),
        "--render",
        "--output=" + str(ppm),
        "--width=%d" % width,
        "--height=%d" % height,
        "--font=" + str(fonts / FONT_NAMES[0]),
        "--mono-font=" + str(fonts / FONT_NAMES[1]),
        "--fallback-font=" + str(fonts / FONT_NAMES[2]),
        str(page),
    ]
    result = run(command, capture_output=True, text=True, timeout=180, env=env)
    with Image.open(ppm) as image:
        image.convert("RGB").save(output)
    ppm.unlink()
    errors = [line for line in result.stderr.splitlines() if "Uncaught" in line]
    return errors


def render_chromium(chromium, page, width, height, output, font_config, work):
    """Render one local page with an isolated deterministic Chromium profile."""
    profile = Path(tempfile.mkdtemp(prefix="profile-", dir=work))
    env = dict(os.environ)
    env["FONTCONFIG_FILE"] = str(font_config)
    env["TZ"] = "UTC"
    command = [
        str(chromium),
        "--headless",
        "--no-sandbox",
        "--disable-gpu",
        "--hide-scrollbars",
        "--disable-background-networking",
        "--disable-component-update",
        "--disable-default-apps",
        "--disable-sync",
        "--metrics-recording-only",
        "--mute-audio",
        "--force-device-scale-factor=1",
        "--host-resolver-rules=MAP * 0.0.0.0, EXCLUDE localhost",
        "--lang=ja-JP",
        "--user-data-dir=" + str(profile),
        "--user-agent=" + AGENT,
        "--virtual-time-budget=5000",
        "--window-size=%d,%d" % (width, height),
        "--screenshot=" + str(output),
        page.as_uri(),
    ]
    try:
        run(command, capture_output=True, text=True, timeout=180, env=env)
    finally:
        shutil.rmtree(profile, ignore_errors=True)
    if not output.is_file():
        raise RuntimeError("Chromium made no screenshot for %s" % page)


def compare_images(ours_path, chromium_path, side_path):
    """Compare two screenshots and make ours/reference/difference output."""
    with Image.open(ours_path) as source:
        ours = source.convert("RGB")
    with Image.open(chromium_path) as source:
        chromium = source.convert("RGB")
    width = min(ours.width, chromium.width)
    height = min(ours.height, chromium.height)
    ours = ours.crop((0, 0, width, height))
    chromium = chromium.crop((0, 0, width, height))
    difference = ImageChops.difference(ours, chromium)
    pixels_ours = ours.load()
    pixels_chromium = chromium.load()
    pixels_difference = difference.load()
    mask = Image.new("RGB", (width, height), (255, 255, 255))
    pixels_mask = mask.load()
    agree = 0
    ink = 0
    ink_agree = 0
    for y in range(height):
        for x in range(width):
            same = max(pixels_difference[x, y]) <= THRESHOLD
            if same:
                agree += 1
                color = pixels_ours[x, y]
                pixels_mask[x, y] = tuple(200 + channel // 5 for channel in color)
            else:
                pixels_mask[x, y] = (230, 30, 30)
            if min(pixels_ours[x, y]) < 240 or min(pixels_chromium[x, y]) < 240:
                ink += 1
                if same:
                    ink_agree += 1
    side = Image.new("RGB", (width * 3 + 20, height), (128, 128, 128))
    side.paste(ours, (0, 0))
    side.paste(chromium, (width + 10, 0))
    side.paste(mask, (width * 2 + 20, 0))
    side.save(side_path)
    total = width * height
    return {
        "width": width,
        "height": height,
        "pixel_agreement": 100.0 * agree / max(total, 1),
        "pixels_agree": agree,
        "pixels_total": total,
        "ink_agreement": 100.0 * ink_agree / max(ink, 1),
        "ink_pixels_agree": ink_agree,
        "ink_pixels_total": ink,
    }


def check_baseline(report, path, maximum_regression):
    """Return readable failures for metrics below an approved earlier report."""
    if path is None:
        return []
    with path.open(encoding="utf-8") as stream:
        baseline = json.load(stream)
    previous = {page["tag"]: page for page in baseline["pages"]}
    failures = []
    for page in report["pages"]:
        old = previous.get(page["tag"])
        if old is None:
            failures.append("%s is absent from baseline" % page["tag"])
            continue
        for metric in ("pixel_agreement", "ink_agreement"):
            drop = old[metric] - page[metric]
            if drop > maximum_regression:
                failures.append(
                    "%s %s regressed %.3f percentage points"
                    % (page["tag"], metric, drop)
                )
    return failures


def git_revision():
    """Return the source revision used for this comparison."""
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True
    )
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--refresh-capture", action="store_true")
    parser.add_argument("--dynamic", action="store_true")
    parser.add_argument("--page", action="append", metavar="TAG=PATH")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=900)
    parser.add_argument("--chromium")
    parser.add_argument(
        "--program", default=str(ROOT / "build/ws074-host/plain/browser")
    )
    parser.add_argument(
        "--capture-dir", default=str(ROOT / "build/ws074-amazon")
    )
    parser.add_argument("--out", default=str(ROOT / "build/ws074-compare"))
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--max-regression", type=float, default=0.25)
    args = parser.parse_args()

    try:
        capture_dir = Path(args.capture_dir).expanduser().resolve()
        output = Path(args.out).expanduser().resolve()
        output.mkdir(parents=True, exist_ok=True)
        if args.prepare or args.refresh_capture:
            prepare(args, capture_dir)
        program = Path(args.program).expanduser().resolve()
        if not program.is_file():
            raise RuntimeError("host browser is missing; run with --prepare")
        chromium = find_chromium(args.chromium)
        fonts = font_directory()
        font_config = ROOT / "build/ws074-chrome/fonts.conf"
        if not font_config.is_file():
            run(["sh", str(ROOT / "plan/ws074/tests/chrome-fonts.sh")], cwd=ROOT)
        pages = page_arguments(args, capture_dir)

        report = {
            "schema": 1,
            "created_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "git_revision": git_revision(),
            "mode": "dynamic-diagnostic" if args.dynamic else "static-regression",
            "viewport": {"width": args.width, "height": args.height, "scale": 1},
            "threshold": THRESHOLD,
            "user_agent": AGENT,
            "browser": {
                "path": str(program),
                "sha256": sha256(program),
            },
            "chromium": {
                "path": str(chromium),
                "version": command_version([str(chromium), "--version"]),
            },
            "fonts": {
                name: sha256(fonts / name) for name in FONT_NAMES
            },
            "capture_manifest_sha256": None,
            "pages": [],
        }
        capture_manifest = capture_dir / "capture-manifest.json"
        if capture_manifest.is_file():
            report["capture_manifest_sha256"] = sha256(capture_manifest)

        data_home = output / "browser-data"
        data_home.mkdir(exist_ok=True)
        for tag, page in pages:
            ours = output / (tag + "-ours.png")
            reference = output / (tag + "-chromium.png")
            side = output / (tag + "-side.png")
            errors = render_browser(
                program, page, args.width, args.height, fonts, ours, data_home
            )
            render_chromium(
                chromium,
                page,
                args.width,
                args.height,
                reference,
                font_config,
                output,
            )
            metrics = compare_images(ours, reference, side)
            metrics.update(
                {
                    "tag": tag,
                    "input": str(page),
                    "input_sha256": sha256(page),
                    "script_errors": errors,
                    "outputs": {
                        path.name: sha256(path)
                        for path in (ours, reference, side)
                    },
                }
            )
            report["pages"].append(metrics)
            print(
                "%s: pixels %.2f%%, ink %.2f%%, script errors %d"
                % (
                    tag,
                    metrics["pixel_agreement"],
                    metrics["ink_agreement"],
                    len(errors),
                )
            )

        failures = check_baseline(report, args.baseline, args.max_regression)
        report["baseline"] = str(args.baseline.resolve()) if args.baseline else None
        report["max_regression"] = args.max_regression
        report["failures"] = failures
        report["status"] = "failed" if failures else "passed"
        report_path = output / "report.json"
        with report_path.open("w", encoding="utf-8") as stream:
            json.dump(report, stream, ensure_ascii=False, indent=2, sort_keys=True)
            stream.write("\n")
        print(report_path)
        for failure in failures:
            print("regression: " + failure, file=sys.stderr)
        return 1 if failures else 0
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print("chromium-regression: %s" % error, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
