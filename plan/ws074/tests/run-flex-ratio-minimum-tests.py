#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check flex ratio minimums before growth, shrinkage and line breaking.

The optional Chromium comparison checks the same independent geometry cases.
No WPT checkout or server is needed for this focused regression.
"""

import argparse
import html
import json
from pathlib import Path
import re
import subprocess
import tempfile


# Name, container style, item style, child style, content dimensions, frame.
CASES = [
    ("row-content", "", "", "width:100px", (100, 100), (0, 0)),
    ("row-transfer", "", "", "", (50, 100), (0, 0)),
    ("row-shrink", "width:40px", "flex-basis:auto", "", (50, 100), (0, 0)),
    ("row-min-zero", "", "min-width:0", "width:100px", (0, 100), (0, 0)),
    ("row-scroll", "", "overflow:hidden", "width:100px", (0, 100), (0, 0)),
    ("row-max-main", "", "max-width:30px", "", (30, 100), (0, 0)),
    ("row-preferred-main", "", "width:20px", "", (20, 100), (0, 0)),
    ("row-max-cross", "", "max-height:40px", "", (20, 40), (0, 0)),
    ("row-min-cross", "", "min-height:120px", "", (60, 120), (0, 0)),
    ("row-border-box", "", "box-sizing:border-box;padding:10px;aspect-ratio:2",
     "", (180, 80), (20, 20)),
    ("column-content", "flex-direction:column", "", "height:100px", (100, 100), (0, 0)),
    ("column-transfer", "flex-direction:column", "", "", (100, 50), (0, 0)),
    ("column-shrink", "flex-direction:column;height:20px", "flex-basis:auto",
     "", (100, 50), (0, 0)),
    ("column-min-zero", "flex-direction:column", "min-height:0",
     "height:100px", (100, 0), (0, 0)),
    ("column-scroll", "flex-direction:column", "overflow:hidden",
     "height:100px", (100, 0), (0, 0)),
    ("column-max-main", "flex-direction:column", "max-height:30px",
     "", (100, 30), (0, 0)),
    ("column-preferred-main", "flex-direction:column", "height:20px",
     "", (100, 20), (0, 0)),
    ("column-max-cross", "flex-direction:column", "max-width:40px",
     "", (40, 20), (0, 0)),
    ("column-min-cross", "flex-direction:column", "min-width:120px",
     "", (120, 60), (0, 0)),
    ("column-border-box", "flex-direction:column",
     "box-sizing:border-box;padding:10px;aspect-ratio:1/2",
     "", (80, 180), (20, 20)),
    ("row-content-cross-cap", "", "max-height:40px",
     "width:100px", (20, 40), (0, 0)),
    ("column-content-cross-cap", "flex-direction:column", "max-width:40px",
     "height:100px", (40, 20), (0, 0)),
    ("row-auto-cross-cap", "", "height:auto;max-height:40px",
     "width:100px", (20, 40), (0, 0)),
    ("row-percent-main", "", "width:10%", "width:100px", (30, 100), (0, 0)),
    ("column-percent-main", "flex-direction:column", "height:10%",
     "height:100px", (100, 30), (0, 0)),
    ("row-percent-cross", "", "height:50%", "", (75, 150), (0, 0)),
    ("column-percent-cross", "flex-direction:column", "width:50%",
     "", (150, 75), (0, 0)),
    ("row-percent-cross-max", "", "max-height:10%", "", (15, 30), (0, 0)),
    ("column-percent-cross-max", "flex-direction:column", "max-width:10%",
     "", (30, 15), (0, 0)),
]

# Flexbox 4.5 caps the content suggestion through the cross maximum here.
# Chromium preserves the content width when an explicit height is also present.
# Keep the specification expectation and expose the reference difference.
CHROMIUM_DIFFERENCES = {"row-content-cross-cap": (100, 40)}


def source():
    boxes = []
    for name, container, item, child, _expected, _frame in CASES:
        size = "height:100px;aspect-ratio:1/2"
        if "flex-direction:column" in container:
            size = "width:100px;aspect-ratio:2/1"
        boxes.append(
            '<section style="display:flex;width:300px;height:300px;'
            'align-items:start;%s"><article id="%s" style="flex:0 1 0px;'
            '%s;%s"><div style="%s"></div></article></section>'
            % (container, name, size, item, child)
        )
    return "<!doctype html>" + "".join(boxes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", default="build/ws074-host/plain/browser")
    parser.add_argument("--chromium", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="flex-ratio-") as directory:
        path = Path(directory) / "cases.html"
        path.write_text(source(), encoding="utf-8")
        run = subprocess.run(
            [args.program, "--dump=layout",
             "--font=userland/desktop/fonts/Mahora-Regular.ttf", str(path)],
            capture_output=True, text=True, timeout=20, check=True,
        )
        observed = [tuple(map(float, dimensions)) for dimensions in re.findall(
            r"^ +block <article> -?[\d.]+ -?[\d.]+ ([\d.]+) ([\d.]+)$",
            run.stdout, re.M,
        )]
        if len(observed) != len(CASES):
            raise RuntimeError("unexpected item count: " + run.stdout)
        observed = [(width - case[5][0], height - case[5][1])
                    for case, (width, height) in zip(CASES, observed)]
        if args.chromium:
            script = """<script>
var boxes = document.querySelectorAll('article');
var dimensions = Array.from(boxes, function(box) {
  var rect = box.getBoundingClientRect();
  var style = getComputedStyle(box);
  return [rect.width - parseFloat(style.paddingLeft) - parseFloat(style.paddingRight),
          rect.height - parseFloat(style.paddingTop) - parseFloat(style.paddingBottom)];
});
var out = document.createElement('pre');
out.id = 'geometry';
out.textContent = JSON.stringify(dimensions);
document.body.appendChild(out);
</script>"""
            path.write_text(source() + script, encoding="utf-8")
            chrome = subprocess.run(
                ["chromium", "--headless", "--no-sandbox", "--disable-gpu",
                 "--user-data-dir=" + directory + "/profile", "--dump-dom", path.as_uri()],
                capture_output=True, text=True, timeout=30, check=True,
            )
            match = re.search(r'<pre id="geometry">(.*?)</pre>', chrome.stdout)
            if match is None:
                raise RuntimeError("Chromium geometry is absent: " + chrome.stderr[-1000:])
            reference = json.loads(html.unescape(match.group(1)))
            if len(reference) != len(CASES):
                raise RuntimeError("unexpected Chromium item count")
            for case, dimensions in zip(CASES, reference):
                expected = CHROMIUM_DIFFERENCES.get(case[0], case[4])
                if tuple(dimensions) != expected:
                    raise RuntimeError("Chromium disagrees with expected %s: %s" %
                                       (case[0], dimensions))
                if case[0] in CHROMIUM_DIFFERENCES:
                    print("reference difference %s: spec %s, Chromium %s" %
                          (case[0], case[4], dimensions))
        failures = 0
        for case, dimensions in zip(CASES, observed):
            if dimensions != case[4]:
                print("FAIL %s: expected %s, got %s" % (case[0], case[4], dimensions))
                failures += 1
        print("flex ratio minimum: %d/%d passed%s" %
              (len(CASES) - failures, len(CASES),
               " (Chromium compared; 1 documented difference)" if args.chromium else ""))
        return int(failures != 0)


if __name__ == "__main__":
    raise SystemExit(main())
