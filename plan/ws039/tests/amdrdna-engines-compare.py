#!/usr/bin/env python3
#
# Compares what stage A5 of the amdrdna driver uses (amdrdna-engines-test
# --layout) with the Linux v6.19 sources: the queue images' word positions
# with struct v10_compute_mqd and struct v10_gfx_mqd, the registers' windows
# and offsets with the GC and NBIO headers, and the settings tables with
# gfx_v10_0.c's golden_settings_gc_10_1 and sdma_v5_0.c's
# golden_settings_sdma_5 and golden_settings_sdma_nv10.
#
# Usage: amdrdna-engines-compare.py <linux drivers/gpu/drm/amd directory> < layout
#
# The Linux names stay in this test; the driver has its own.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

import os
import re
import sys


def defines(path):
    table = {}
    with open(path) as handle:
        for line in handle:
            match = re.match(r"#define\s+(\w+)\s+(0x[0-9a-fA-F]+|\d+)", line)
            if match:
                table[match.group(1)] = int(match.group(2), 0)
    return table


def struct_words(path, name):
    text = open(path).read()
    body = re.search(r"struct " + name + r" \{(.*?)\n\};", text, re.S).group(1)
    words = {}
    index = 0
    for line in body.split("\n"):
        field = re.match(r"\s*uint32_t\s+(\w+)(\[(\d+)\])?;", line)
        if field:
            words[field.group(1)] = index
            index += int(field.group(3)) if field.group(3) else 1
    return words


def golden(path, name, gc):
    text = open(path).read()
    body = re.search(r"static const struct soc15_reg_golden " + name + r"\[\] = \{(.*?)\n\};", text, re.S).group(1)
    rows = []
    for register, mask, value in re.findall(r"SOC15_REG_GOLDEN_VALUE\(GC, 0, mm(\w+), (0x[0-9a-f]+), (0x[0-9a-f]+)\)", body):
        rows.append((gc["mm" + register + "_BASE_IDX"], gc["mm" + register], int(mask, 16), int(value, 16)))
    return rows


def main():
    amd = sys.argv[1]
    gc = defines(os.path.join(amd, "include/asic_reg/gc/gc_10_1_0_offset.h"))
    nbio = defines(os.path.join(amd, "include/asic_reg/nbio/nbio_2_3_offset.h"))
    compute = struct_words(os.path.join(amd, "include/v10_structs.h"), "v10_compute_mqd")
    graphics = struct_words(os.path.join(amd, "include/v10_structs.h"), "v10_gfx_mqd")
    expected = {
        "graphics": golden(os.path.join(amd, "amdgpu/gfx_v10_0.c"), "golden_settings_gc_10_1", gc),
        "copy": golden(os.path.join(amd, "amdgpu/sdma_v5_0.c"), "golden_settings_sdma_5", gc)
        + golden(os.path.join(amd, "amdgpu/sdma_v5_0.c"), "golden_settings_sdma_nv10", gc),
    }
    seen = {"graphics": [], "copy": []}
    failures = 0
    checked = 0
    for line in sys.stdin:
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "setting":
            seen[parts[1]].append((int(parts[2]), int(parts[3], 16), int(parts[4], 16), int(parts[5], 16)))
            continue
        if parts[0] in ("compute", "graphics"):
            fields = compute if parts[0] == "compute" else graphics
            want = fields.get(parts[1])
            got = int(parts[2])
            checked += 1
            if want != got:
                print("FAIL image %s %s: driver %d, Linux %s" % (parts[0], parts[1], got, want))
                failures += 1
            continue
        table = gc if parts[0] == "gc" else nbio
        window = int(parts[1])
        offset = int(parts[2], 16)
        name = parts[3]
        checked += 1
        if table.get("mm" + name) != offset or table.get("mm" + name + "_BASE_IDX") != window:
            print("FAIL register %s: driver window %d offset 0x%x, Linux %s %s"
                  % (name, window, offset, table.get("mm" + name + "_BASE_IDX"), table.get("mm" + name)))
            failures += 1
    for kind in ("graphics", "copy"):
        checked += 1
        if seen[kind] != expected[kind]:
            print("FAIL settings %s: the driver's table differs from Linux's (%d and %d rows)"
                  % (kind, len(seen[kind]), len(expected[kind])))
            failures += 1
    if failures:
        print("FAIL amdrdna-engines-compare: %d of %d" % (failures, checked))
        return 1
    print("PASS amdrdna-engines-compare: %d image words, registers and tables" % checked)
    return 0


sys.exit(main())
