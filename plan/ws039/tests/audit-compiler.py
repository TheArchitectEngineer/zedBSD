# The source audit of the amdrdna shader compiler (src/drivers/gpu/amdrdna/compiler/), with the rules of
# audit-symbols.py over the Mesa files the compiler cites (plan/ws039/amdrdna-compiler-license-audit.md):
# 1. No identifier of the cited sources appears in the code (comments and strings left out), but the general words
#    of the allow lists.
# 2. Every #define block or static const table with a hexadecimal value has the comment before it name its source
#    ("Value referenced:" or "Values referenced:").
# 3. No file says "referenced" in its first 20 lines.
#
# Usage: audit-compiler.py <mesa tree> <compiler directory> <allow list>...
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

import os
import re
import sys

IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]{3,}")
HEX = re.compile(r"\b0[xX][0-9a-fA-F]+")
REFERENCED = re.compile(r"Values? referenced:")

CITED = [
    "src/amd/compiler/aco_opcodes.py",
    "src/amd/compiler/aco_assembler.cpp",
    "src/amd/compiler/aco_insert_NOPs.cpp",
    "src/amd/compiler/aco_ir.h",
    "src/amd/compiler/aco_ir.cpp",
    "src/amd/compiler/aco_validate.cpp",
    "src/amd/compiler/aco_instruction_selection.cpp",
    "src/amd/compiler/aco_lower_to_hw_instr.cpp",
    "src/amd/compiler/aco_insert_waitcnt.cpp",
    "src/amd/compiler/aco_builder_h.py",
    "src/compiler/nir/nir_lower_idiv.c",
    "src/amd/vulkan/radv_shader_args.c",
    "src/amd/vulkan/radv_shader.c",
    "src/amd/vulkan/radv_pipeline_graphics.c",
    "src/amd/vulkan/radv_cmd_buffer.c",
    "src/amd/vulkan/radv_queue.c",
    "src/amd/common/nir/ac_nir_lower_intrinsics_to_args.c",
    "src/amd/common/nir/ac_nir_prerast_utils.c",
    "src/amd/common/nir/ac_nir_lower_tex.c",
    "src/amd/common/ac_gpu_info.c",
    "src/amd/common/ac_descriptors.c",
    "src/amd/common/ac_binary.c",
    "src/amd/common/ac_shader_util.h",
    "src/amd/common/sid.h",
    "src/amd/registers/gfx10.json",
    "src/amd/registers/gfx10-rsrc.json",
    "src/amd/registers/gfx9.json",
]


def strip_c(text):
    """Removes comments and string and character literals from C text."""
    out = []
    index = 0
    length = len(text)
    while index < length:
        if text.startswith("/*", index):
            end = text.find("*/", index + 2)
            if end < 0:
                break
            out.append(" ")
            index = end + 2
        elif text.startswith("//", index):
            end = text.find("\n", index)
            if end < 0:
                break
            index = end
        elif text[index] == '"' or text[index] == "'":
            quote = text[index]
            index += 1
            while index < length and text[index] != quote:
                if text[index] == "\\":
                    index += 1
                index += 1
            index += 1
            out.append(" ")
        else:
            out.append(text[index])
            index += 1
    return "".join(out)


def preceding_comment(lines, index):
    """Returns the comment that ends on the line before index, or None."""
    line = index - 1
    if line < 0 or lines[line].strip() != "*/":
        return None
    start = line
    while start >= 0 and not lines[start].lstrip().startswith("/*"):
        start -= 1
    if start < 0:
        return None
    return "\n".join(lines[start:line + 1])


def check_comments(path, text):
    """Checks the source comment of every hexadecimal block, and the file's head."""
    problems = []
    lines = text.split("\n")
    for number, line in enumerate(lines[:20]):
        if "referenced" in line:
            problems.append("%s:%d: 'referenced' in the file's head" % (path, number + 1))
    index = 0
    while index < len(lines):
        line = lines[index]
        if line.startswith("#define"):
            start = index
            has_hex = False
            while index < len(lines) and (lines[index].startswith("#define") or (index > start and lines[index - 1].endswith("\\"))):
                if HEX.search(lines[index].split("/*")[0]):
                    has_hex = True
                index += 1
            if has_hex:
                comment = preceding_comment(lines, start)
                if comment is None or REFERENCED.search(comment) is None:
                    problems.append("%s:%d: hexadecimal #define block without 'Value(s) referenced:'" % (path, start + 1))
            continue
        if line.startswith("static const") and "=" in line:
            start = index
            body = []
            while index < len(lines):
                body.append(lines[index].split("/*")[0])
                if lines[index].rstrip().endswith(";"):
                    break
                index += 1
            if HEX.search("\n".join(body)):
                comment = preceding_comment(lines, start)
                if comment is None or REFERENCED.search(comment) is None:
                    problems.append("%s:%d: hexadecimal table without 'Value(s) referenced:'" % (path, start + 1))
        index += 1
    return problems


def main():
    mesa, directory = sys.argv[1:3]
    allowed = set()
    for allow_path in sys.argv[3:]:
        with open(allow_path) as stream:
            for line in stream:
                word = line.split("#")[0].strip()
                if word:
                    allowed.add(word)
    cited = set()
    for item in CITED:
        path = os.path.join(mesa, item)
        if not os.path.exists(path):
            print("FAIL audit: %s is missing" % path)
            return 1
        with open(path, errors="replace") as stream:
            cited.update(IDENTIFIER.findall(stream.read()))
    files = sorted(os.path.join(directory, name) for name in os.listdir(directory) if name.endswith(".c") or name.endswith(".h"))
    problems = []
    used_allowed = set()
    for path in files:
        with open(path) as stream:
            text = stream.read()
        for name in sorted(set(IDENTIFIER.findall(strip_c(text)))):
            if name not in cited:
                continue
            if name in allowed:
                used_allowed.add(name)
                continue
            problems.append("%s: '%s' is an identifier of the cited sources" % (path, name))
        problems += check_comments(path, text)
    for problem in problems:
        print("FAIL audit: " + problem)
    if problems:
        print("FAIL audit-compiler (%d)" % len(problems))
        return 1
    print("PASS audit-compiler (%d files, %d cited identifiers, %d allowed words used)" % (len(files), len(cited), len(used_allowed)))
    return 0


sys.exit(main())
