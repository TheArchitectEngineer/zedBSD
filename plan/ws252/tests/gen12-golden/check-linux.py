#!/usr/bin/env python3
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# Checks the Gen12 golden's per-platform outputs (run.sh: tgl.txt, rkl.txt, adls.txt, rpls.txt, adlp.txt and their
# .product lines) against what a Linux tree says, read mechanically so that the expectation is not written by hand:
#   - the product's PCI id is in the Linux id list of the platform, and the zedBSD id lists of Rocket Lake,
#     Alder Lake-S and Raptor Lake-S equal Linux's (src/drivers/gpu/i915/intel/pci-ids.h);
#   - the engines the golden built equal the platform's platform_engine_mask in i915_pci.c (the golden's fuses keep
#     every engine);
#   - every block of rcs_engine_wa_init (gt/intel_workarounds.c) whose condition names only platforms is taken by
#     exactly the rows it names: its registers are in the golden's rcs0 engine list for those rows and absent from
#     the others when no other block writes them.
# Usage: check-linux.py <linux tree> <golden output directory>
# Prints one line per finding and "linux check: PASS" or "linux check: FAIL".
import os
import re
import sys

# The golden's rows: the run name, the Linux id macro, the Linux platform test, and the zedBSD id macro to compare
# whole (None for the lists cited from another Linux version).
ROWS = [
    ("tgl", "INTEL_TGL_12_IDS", "IS_TIGERLAKE", None),
    ("rkl", "INTEL_RKL_IDS", "IS_ROCKETLAKE", "I915_PCI_RKL_IDS"),
    ("adls", "INTEL_ADLS_IDS", "IS_ALDERLAKE_S", "I915_PCI_ADLS_IDS"),
    ("rpls", "INTEL_RPLS_IDS", "IS_ALDERLAKE_S", "I915_PCI_RPLS_IDS"),
    ("adlp", "INTEL_ADLP_IDS", "IS_ALDERLAKE_P", None),
]


def read(path):
    with open(path, encoding="utf-8", errors="replace") as handle:
        return handle.read()


def macros(text):
    """Every #define with its body, continuation lines joined."""
    joined = text.replace("\\\n", " ")
    found = {}
    for match in re.finditer(r"^#define\s+(\w+)(\([^)]*\))?\s*(.*)$", joined, re.M):
        found[match.group(1)] = (match.group(2), match.group(3).strip())
    return found


def id_list(defines, name, seen=None):
    """The device ids a Linux id macro expands to, nested lists followed."""
    seen = seen or set()
    if name in seen or name not in defines:
        return set()
    seen.add(name)
    body = defines[name][1]
    ids = {int(value, 16) for value in re.findall(r"INTEL_VGA_DEVICE\((0x[0-9A-Fa-f]+)", body)}
    for nested in re.findall(r"(INTEL_\w+_IDS)\(", body):
        ids |= id_list(defines, nested, seen)
    return ids


def zedbsd_ids(text, name):
    defines = macros(text)
    if name not in defines:
        return None
    return {int(value, 16) for value in re.findall(r"MACRO__\((0x[0-9A-Fa-f]+)\)", defines[name][1])}


def resolve(expression, defines, depth=0):
    """A register expression of the Linux headers as a number (only the forms the rules use)."""
    expression = expression.strip()
    if depth > 8:
        return None
    call = re.fullmatch(r"(\w+)\((.*)\)", expression)
    if call and call.group(1) in ("_MMIO", "MCR_REG"):
        return resolve(call.group(2), defines, depth + 1)
    if call and call.group(1) in defines and defines[call.group(1)][0]:
        params = [p.strip() for p in defines[call.group(1)][0][1:-1].split(",")]
        args = [a.strip() for a in call.group(2).split(",")]
        body = defines[call.group(1)][1]
        for param, arg in zip(params, args):
            body = re.sub(r"\b%s\b" % re.escape(param), "(%s)" % arg, body)
        return resolve(body, defines, depth + 1)
    if re.fullmatch(r"\w+", expression) and expression in defines and not defines[expression][0]:
        return resolve(defines[expression][1], defines, depth + 1)
    words = re.findall(r"\b[A-Za-z_]\w*", expression)
    for word in words:
        if word in defines and not defines[word][0]:
            value = resolve(defines[word][1], defines, depth + 1)
            if value is None:
                return None
            expression = re.sub(r"\b%s\b" % word, str(value), expression)
        elif call is None:
            return None
    try:
        return int(eval(expression, {"__builtins__": {}}, {}))
    except Exception:
        return None


def function_body(text, name):
    start = re.search(r"^%s\([^)]*\)\s*\{" % name, text, re.M)
    if not start:
        return ""
    depth = 0
    for index in range(start.end() - 1, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start.end():index]
    return ""


