#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Compare browser and Chromium DOM trees and block boxes for a fixed page.

Both sides are reduced to the same ordered representation. DOM records retain
node type, depth, element name, sorted attributes and normalized nonempty text.
Box records retain element name and border-box geometry.
"""

import argparse
import difflib
from html import unescape
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from comparison_config import CHROME_USER_AGENT

ROOT = Path(__file__).resolve().parents[3]
FONT_NAMES = ("Inter.ttf", "JetBrainsMono-Regular.ttf", "DroidSansFallbackFull.ttf")
RECORD_START = re.compile(r"(?m)^\| (?= *)")
ELEMENT = re.compile(r"^(?:svg |math )?<([^>]+)>$")
ATTRIBUTE = re.compile(r'^([^=]+)="(.*)"$', re.S)
DOCTYPE = re.compile(r"^<!DOCTYPE ([^>]+)>$", re.I)
BOX = re.compile(
    r"^\s*\S+ <([^>]+)> (-?\d+(?:\.\d+)?) (-?\d+(?:\.\d+)?) "
    r"(-?\d+(?:\.\d+)?) (-?\d+(?:\.\d+)?)", re.M
)

CHROMIUM_SCRIPT = r'''<script>
(function () {
  function cleanText(value) {
    return value.replace(/\s+/gu, " ").trim();
  }
  function elementName(node) {
    if (node.namespaceURI === "http://www.w3.org/2000/svg")
      return "svg " + node.localName;
    if (node.namespaceURI === "http://www.w3.org/1998/Math/MathML")
      return "math " + node.localName;
    return node.localName;
  }
  var dom = [];
  var boxes = [];
  function walk(node, depth) {
    if (node.nodeType === Node.DOCUMENT_TYPE_NODE) {
      dom.push({kind: "doctype", depth: depth, name: node.name});
      return;
    }
    if (node.nodeType === Node.TEXT_NODE) {
      var value = cleanText(node.data);
      if (value)
        dom.push({kind: "text", depth: depth, value: value});
      return;
    }
    if (node.nodeType === Node.ELEMENT_NODE) {
      var attrs = Array.from(node.attributes, function (attr) {
        return [attr.name, attr.value];
      });
      attrs.sort(function (a, b) {
        return a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0;
      });
      dom.push({kind: "element", depth: depth,
                name: elementName(node), attrs: attrs});
      var style = getComputedStyle(node);
      if (style.display !== "none" && style.display !== "inline" &&
          style.display !== "contents") {
        var rect = node.getBoundingClientRect();
        boxes.push([node.localName, rect.left, rect.top,
                    rect.width, rect.height]);
      }
    } else if (node.nodeType !== Node.DOCUMENT_NODE) {
      return;
    }
    for (var child = node.firstChild; child; child = child.nextSibling)
      walk(child, node.nodeType === Node.DOCUMENT_NODE ? depth : depth + 1);
  }
  var instrument = document.currentScript;
  if (instrument)
    instrument.remove();
  walk(document, 0);
  var result = JSON.stringify({dom: dom, boxes: boxes});
  var head = document.createElement("head");
  var body = document.createElement("body");
  var pre = document.createElement("pre");
  pre.id = "zstructure";
  pre.textContent = result;
  body.appendChild(pre);
  document.documentElement.replaceChildren(head, body);
})();
</script>'''


def normalized_text(value):
    """Collapse HTML whitespace for a stable meaningful text record."""
    return re.sub(r"\s+", " ", value, flags=re.UNICODE).strip()


def parse_browser_dom(dump):
    """Parse browser's html5lib-style dump into ordered JSON records."""
    starts = list(RECORD_START.finditer(dump))
    raw_records = []
    for index, match in enumerate(starts):
        end = starts[index + 1].start() if index + 1 < len(starts) else len(dump)
        segment = dump[match.end():end]
        spaces = len(segment) - len(segment.lstrip(" "))
        raw_records.append((spaces // 2, segment[spaces:].rstrip("\n")))

    records = []
    element_at_depth = {}
    for depth, value in raw_records:
        if value.startswith("<!--"):
            continue
        match = DOCTYPE.fullmatch(value)
        if match:
            records.append({"kind": "doctype", "depth": depth,
                            "name": match.group(1).lower()})
            continue
        match = ELEMENT.fullmatch(value)
        if match:
            name = match.group(1).lower()
            if value.startswith("<svg "):
                name = "svg " + name
            elif value.startswith("<math "):
                name = "math " + name
            record = {"kind": "element", "depth": depth,
                      "name": name, "attrs": []}
            records.append(record)
            element_at_depth[depth] = record
            for old_depth in [item for item in element_at_depth if item > depth]:
                del element_at_depth[old_depth]
            continue
        match = ATTRIBUTE.fullmatch(value)
        parent = element_at_depth.get(depth - 1)
        if match and parent is not None:
            parent["attrs"].append([match.group(1).lower(), match.group(2)])
            continue
        if value.startswith('"') and value.endswith('"'):
            text = normalized_text(value[1:-1])
            if text:
                records.append({"kind": "text", "depth": depth,
                                "value": text})
    for record in records:
        if record["kind"] == "element":
            record["attrs"].sort(key=lambda item: item[0])
    return records


def browser_output(program, page, width, height, fonts, kind):
    """Run one browser dump with the comparison fonts."""
    command = [
        str(program), "--dump=" + kind,
        "--width=%d" % width, "--height=%d" % height,
        "--font=" + str(fonts / FONT_NAMES[0]),
        "--mono-font=" + str(fonts / FONT_NAMES[1]),
        "--fallback-font=" + str(fonts / FONT_NAMES[2]), str(page),
    ]
    result = subprocess.run(command, capture_output=True, text=True, timeout=180)
    if result.returncode != 0:
        raise RuntimeError("browser --dump=%s failed: %s" %
                           (kind, result.stderr.strip()[-1000:]))
    return result.stdout


def chromium_structure(chromium, page, width, height, font_config, profile):
    """Read Chromium's normalized DOM and block boxes from one local page."""
    source = page.read_text(encoding="utf-8", errors="replace")
    descriptor, temporary = tempfile.mkstemp(
        prefix="zedbsd-structure-", suffix=".html", dir=page.parent
    )
    with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
        stream.write(source)
        stream.write(CHROMIUM_SCRIPT)
    env = dict(os.environ)
    env["FONTCONFIG_FILE"] = str(font_config)
    command = [
        str(chromium), "--headless", "--no-sandbox", "--disable-gpu",
        "--hide-scrollbars", "--allow-file-access-from-files",
        "--force-device-scale-factor=1", "--lang=ja-JP",
        "--user-agent=" + CHROME_USER_AGENT,
        "--user-data-dir=" + str(profile),
        "--window-size=%d,%d" % (width, height), "--dump-dom",
        "file://" + temporary,
    ]
    try:
        result = subprocess.run(command, capture_output=True, text=True,
                                timeout=180, env=env)
    finally:
        Path(temporary).unlink(missing_ok=True)
    if result.returncode != 0:
        raise RuntimeError("Chromium structure dump failed: " + result.stderr[-1000:])
    match = re.search(r'<pre id="zstructure">(.*?)</pre>', result.stdout, re.S)
    if match is None:
        raise RuntimeError("Chromium returned no structure record")
    return json.loads(unescape(match.group(1)))


def parse_browser_boxes(dump):
    """Return browser element border boxes in layout order."""
    return [
        [match.group(1).lower()] +
        [float(match.group(index)) for index in range(2, 6)]
        for match in BOX.finditer(dump)
    ]


def record_line(record):
    """Serialize one normalized DOM record for diffing."""
    return json.dumps(record, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"))


def dom_comparison(browser, chromium, output_prefix):
    """Save normalized DOM trees and return sequence comparison metrics."""
    ours = [record_line(record) for record in browser]
    theirs = [record_line(record) for record in chromium]
    matcher = difflib.SequenceMatcher(a=ours, b=theirs, autojunk=False)
    matched = sum(block.size for block in matcher.get_matching_blocks())
    diff = list(difflib.unified_diff(
        theirs, ours, fromfile="chromium", tofile="browser", lineterm=""
    ))
    browser_path = Path(str(output_prefix) + "-browser.dom.jsonl")
    chromium_path = Path(str(output_prefix) + "-chromium.dom.jsonl")
    diff_path = Path(str(output_prefix) + "-dom.diff")
    browser_path.write_text("\n".join(ours) + "\n", encoding="utf-8")
    chromium_path.write_text("\n".join(theirs) + "\n", encoding="utf-8")
    diff_path.write_text("\n".join(diff) + ("\n" if diff else ""),
                         encoding="utf-8")
    return {
        "browser_records": len(ours), "chromium_records": len(theirs),
        "matched_records": matched,
        "sequence_agreement": 100.0 * 2 * matched /
        max(len(ours) + len(theirs), 1),
        "diff_lines": len(diff),
        "outputs": [browser_path.name, chromium_path.name, diff_path.name],
    }


def box_comparison(browser, chromium, tolerance=1.0):
    """Match boxes by element order and return edge agreement metrics."""
    used = [False] * len(browser)
    matched = 0
    within = 0
    absolute_error = 0.0
    maximum_error = 0.0
    examples = []
    for expected in chromium:
        index = next(
            (candidate for candidate, actual in enumerate(browser)
             if not used[candidate] and actual[0] == expected[0]), None
        )
        if index is None:
            if len(examples) < 20:
                examples.append({"kind": "missing", "chromium": expected})
            continue
        used[index] = True
        matched += 1
        actual = browser[index]
        expected_edges = [expected[1], expected[2], expected[1] + expected[3],
                          expected[2] + expected[4]]
        actual_edges = [actual[1], actual[2], actual[1] + actual[3],
                        actual[2] + actual[4]]
        errors = [abs(left - right)
                  for left, right in zip(actual_edges, expected_edges)]
        absolute_error += sum(errors)
        maximum_error = max(maximum_error, *errors)
        if max(errors) <= tolerance:
            within += 1
        elif len(examples) < 20:
            examples.append({"kind": "different", "chromium": expected,
                             "browser": actual, "edge_errors": errors})
    return {
        "browser_boxes": len(browser), "chromium_boxes": len(chromium),
        "matched_boxes": matched, "within_1px": within,
        "agreement": 100.0 * within / max(len(chromium), 1),
        "mean_edge_error": absolute_error / max(matched * 4, 1),
        "maximum_edge_error": maximum_error, "examples": examples,
    }


def compare_page_structure(program, chromium, page, width, height, fonts,
                           font_config, profile, output_prefix):
    """Compare one page and return its DOM and box metrics."""
    dom_dump = browser_output(program, page, width, height, fonts, "dom")
    layout_dump = browser_output(program, page, width, height, fonts, "layout")
    reference = chromium_structure(
        chromium, page, width, height, font_config, profile
    )
    return {
        "dom": dom_comparison(
            parse_browser_dom(dom_dump), reference["dom"], output_prefix
        ),
        "boxes": box_comparison(
            parse_browser_boxes(layout_dump), reference["boxes"]
        ),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("page", type=Path)
    parser.add_argument("--tag", default="page")
    parser.add_argument("--program", type=Path,
                        default=ROOT / "build/ws074-host/plain/browser")
    parser.add_argument("--chromium", type=Path, required=True)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=900)
    parser.add_argument("--out", type=Path,
                        default=ROOT / "build/ws074-structure")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    fonts = ROOT / "build/ws035-fonts"
    font_config = ROOT / "build/ws074-chrome/fonts.conf"
    result = compare_page_structure(
        args.program.resolve(), args.chromium.resolve(), args.page.resolve(),
        args.width, args.height, fonts, font_config,
        args.out / "chromium-profile", args.out / args.tag,
    )
    print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
