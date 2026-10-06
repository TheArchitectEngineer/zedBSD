#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Compares browser's block boxes with Chromium's for the same page.

  chrome-boxes.py PAGE.html [--width W] [--height H] [--program PATH] [--show N]

Chromium (headless, the fonts of build/ws074-chrome/fonts.conf, made by chrome-fonts.sh) runs
the page with a script appended that writes every element's border box (getBoundingClientRect)
as JSON; browser's `--dump=layout` gives its block boxes.  Boxes are matched in document
order by element name, and a box agrees when its four edges are within 1 px.  Prints the share
that agrees and the ones that do not.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

SCRIPT = """<script>
(function () {
  var out = [];
  var all = document.querySelectorAll('*');
  for (var i = 0; i < all.length; i++) {
    var e = all[i];
    var style = getComputedStyle(e);
    if (style.display === 'none' || style.display === 'inline' || style.display === 'contents') continue;
    var r = e.getBoundingClientRect();
    out.push([e.localName, r.left, r.top, r.width, r.height]);
  }
  document.documentElement.innerHTML = '<body><pre id="zbox">' + JSON.stringify(out) + '</pre></body>';
})();
</script>"""


def chrome_boxes(root, page, width, height):
    with open(page, encoding="utf-8") as stream:
        text = stream.read()
    fd, copy = tempfile.mkstemp(suffix=".html", dir=os.path.dirname(os.path.abspath(page)))
    with os.fdopen(fd, "w", encoding="utf-8") as stream:
        stream.write(text + SCRIPT)
    env = dict(os.environ)
    env["FONTCONFIG_FILE"] = os.path.join(root, "build/ws074-chrome/fonts.conf")
    try:
        result = subprocess.run(["chromium", "--headless", "--no-sandbox", "--disable-gpu", "--hide-scrollbars",
                                 "--force-device-scale-factor=1", "--user-data-dir=" + os.path.join(root, "build/ws074-chrome/profile"),
                                 "--window-size=%d,%d" % (width, height), "--dump-dom", "file://" + copy],
                                capture_output=True, text=True, timeout=120, env=env)
    finally:
        os.unlink(copy)
    match = re.search(r'<pre id="zbox">(.*?)</pre>', result.stdout, re.S)
    if not match:
        raise SystemExit("chromium gave no boxes: " + result.stderr[-500:])
    raw = match.group(1).replace("&quot;", '"').replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">")
    return [tuple(item) for item in json.loads(raw)]


def our_boxes(root, program, page, width, height):
    fonts = os.path.join(root, "userland/desktop/fonts")
    if not os.path.isfile(os.path.join(fonts, "Mahora-Regular.ttf")):
        fonts = os.path.join(root, "userland/desktop/fonts")
    result = subprocess.run([program, "--dump=layout", "--width=%d" % width, "--height=%d" % height,
                             "--font=" + os.path.join(fonts, "Mahora-Regular.ttf"),
                             "--mono-font=" + os.path.join(fonts, "JetBrainsMono-Regular.ttf"),
                             "--fallback-font=" + os.path.join(fonts, "DroidSansFallbackFull.ttf"), page],
                            capture_output=True, text=True, check=True)
    boxes = []
    for line in result.stdout.splitlines():
        match = re.match(r"\s*block <([^>]+)> (\S+) (\S+) (\S+) (\S+)", line)
        if match:
            boxes.append((match.group(1), float(match.group(2)), float(match.group(3)),
                          float(match.group(4)), float(match.group(5))))
    return boxes


def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    parser = argparse.ArgumentParser()
    parser.add_argument("page")
    parser.add_argument("--width", type=int, default=800)
    parser.add_argument("--height", type=int, default=600)
    parser.add_argument("--program", default=os.path.join(root, "build/ws074-host/plain/browser"))
    parser.add_argument("--show", type=int, default=20)
    args = parser.parse_args()
    theirs = chrome_boxes(root, args.page, args.width, args.height)
    ours = our_boxes(root, args.program, args.page, args.width, args.height)
    agree = 0
    shown = 0
    used = [False] * len(ours)
    for name, left, top, width, height in theirs:
        match = None
        for index, box in enumerate(ours):
            if not used[index] and box[0] == name:
                match = index
                break
        if match is None:
            if shown < args.show:
                print("missing  <%s> chrome %.2f %.2f %.2f %.2f" % (name, left, top, width, height))
                shown += 1
            continue
        used[match] = True
        box = ours[match]
        if (abs(box[1] - left) <= 1 and abs(box[2] - top) <= 1 and
                abs(box[1] + box[3] - left - width) <= 1 and abs(box[2] + box[4] - top - height) <= 1):
            agree += 1
        elif shown < args.show:
            print("differs  <%s> chrome %.2f %.2f %.2f %.2f  ours %.2f %.2f %.2f %.2f"
                  % (name, left, top, width, height, box[1], box[2], box[3], box[4]))
            shown += 1
    total = len(theirs)
    print("chrome-boxes %s: %d/%d boxes within 1px (%.1f%%)" % (os.path.basename(args.page), agree, total,
                                                               100.0 * agree / max(total, 1)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
