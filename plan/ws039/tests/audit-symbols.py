#!/usr/bin/env python3
#
# The source audit of the amdrdna driver (the WS039 rule on cited sources):
#
# 1. No identifier of the cited Linux and Mesa headers appears in the
#    driver's code (comments and strings are left out).  Short, generic
#    words are allowed by the list in audit-allow.txt.
# 2. Every block of #defines with a hexadecimal value, and every static
#    const table with one, is introduced by a comment that names its
#    source ("Value referenced:" or "Values referenced:").  A comment that
#    only says "derived:" is not enough.
# 3. No file says "referenced" in its first 20 lines.
#
# Usage: audit-symbols.py <linux tree> <mesa tree or -> <driver directory> <allow list>
#
# The shader compiler (compiler/) and the Vulkan executor (vk/) have their
# own audit and are left out.

import os
import re
import sys

IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]{3,}")
HEX = re.compile(r"\b0[xX][0-9a-fA-F]+")
REFERENCED = re.compile(r"Values? referenced:")

LINUX_HEADERS = [
    "drivers/gpu/drm/amd/include/asic_reg",
    "drivers/gpu/drm/amd/include/ivsrcid",
    "drivers/gpu/drm/amd/include/discovery.h",
    "drivers/gpu/drm/amd/include/soc15_hw_ip.h",
    "drivers/gpu/drm/amd/include/soc15_ih_clientid.h",
    "drivers/gpu/drm/amd/include/navi10_enum.h",
    "drivers/gpu/drm/amd/include/navi10_ip_offset.h",
    "drivers/gpu/drm/amd/include/atomfirmware.h",
    "drivers/gpu/drm/amd/include/atombios.h",
    "drivers/gpu/drm/amd/amdgpu/nvd.h",
    "drivers/gpu/drm/amd/amdgpu/psp_gfx_if.h",
    "drivers/gpu/drm/amd/amdgpu/amdgpu_psp.h",
    "drivers/gpu/drm/amd/amdgpu/amdgpu_vm.h",
    "drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.h",
    "drivers/gpu/drm/amd/amdgpu/navi10_sdma_pkt_open.h",
    "drivers/gpu/drm/amd/pm/swsmu/inc/pmfw_if/smu_v11_0_ppsmc.h",
    "drivers/gpu/drm/amd/pm/swsmu/inc/pmfw_if/smu11_driver_if_navi10.h",
    "drivers/gpu/drm/amd/pm/swsmu/inc/smu_v11_0_pptable.h",
    "drivers/gpu/drm/amd/pm/swsmu/inc/smu_v11_0.h",
]

MESA_SOURCES = [
    "src/amd/common/sid.h",
    "src/amd/registers",
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
            out.append(" " * 1)
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


def source_files(path):
    """Lists the files below a path, or the path itself."""
    if os.path.isfile(path):
        return [path]
    found = []
    for root, _, names in os.walk(path):
        for name in names:
            found.append(os.path.join(root, name))
    return found


def source_identifiers(linux, mesa):
    """Collects the identifiers of the cited headers."""
    names = set()
    paths = [os.path.join(linux, item) for item in LINUX_HEADERS]
    if mesa != "-":
        paths += [os.path.join(mesa, item) for item in MESA_SOURCES]
    for path in paths:
        if not os.path.exists(path):
            print("NOTE audit: %s is missing" % path)
            continue
        for name in source_files(path):
            with open(name, errors="replace") as stream:
                names.update(IDENTIFIER.findall(stream.read()))
    return names


def driver_files(directory):
    """Lists the driver's C files, without the shader compiler and the executor."""
    found = []
    for root, dirs, names in os.walk(directory):
        for skipped in ("compiler", "vk"):
            if skipped in dirs:
                dirs.remove(skipped)
        for name in names:
            if name.endswith(".c") or name.endswith(".h"):
                found.append(os.path.join(root, name))
    return sorted(found)


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
                code = lines[index].split("/*")[0]
                if HEX.search(code):
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
    linux, mesa, directory, allow_path = sys.argv[1:5]
    allowed = set()
    with open(allow_path) as stream:
        for line in stream:
            word = line.split("#")[0].strip()
            if word:
                allowed.add(word)
    cited = source_identifiers(linux, mesa)
    problems = []
    used_allowed = set()
    for path in driver_files(directory):
        with open(path) as stream:
            text = stream.read()
        code = strip_c(text)
        for name in sorted(set(IDENTIFIER.findall(code))):
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
        print("FAIL audit-symbols (%d)" % len(problems))
        return 1
    print("PASS audit-symbols (%d files, %d cited identifiers, %d allowed words used)" % (len(driver_files(directory)), len(cited), len(used_allowed)))
    return 0


sys.exit(main())
