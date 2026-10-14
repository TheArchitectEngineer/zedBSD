#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Makes userland/desktop/libbrowser/bind/prelude-source.inc from plan/ws074/phase230/prelude.js (a C string literal)."""
import os

root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
source = open(os.path.join(root, "plan/ws074/phase230/prelude.js"), encoding="utf-8").read()
lines = []
for line in source.split("\n"):
    escaped = line.replace("\\", "\\\\").replace('"', '\\"')
    lines.append('\t"%s\\n"' % escaped)
text = "/* Made by plan/ws074/phase230/make-prelude.py from prelude.js; edit that and run the script. */\n"
text += "static const char prelude_source[] =\n" + "\n".join(lines) + ";\n"
open(os.path.join(root, "userland/desktop/libbrowser/bind/prelude-source.inc"), "w", encoding="utf-8").write(text)
