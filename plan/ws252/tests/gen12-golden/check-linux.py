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
    joined = re.sub(r"/\*.*?\*/", " ", text.replace("\\\n", " "), flags=re.S)
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


# The ICL check: the golden's Ice Lake lists against the lists Linux builds for graphics version 11.
MASK32 = 0xffffffff


def ctz(value):
    count = 0
    while value and not value & 1:
        value >>= 1
        count += 1
    return count


class Evaluator:
    """Evaluates the register and value expressions of the Linux workaround calls."""

    def __init__(self, defines):
        self.defines = defines

    def value(self, expression, depth=0):
        expression = " ".join(expression.split())
        expression = re.sub(r"\b(0x[0-9A-Fa-f]+|\d+)[uUlL]+\b", r"\1", expression)
        names = {}
        for word in set(re.findall(r"\b[A-Za-z_]\w*", expression)):
            names[word] = self.binding(word, depth)
        return int(eval(expression, {"__builtins__": {}}, names)) & MASK32

    def binding(self, word, depth):
        special = {
            "REG_BIT": lambda n: 1 << n,
            "BIT": lambda n: 1 << n,
            "REG_GENMASK": lambda high, low: ((1 << (high - low + 1)) - 1) << low,
            "REG_FIELD_PREP": lambda mask, value: (value << ctz(mask)) & mask,
            "_MASKED_BIT_ENABLE": lambda a: (a << 16) | a,
            "_MASKED_BIT_DISABLE": lambda a: a << 16,
            "_MASKED_FIELD": lambda mask, value: (mask << 16) | value,
            "_MMIO": lambda x: x,
            "MCR_REG": lambda x: x,
        }
        if word in special:
            return special[word]
        if depth > 12 or word not in self.defines:
            raise KeyError(word)
        params, body = self.defines[word]
        if not params:
            return self.value(body, depth + 1)
        names = [name.strip() for name in params[1:-1].split(",")]

        def call(*args):
            text = body
            for name, arg in zip(names, args):
                text = re.sub(r"\b%s\b" % re.escape(name), "(%d)" % arg, text)
            return self.value(text, depth + 1)
        return call


def split_args(text):
    args, depth, current = [], 0, ""
    for char in text:
        if char == "," and depth == 0:
            args.append(current.strip())
            current = ""
            continue
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        current += char
    args.append(current.strip())
    return args


def wa_calls(body):
    """Every wa_* call of a body: (function, arguments without the list)."""
    calls = []
    for match in re.finditer(r"\b(wa_\w+|whitelist_\w+)\(", body):
        start = match.end()
        depth = 1
        index = start
        while depth:
            if body[index] == "(":
                depth += 1
            elif body[index] == ")":
                depth -= 1
            index += 1
        args = split_args(body[start:index - 1])
        calls.append((match.group(1), args[1:]))
    return calls


def block_after(body, condition):
    """The body of the first if block whose condition is the text given."""
    start = body.find("if (%s)" % condition)
    if start < 0:
        return ""
    after = start + len("if (%s)" % condition)
    if not body[after:].lstrip().startswith("{"):
        return body[after:body.index(";", after) + 1]
    index = body.index("{", after)
    depth = 0
    for position in range(index, len(body)):
        if body[position] == "{":
            depth += 1
        elif body[position] == "}":
            depth -= 1
            if depth == 0:
                return body[index + 1:position]
    return ""


def expected_list(calls, evaluator, base=0):
    """The entries Linux adds, merged by register: {(register, mcr): (clr, set)}."""
    entries = {}
    order = []
    for function, args in calls:
        mcr = 1 if "_mcr_" in function or function.startswith("whitelist_mcr") else 0
        reg = evaluator.value(args[0].replace("engine->mmio_base", str(base)))
        name = function.replace("wa_mcr_", "wa_")
        if name in ("wa_write_clr_set",):
            clr, set_ = evaluator.value(args[1]), evaluator.value(args[2])
        elif name == "wa_write":
            clr, set_ = MASK32, evaluator.value(args[1])
        elif name == "wa_write_or":
            clr = set_ = evaluator.value(args[1])
        elif name == "wa_write_clr":
            clr, set_ = evaluator.value(args[1]), 0
        elif name == "wa_masked_en":
            bits = evaluator.value(args[1])
            clr, set_ = 0, (bits << 16) | bits
        elif name == "wa_masked_dis":
            clr, set_ = 0, evaluator.value(args[1]) << 16
        elif name == "wa_masked_field_set":
            clr, set_ = 0, (evaluator.value(args[1]) << 16) | evaluator.value(args[2])
        elif name == "wa_add":
            clr, set_ = evaluator.value(args[1]), evaluator.value(args[2])
        elif name.startswith("whitelist_"):
            flags = evaluator.value(args[1]) if name.endswith("_ext") else 0
            reg |= flags
            clr = set_ = 0
        else:
            raise ValueError(function)
        key = (reg & MASK32, mcr)
        if key in entries:
            old_clr, old_set = entries[key]
            entries[key] = (old_clr | clr, ((old_set & ~clr) | set_) & MASK32)
        else:
            entries[key] = (clr & MASK32, set_ & MASK32)
            order.append(key)
    return entries


