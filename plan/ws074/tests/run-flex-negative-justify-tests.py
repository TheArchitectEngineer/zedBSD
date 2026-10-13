#!/usr/bin/env python3
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""Check flex main-axis overflow offsets against the CSS alignment fallback."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


VALUES = ("flex-end", "center", "space-between", "space-around", "space-evenly")
EXPECTED = (-5.0, -2.5, 0.0, 0.0, 0.0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", default="build/ws074-host/plain/browser")
    args = parser.parse_args()
    boxes = "".join(
        '<div class="flex" style="justify-content:%s"><div class="item"></div></div>'
        % value for value in VALUES
    )
    source = ('<!doctype html><style>.flex{display:flex;width:30px;margin:0 0 2px 100px}'
              '.item{flex:0 0 35px;height:10px;background:red}</style>' + boxes)
    fonts = Path("userland/desktop/fonts")
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "flex-negative.html"
        path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [args.program, "--dump=layout", "--font=" + str(fonts / "Mahora-Regular.ttf"),
             "--mono-font=" + str(fonts / "JetBrainsMono-Regular.ttf"),
             "--fallback-font=" + str(fonts / "DroidSansFallbackFull.ttf"), str(path)],
            capture_output=True, text=True, timeout=10,
        )
    if result.returncode:
        raise RuntimeError(result.stderr[-1000:])
    containers = re.findall(r"^    block <div> (-?\d+\.\d+)", result.stdout, re.M)
    children = re.findall(r"^      block <div> (-?\d+\.\d+)", result.stdout, re.M)
    if len(containers) != len(VALUES) or len(children) != len(VALUES):
        print("FAIL flex layout count: %d containers, %d children" %
              (len(containers), len(children)))
        return 1
    offsets = [float(child) - float(container)
               for container, child in zip(containers, children)]
    if offsets != list(EXPECTED):
        print("FAIL negative flex offsets: expected %s, got %s" %
              (list(EXPECTED), offsets))
        return 1
    print("negative flex justify-content: %d/%d passed" % (len(VALUES), len(VALUES)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