def platform_blocks(body):
    """The top-level if blocks whose condition is only IS_<platform>(i915) tests joined by ||."""
    blocks = []
    for match in re.finditer(r"\n\tif \((.*?)\) \{", body, re.S):
        condition = " ".join(match.group(1).split())
        terms = [term.strip() for term in condition.split("||")]
        if not all(re.fullmatch(r"IS_\w+\(i915\)", term) for term in terms):
            continue
        depth = 0
        for index in range(match.end() - 1, len(body)):
            if body[index] == "{":
                depth += 1
            elif body[index] == "}":
                depth -= 1
                if depth == 0:
                    break
        inner = body[match.end():index]
        platforms = {term.split("(")[0] for term in terms}
        names = sorted(set(re.findall(r"Wa_\d+", inner)))
        registers = re.findall(r"wa_\w+\(\s*wal\s*,\s*([^,]+?)\s*,", inner)
        blocks.append((platforms, names, registers))
    return blocks


def golden_rows(directory):
    rows = {}
    for row, _, _, _ in ROWS:
        text = read(os.path.join(directory, row + ".txt"))
        product = read(os.path.join(directory, row + ".product"))
        engines = re.findall(r"^engine (\w+) ", text, re.M)
        rcs = re.search(r"^list engine rcs0 \d+\n((?:  .*\n)*)", text, re.M)
        registers = set(int(value, 16) for value in re.findall(r"reg=([0-9a-f]+)", rcs.group(1) if rcs else ""))
        pci = re.search(r"product (0x[0-9a-f]+)", product)
        rows[row] = (engines, registers, int(pci.group(1), 16) if pci else None)
    return rows


def main():
    if len(sys.argv) != 3:
        print("usage: check-linux.py <linux tree> <golden output directory>")
        return 2
    linux, out = sys.argv[1], sys.argv[2]
    driver = os.path.join(linux, "drivers/gpu/drm/i915")
    pci = read(os.path.join(driver, "i915_pci.c"))
    ids = macros(read(os.path.join(linux, "include/drm/i915_pciids.h")))
    headers = {}
    for path in ("i915_reg.h", "gt/intel_gt_regs.h", "gt/intel_engine_regs.h", "i915_reg_defs.h"):
        headers.update(macros(read(os.path.join(driver, path))))
    here = os.path.dirname(os.path.abspath(__file__))
    zedbsd_pci = read(os.path.join(here, "../../../../src/drivers/gpu/i915/intel/pci-ids.h"))
    rows = golden_rows(out)
    failures = 0

    # The ids and the engines of each row.
    infos = dict(re.findall(r"^\s*(INTEL_\w+_IDS)\(&(\w+)\)", pci, re.M))
    for row, macro, _, zedbsd in ROWS:
        engines, _, product = rows[row]
        linux_ids = id_list(ids, macro)
        if product not in linux_ids:
            print("%s: product 0x%04x is not in Linux %s" % (row, product or 0, macro))
            failures += 1
        if zedbsd is not None and zedbsd_ids(zedbsd_pci, zedbsd) != linux_ids:
            print("%s: %s differs from Linux %s" % (row, zedbsd, macro))
            failures += 1
        info = infos.get(macro)
        block = re.search(r"static const struct intel_device_info %s = \{(.*?)\n\};" % info, pci, re.S) if info else None
        mask = re.search(r"\.platform_engine_mask\s*=\s*(.*?),\n", block.group(1) + "\n", re.S) if block else None
        expected = sorted(name.lower() for name in re.findall(r"BIT\((\w+)\)", mask.group(1))) if mask else None
        if expected is None:
            print("%s: no platform_engine_mask in Linux %s" % (row, info))
            failures += 1
        elif sorted(engines) != expected:
            print("%s: engines %s, Linux %s %s" % (row, " ".join(sorted(engines)), info, " ".join(expected)))
            failures += 1
        else:
            print("%s: id 0x%04x and engines %s as Linux %s" % (row, product, " ".join(expected), info))

    # The render rules that only some platforms take.
    body = function_body(read(os.path.join(driver, "gt/intel_workarounds.c")), "rcs_engine_wa_init")
    blocks = platform_blocks(body)
    if not blocks:
        print("no platform blocks found in rcs_engine_wa_init")
        failures += 1
    for platforms, names, registers in blocks:
        taking = [row for row, _, test, _ in ROWS if test in platforms]
        if not taking:
            continue
        for expression in registers:
            register = resolve(expression, headers)
            if register is None:
                print("%s: register %s not resolved" % (",".join(names), expression))
                failures += 1
                continue
            shared = False
            for other in blocks:
                if other[2] is registers:
                    continue
                if register in [resolve(e, headers) for e in other[2]]:
                    shared = True
            for row, _, _, _ in ROWS:
                present = register in rows[row][1]
                wanted = row in taking
                if present != wanted and not (present and shared):
                    print("%s: %s 0x%05x in rcs0 %s, Linux takes it on %s" %
                          (row, ",".join(names), register, "present" if present else "absent", " ".join(taking)))
                    failures += 1
            print("%s (0x%05x): taken by %s" % (",".join(names), register, " ".join(taking)))
    print("linux check: %s" % ("PASS" if failures == 0 else "FAIL"))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