def golden_lists(text):
    """The golden's lists in order: [(kind, name, {(register, mcr): (clr, set)})]."""
    lists = []
    current = None
    for line in text.splitlines():
        head = re.match(r"^list (\w+) (\S+) \d+$", line)
        if head:
            current = {}
            lists.append((head.group(1), head.group(2), current))
            continue
        entry = re.match(r"^  reg=([0-9a-f]+) clr=([0-9a-f]+) set=([0-9a-f]+) rmask=[0-9a-f]+ kind=\d+ mcr=(\d)", line)
        if entry and current is not None:
            current[(int(entry.group(1), 16), int(entry.group(4)))] = (int(entry.group(2), 16), int(entry.group(3), 16))
            continue
        current = None
    return lists


def compare(label, expected, actual, skip=(), by_register=False):
    failures = 0
    if by_register:
        expected = {(key[0], 0): value for key, value in expected.items()}
        actual = {(key[0], 0): value for key, value in actual.items()}
    for key in sorted(set(expected) | set(actual)):
        if key[0] in skip:
            continue
        if expected.get(key) != actual.get(key):
            print("icl %s: register 0x%05x mcr=%d: Linux %s, golden %s" % (label, key[0], key[1], expected.get(key), actual.get(key)))
            failures += 1
    if failures == 0:
        print("icl %s: %d entries as Linux" % (label, len([k for k in expected if k[0] not in skip])))
    return failures


def check_icl(driver, out):
    text = read(os.path.join(out, "icl.txt"))
    defines = {}
    for path in ("i915_reg.h", "i915_reg_defs.h", "gt/intel_gt_regs.h", "gt/intel_engine_regs.h"):
        defines.update(macros(read(os.path.join(driver, path))))
    evaluator = Evaluator(defines)
    source = read(os.path.join(driver, "gt/intel_workarounds.c"))
    lists = golden_lists(text)
    by_name = {}
    engine = None
    for kind, name, entries in lists:
        if kind == "engine":
            engine = name
            by_name[("engine", name)] = entries
        elif kind == "gt":
            by_name[("gt", "gt")] = entries
        elif engine is not None:
            by_name[(kind, engine)] = entries
    failures = 0

    # The GT list: icl_gt_workarounds_init; the steering selector is checked apart (it depends on the fuses).
    gt = expected_list(wa_calls(function_body(source, "icl_gt_workarounds_init")), evaluator)
    failures += compare("gt", gt, by_name.get(("gt", "gt"), {}), skip=(0x0fdc,))
    if (0x0fdc, 0) not in by_name.get(("gt", "gt"), {}):
        print("icl gt: no steering entry")
        failures += 1

    # The render engine: the graphics version 11 render-compute and render blocks, and the per-context preemption.
    general = block_after(function_body(source, "general_render_compute_wa_init"), "GRAPHICS_VER(i915) >= 11")
    rcs_body = function_body(source, "rcs_engine_wa_init")
    rcs = block_after(rcs_body, "GRAPHICS_VER(i915) == 11") + block_after(rcs_body, "GRAPHICS_VER(i915) >= 9")
    failures += compare("rcs0 engine", expected_list(wa_calls(general + rcs), evaluator), by_name.get(("engine", "rcs0"), {}))

    # The render context: icl_ctx_workarounds_init.
    ctx_body = re.search(r"static void icl_ctx_workarounds_init\(.*?\)\s*\{(.*?)\n\}", source, re.S).group(1)
    failures += compare("rcs0 context", expected_list(wa_calls(ctx_body), evaluator), by_name.get(("ctx", "rcs0"), {}))

    # The whitelists: icl_whitelist_build, the context timestamp of every engine but render (a whitelist slot names the
    # register only, so its multicast flag is not compared).
    white = re.search(r"static void icl_whitelist_build\(.*?\)\s*\{(.*?)\n\}", source, re.S).group(1)
    render = white[white.index("case RENDER_CLASS:"):white.index("case VIDEO_DECODE_CLASS:")]
    video = white[white.index("case VIDEO_DECODE_CLASS:"):white.index("default:")]
    failures += compare("rcs0 whitelist", expected_list(wa_calls(render), evaluator), by_name.get(("whitelist", "rcs0"), {}), by_register=True)
    timestamp = [("whitelist_reg_ext", ["RING_CTX_TIMESTAMP(engine->mmio_base)", "RING_FORCE_TO_NONPRIV_ACCESS_RD"])]
    for name, base in (("bcs0", 0x22000), ("vcs0", 0x1c0000), ("vcs2", 0x1d0000), ("vecs0", 0x1c8000)):
        calls = list(timestamp)
        if name.startswith("vcs"):
            calls += wa_calls(video)
        failures += compare(name + " whitelist", expected_list(calls, evaluator, base), by_name.get(("whitelist", name), {}), by_register=True)
    return failures


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
    if os.path.exists(os.path.join(out, "icl.txt")):
        failures += check_icl(driver, out)
    print("linux check: %s" % ("PASS" if failures == 0 else "FAIL"))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
