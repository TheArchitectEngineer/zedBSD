#!/usr/bin/env python3
# The GSI1 compiler's host test, driven by gsi1-host-test.sh.
#
#   gsi1-host-test.py HARNESS AMATERRACE_TOOLS SPV_DIR OUT_DIR
#
# 1. The encoder's words of known instructions (HARNESS encode) are what
#    the copied amaterrace-gpu tools/gsi1asm.py makes of the same assembly, and
#    tools/gsi1dis.py disassembles each to text that assembles back to it.
# 2. Every module is compiled; its code is GSI1 code alone (no header: two
#    words to an instruction), disassembles without an undefined or
#    inexpressible word and reassembles to the same words (the round trip;
#    the sampler slot of the texture forms, word1 [9:6], which the tools
#    predate, is checked and cleared first), and the record and the code
#    have what the case expects (hand-written from docs/ghci.md d9252e4 and
#    the driver's convention: the slots, the resource table, the bank words,
#    the stage I/O).
# 3. The modules the model can run (compute, vertex, fragment without
#    textures) run on tools/gsi1model.py (through gsi1-model-run.py) with the
#    memory, registers and primitive table the harness laid out from the
#    record, and leave what the harness's IR reference left (floats to a
#    tolerance).
# 4. A refusal case must be refused with its reason.
# Prints PASS/FAIL per case and a count; exit status 1 on any failure.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import os
import re
import struct
import subprocess
import sys

HARNESS, TOOLS, SPV, OUT = sys.argv[1:5]
sys.path.insert(0, TOOLS)
import gsi1asm as A  # noqa: E402
import gsi1dis as D  # noqa: E402

TIMEOUT = 300
HERE = os.path.dirname(os.path.abspath(__file__))
MAGIC = 0x47534931        # "GSI1": a pre-1.0 container header's first word, which a 1.0 program has not
failures = []
passes = []


def fail(name, why):
    failures.append(name)
    print("FAIL %s: %s" % (name, why))


def ok(name, what):
    passes.append(name)
    print("PASS %s: %s" % (name, what))


def run(cmd):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=TIMEOUT)
    return r.returncode, r.stdout.decode(errors="replace")


def f32(w):
    return struct.unpack("<f", struct.pack("<I", w & 0xFFFFFFFF))[0]


def close(a, b, tol):
    if a == b:
        return True
    fa, fb = f32(a), f32(b)
    if fa != fa and fb != fb:
        return True         # NaN either way (GSI1 makes every NaN 0x7FC00000)
    if fa != fa or fb != fb:
        return False
    if abs(fa) < 1e-30 and abs(fb) < 1e-30:
        return True
    return abs(fa - fb) <= tol * max(1.0, abs(fa), abs(fb))


def load_hex(path):
    with open(path) as f:
        return [int(x, 16) for x in f.read().split()]


def is_texture_r(w):
    """A texl, texb or texf (custom-1, funct3 0-2): word1 [9:6] is its sampler slot."""
    return (w & 0x7F) == 0x2B and ((w >> 12) & 7) <= 2


def read_record(prefix):
    record = {}
    with open(prefix + ".record") as f:
        for ln in f:
            key, value = ln.split()
            record[key] = value
    return record


def round_trip(name, prefix):
    """The code disassembles and reassembles to itself; returns (.s path, words, lines, slots) or None."""
    with open(prefix + ".bin", "rb") as f:
        w32 = D.read_bin(f.read())
    if w32 and w32[0] == MAGIC:
        fail(name, "the code starts with a container header")
        return None
    record = read_record(prefix)
    if len(w32) != 2 * int(record["instructions"]) or len(w32) != int(record["words"]):
        fail(name, "%d words for %s instructions: not the code alone" % (len(w32), record["instructions"]))
        return None
    slots = set()
    for i in range(0, len(w32), 2):
        if is_texture_r(w32[i]):
            slots.add(((w32[i + 1] >> 17) & 15, (w32[i + 1] >> 6) & 15))
            w32[i + 1] &= ~(15 << 6)
    head, words = D.from_words32(w32)
    lines = D.disassemble(words)
    bad = [ln for ln in lines if ".inst" in ln]
    if bad:
        fail(name, "the disassembly has words gsi1asm cannot write: %s" % bad[:3])
        return None
    again = A.encode("\n".join(lines))
    if again != words:
        fail(name, "the reassembled code differs from the compiled code")
        return None
    with open(prefix + ".s", "w") as f:
        f.write("\n".join(head + lines) + "\n")
    return prefix + ".s", w32, lines, slots, record


# 1. The encoder.
enc = os.path.join(OUT, "encode.txt")
code, text = run([HARNESS, "encode", enc])
if code != 0:
    fail("encode", text)
else:
    bad = 0
    count = 0
    with open(enc) as f:
        for ln in f:
            hexw, asm = ln.rstrip("\n").split("\t")
            w = int(hexw, 16)
            count += 1
            try:
                want = A.encode(asm)[0]
            except SystemExit as ex:
                bad += 1
                print("  %s: gsi1asm refuses: %s" % (asm, ex))
                continue
            if want != w:
                bad += 1
                print("  %s: encoder %016x, gsi1asm %016x" % (asm, w, want))
                continue
            dis = D.disassemble([w], use_labels=False)[0].strip()
            if ".inst" in dis or A.encode(dis)[0] != w:
                bad += 1
                print("  %s: gsi1dis gives %r" % (asm, dis))
    if bad:
        fail("encode", "%d of %d words differ" % (bad, count))
    else:
        ok("encode", "%d known words match gsi1asm and round-trip through gsi1dis" % count)

# 2-4. The modules: (name, mode, harness arguments, options, tolerance, mnemonics the code must
# have, or with "!" must not have).
CASES = [
    ("integer.comp", "compute", ["64", "4"], "", 0, ["div", "remu", "srai", "slt"]),
    ("float.comp", "compute", ["32", "1"], "floats", 2e-6, ["fround.s", "fract", "fcvt.w.s"]),
    ("trig.comp", "compute", ["32", "1"], "floats", 2e-4, ["rcp", "log2", "sin", "fsqrt.s"]),
    ("loop.comp", "compute", ["64", "1"], "", 0, ["exec.save", "exec.and", "bexnz", "exec.set"]),
    ("control.comp", "compute", ["64", "1"], "", 0, ["sw"]),
    ("atomic.comp", "compute", ["32", "1"], "", 0, ["atom.add", "atom.cmpxchg", "atom.xchg"]),
    ("shared.comp", "compute", ["128", "2"], "", 0, ["lds", "sts", "bar"]),
    ("sharedatomic.comp", "compute", ["64", "1"], "", 0,
     ["atoms.add", "atoms.umax", "atoms.umin", "atoms.or", "atoms.and", "atoms.xor",
      "atoms.smax", "atoms.smin", "atoms.xchg", "atoms.cmpxchg"]),
    ("uniform.comp", "compute", ["32", "1"], "", 0, ["lw"]),
    ("uniform.comp:robust", "compute", ["32", "1"], "robust", 0, ["minu"]),
    ("uniform.comp:dynamic", "compute", ["32", "1"], "dyn=0.0,dyn=0.1", 0, ["lw", "sw"]),
    ("push.comp", "compute", ["32", "1"], "", 0, ["lwc"]),
    ("groups.comp", "compute", ["32", "4"], "", 0, []),
    ("half.comp", "compute", ["32", "1"], "floats", 1e-6, ["packh", "unpackh.lo"]),
    ("image.comp", "compute", ["64", "1"], "", 1e-6, ["lw", "sw"]),
    ("basic.frag", "fragment", [], "", 2e-6, ["lda", "fmadd.s", "rcp"]),
    ("basic.frag:line", "fragment", [], "prim=line", 2e-6, ["lda", "rcp"]),
    ("basic.frag:point", "fragment", [], "prim=point", 2e-6, ["lda", "rcp"]),
    ("discard.frag", "fragment", [], "cover=ffff7f3f", 2e-6, ["kill", "exp"]),
    ("samplemask.frag", "fragment", [], "samples=4,mask", 2e-6, ["exp"]),
    ("samplemask.frag:1x", "fragment", [], "mask", 2e-6, ["exp"]),
    ("derivative.frag", "fragment", [], "", 2e-5, ["qdx", "qdy", "qbcast"]),
    ("subpass.frag", "fragment", [], "ia0=37", 2e-6, ["fcvt.w.s", "lw"]),
    ("store.frag", "fragment", [], "cover=f3ff3fff", 0, ["sw"]),
    ("basic.vert", "vertex", [], "", 2e-6, ["exp"]),
    ("image.vert", "vertex", [], "", 2e-6, ["lw"]),
    ("attrib.vert", "vertex", [], "", 2e-6, ["exp"]),
    ("attrib.vert:map", "vertex", [], "amap=2.2.3.1", 2e-6, ["exp"]),
    ("attrib60.vert:map", "vertex", [], "amap=4.4.4.4.4.4.4.4.4.4.4.4.4.4.4.0", 2e-6, ["exp"]),
    ("varying64.vert", "vertex", [], "", 2e-6, ["exp"]),
    ("varying64.frag", "fragment", [], "", 2e-6, ["lda"]),
    ("varying64all.vert", "vertex", [], "", 2e-6, ["exp"]),
    ("varyingloop.vert", "vertex", [], "", 2e-6, ["exp", "exec.save", "bexnz"]),
    ("late.vert", "vertex", [], "", 2e-6, ["exp", "!sw.x"]),
    ("spill.vert", "vertex", [], "regs=32", 2e-6, ["exp", "sw.x", "lw"]),
    ("spill.comp", "compute", ["64", "2"], "regs=32", 0, ["sw.x", "lw", "bexnz"]),
    ("spill.frag", "fragment", [], "cover=ffff7f3f,floats,regs=32", 2e-5, ["sw.x", "lw", "qdx", "qbcast"]),
    ("mrt.frag", "compile-fragment", [], "", 0, ["exp"]),
    ("mrt8.frag", "compile-fragment", [], "", 0, ["exp"]),
    ("dual.frag", "compile-fragment", [], "", 0, ["exp"]),
    ("resources.frag", "compile-fragment", [], "", 0, ["texb", "lwc", "lw", "sw"]),
    ("texture.frag", "compile-fragment", [], "", 0,
     ["texb", "texl", "texg.b", "texb.c", "texb.3", "texf", "texsz", "texsz.s", "texsz.l"]),
    ("msarray.frag", "compile-fragment", [], "", 0, ["texf.3"]),
    ("texelbuffer.comp", "compute", ["32", "1"], "", 0, ["lw", "sw", "divu"]),
    ("imageatomic.comp", "compute", ["32", "1"], "", 0, ["atom.add", "atom.umax", "atom.cmpxchg"]),
    ("cubegrad.frag", "compile-fragment", [], "", 0, ["texl.3", "log2", "texsz.d", "exec.save"]),
    ("shared.vert+shared.frag", "pipeline", [], "", 0, ["texb"]),
]

# What the record and the code of a case must have, written from docs/ghci.md d9252e4 and the
# driver's convention (design section 11.4) by hand: "record" values, regular expressions the
# disassembly must ("re") or must not ("not") match, and the (texture slot, sampler slot) pairs
# of the texture instructions ("slots").  Kinds: texture 1 image, 2 texel buffer; sampler 1;
# resource 1 uniform buffer, 2 storage buffer, 3 uniform texel buffer, 4 storage texel buffer,
# 5 storage image, 6 input attachment (gsi1-compile.h).  Bank: c28 is set 2's address, c30 set 3's;
# push bytes 96-127 are c32-c39 (lwc 32-39); the dynamic offsets c40 on.  Vertex: operands 120, 121
# ({o120} and {o121}: the copied disassembler's names of them, which predate vtx_idx and inst_idx).
# Export targets: COLOR(k, c) 0x100 + 16 k + c, COLOR0_SRC1 0x108 + c; lda word 3 of vertex v is
# immediate 12 + v; the disassembler writes numbers below 4096 in decimal.
EXPECT = {
    "uniform.comp": {"record": {"resources": "2", "resource_bytes": "32", "push_bytes": "32",
                                "dynamic_used": "0", "textures": "0"},
                     "re": [r"lw r\d+, 0\(c28\)", r"lw r\d+, 16\(c28\)"], "not": [r"lwc"]},
    "uniform.comp:dynamic": {"record": {"dynamic_used": "0x3"},
                             "re": [r"lwc r\d+, 40\(zero\)", r"lwc r\d+, 41\(zero\)"]},
    "push.comp": {"record": {"push_bytes": "128"},
                  "re": [r"c0\b", r"c23\b", r"lwc r\d+, 32\(zero\)", r"lwc r\d+, 39\(zero\)",
                         r"lwc r\d+, 35\(zero\)"],
                  "not": [r"c24\b", r"c25\b", r"c26\b", r"c27\b", r"\(c30\)"]},
    "texelbuffer.comp": {"record": {"resource0": "4.0.0.0", "resource_bytes": "16"},
                         "re": [r"lw r\d+, 8\(c28\)", r"lw r\d+, 12\(c28\)"]},
    "image.comp": {"record": {"resource0": "5.0.0.0", "resource1": "5.0.1.32", "resource2": "5.0.2.64",
                              "resource_bytes": "96"},
                   "re": [r"lw r\d+, 8\(c28\)", r"lw r\d+, 44\(c28\)", r"lw r\d+, 48\(c28\)", r"lw r\d+, 52\(c28\)"]},
    "subpass.frag": {"record": {"resource0": "6.0.0.0", "resource_bytes": "32"},
                     "re": [r"lw r\d+, 0\(c28\)", r"lw r\d+, 16\(c28\)"]},
    "spill.comp": {"record": {"scratch_bytes": "!0"}, "re": [r"lw r\d+, 16\(c30\)", r"warp"]},
    "spill.vert": {"re": [r"lw r\d+, 0\(c30\)"]},
    "spill.frag": {"re": [r"lw r\d+, 8\(c30\)"]},
    "basic.vert": {"record": {"varying_count": "2", "attribute_registers": "7"}, "used_regs_below": 62,
                   "re": [r"{o120}", r"{o121}", r"exp r\d+, 3\b", r"exp r\d+, 20\b", r"exp r\d+, 21\b"],
                   "not": [r"\br60\b", r"\br61\b", r"exp r\d+, 4\b"]},
    "attrib.vert": {"record": {"attribute_registers": "13", "varying_count": "2"}},
    "attrib.vert:map": {"record": {"attribute_registers": "8"}},
    "attrib60.vert:map": {"record": {"attribute_registers": "60"}},
    "basic.frag": {"record": {"varying_count": "3", "flags": "0", "color_outputs": "0x1"},
                   "re": [r"lda r\d+, 12\b", r"lda r\d+, 13\b", r"lda r\d+, 14\b", r"lda r\d+, 20\b",
                          r"exp r\d+, 256\b", r"exp r\d+, 261\b"], "lda_vertices": {0, 1, 2}},
    "basic.frag:line": {"re": [r"lda r\d+, 12\b", r"lda r\d+, 13\b"], "lda_vertices": {0, 1}},
    "basic.frag:point": {"re": [r"lda r\d+, 12\b", r"lda r\d+, 16\b"], "lda_vertices": {0}},
    "discard.frag": {"record": {"flags": "6"}},
    "samplemask.frag": {"record": {"flags": "1"}},
    "mrt.frag": {"record": {"color_outputs": "0xf"}},
    "mrt8.frag": {"record": {"color_outputs": "0xff"},
                  "re": [r"exp r\d+, %d\b" % (0x100 + 16 * k + c) for k in range(8) for c in range(4)]},
    "dual.frag": {"record": {"color_outputs": "0x101"},
                  "re": [r"exp r\d+, %d\b" % t for t in (256, 257, 258, 259, 264, 265, 266, 267)]},
    "resources.frag": {"record": {"textures": "1", "texture0": "1.2.3", "samplers": "1", "sampler0": "1.2.3",
                                  "resources": "2", "resource_bytes": "32", "push_bytes": "128"},
                       "sets": {"resource": {"1.1.0", "2.0.5"}},
                       "re": [r"lwc r\d+, 36\(zero\)", r"lwc r\d+, 39\(zero\)", r"\(c28\)"],
                       "slots": {(0, 0)}},
    "texture.frag": {"record": {"textures": "!0"}},
    "msarray.frag": {"record": {"texture0": "1.0.0", "texture1": "2.0.1", "samplers": "1",
                                "resource0": "3.0.1.0"},
                     "re": [r"divu", r"lw r\d+, 8\(c28\)"]},
    "shared.vert+shared.frag-vs": {"record": {"textures": "1", "texture0": "1.0.0", "sampler0": "1.0.0",
                                              "resources": "1", "resource0": "1.0.1.0"},
                                   "slots": {(0, 0)}},
    "shared.vert+shared.frag-fs": {"record": {"textures": "2", "texture0": "1.0.0", "texture1": "1.0.2",
                                              "samplers": "2", "sampler0": "1.0.0", "sampler1": "1.0.2",
                                              "resources": "2", "resource0": "1.0.1.0", "resource1": "2.1.0.16",
                                              "resource_bytes": "32"},
                                   "slots": {(0, 0), (1, 1)}},
}

# Modules the compiler must refuse, with a word of the reason.
REFUSALS = [
    ("attrib60.vert", "vertex", "", "60 registers"),
    ("tex17.frag", "fragment", "", "16 texture or sampler slots"),
]


def check_expect(name, lines, slots, record):
    """The record's values, the code's patterns and the texture slots the case expects."""
    want = EXPECT.get(name)
    if want is None:
        return None
    text = "\n".join(lines)
    names = {"{o120}": re.escape(D.reg_name(120)), "{o121}": re.escape(D.reg_name(121))}
    for key, value in want.get("record", {}).items():
        got = record.get(key)
        if value.startswith("!"):
            if got is None or got == value[1:]:
                return "record %s is %s" % (key, got)
        elif got != value:
            return "record %s is %s, not %s" % (key, got, value)
    for prefix, values in want.get("sets", {}).items():
        got = set(".".join(v.split(".")[:3]) for k, v in record.items() if re.fullmatch(prefix + r"\d+", k))
        if got != values:
            return "the %s entries are %s, not %s" % (prefix, sorted(got), sorted(values))
    for pattern in want.get("re", []):
        for key, value in names.items():
            pattern = pattern.replace(key, r"\b" + value + r"\b")
        if not re.search(pattern, text):
            return "the code has no %r" % pattern
    for pattern in want.get("not", []):
        if re.search(pattern, text):
            return "the code has %r" % pattern
    if "used_regs_below" in want and int(record["used_regs"]) >= want["used_regs_below"]:
        return "used_regs %s: the fetcher would also write r60 and r61" % record["used_regs"]
    if "lda_vertices" in want:
        got = set(int(ln.split(",")[1]) & 3 for ln in lines if ln.startswith("\tlda "))
        if got != want["lda_vertices"]:
            return "lda reads vertices %s, not %s" % (sorted(got), sorted(want["lda_vertices"]))
    if "slots" in want and slots != want["slots"]:
        return "texture and sampler slots %s, not %s" % (sorted(slots), sorted(want["slots"]))
    return None


def check_module(name, prefix, mnemonics):
    """Round trip, mnemonics and expectations of one compiled program; returns the round trip or None."""
    rt = round_trip(name, prefix)
    if rt is None:
        return None
    asm, w32, lines, slots, record = rt
    used = set(ln.split()[0] for ln in lines if ln.startswith("\t"))
    if not 1 <= int(record["used_regs"]) <= 63:
        fail(name, "used_regs %s" % record["used_regs"])
        return None
    missing = [m for m in mnemonics if not m.startswith("!") and m not in used]
    missing += [m for m in mnemonics if m.startswith("!") and m[1:] in used]
    if missing:
        fail(name, "the code lacks %s" % missing)
        return None
    why = check_expect(name, lines, slots, record)
    if why is not None:
        fail(name, why)
        return None
    return rt


for name, mode, extra, options, tol, mnemonics in CASES:
    spv_name = name.split(":")[0]
    prefix = os.path.join(OUT, name.replace(":", "-"))
    spv = os.path.join(SPV, spv_name + ".spv")
    if mode == "pipeline":
        vs, fs = spv_name.split("+")
        code, text = run([HARNESS, "pipeline", os.path.join(SPV, vs + ".spv"), os.path.join(SPV, fs + ".spv"),
                          prefix, options])
        if code != 0:
            fail(name, text.strip())
            continue
        rts = [check_module(name + suffix, prefix + suffix, [] if suffix == "-vs" else mnemonics)
               for suffix in ("-vs", "-fs")]
        if None not in rts:
            ok(name, "the fragment program extends the vertex program's slots and resource table")
        continue
    if mode.startswith("compile-"):
        stage = mode.split("-", 1)[1]
        code, text = run([HARNESS, "compile", stage, spv, prefix, options])
    else:
        code, text = run([HARNESS, mode, spv, prefix] + extra + ["1"] + [options])
    if code != 0:
        fail(name, text.strip())
        continue
    rt = check_module(name, prefix, mnemonics)
    if rt is None:
        continue
    asm, w32, lines, slots, record = rt
    if mode.startswith("compile-"):
        ok(name, "%d instructions, round trip, record and code as expected" % (len(w32) // 2))
        continue
    with open(prefix + ".args") as f:
        margs = f.read().split()
    model = [sys.executable, "-I", os.path.join(HERE, "gsi1-model-run.py"), TOOLS, asm] + margs + \
        ["--data", prefix + ".data.hex", "-o", prefix + ".regs", "--mem-out", prefix + ".mem"]
    if mode == "fragment":
        model += ["--warps", prefix + ".warps.hex", "--init", prefix + ".init.hex",
                  "--prims", prefix + ".prims.hex", "--exp-out", prefix + ".exp",
                  "--ms-out", prefix + ".ms"]
    if mode == "vertex":
        model += ["--warps", prefix + ".warps.hex", "--init", prefix + ".init.hex",
                  "--pt-out", prefix + ".pt"]
    code, text = run(model)
    if code != 0:
        fail(name, "the model stops: " + text.strip()[-400:])
        continue
    bad = []
    if mode in ("compute", "fragment"):
        mem = load_hex(prefix + ".mem")
        want = load_hex(prefix + ".expect.hex")
        cmp = load_hex(prefix + ".compare")
        for i, c in enumerate(cmp):
            if c == 0:
                continue
            t = tol if c == 2 or tol else 0
            if (t == 0 and mem[i] != want[i]) or (t and not close(mem[i], want[i], t)):
                bad.append("word %d: model %08x reference %08x" % (i, mem[i], want[i]))
    if mode == "fragment":
        got = load_hex(prefix + ".exp")
        want = load_hex(prefix + ".expect-exp.hex")
        if len(got) != len(want):
            bad.append("%d export words, the reference %d" % (len(got), len(want)))
        else:
            for i, (g, w) in enumerate(zip(got, want)):
                q = (i - 1) % 22
                exact = i == 0 or q in (0, 1)
                if (exact and g != w) or (not exact and not close(g, w, tol)):
                    bad.append("export word %d: model %08x reference %08x" % (i, g, w))
        got = load_hex(prefix + ".ms")
        want = load_hex(prefix + ".expect-ms.hex")
        if got != want:
            bad.append("sample masks: model %s reference %s" % (got, want))
    if mode == "vertex":
        with open(prefix + ".pt") as f:
            got = f.read().split()
        with open(prefix + ".expect-pt.hex") as f:
            want = f.read().split()
        for i, (g, w) in enumerate(zip(got, want)):
            if w == "000000000":
                continue
            if g[0] != "1" or not close(int(g[1:], 16), int(w[1:], 16), tol):
                bad.append("PT word %d: model %s reference %s" % (i, g, w))
    if bad:
        fail(name, "%d differences: %s" % (len(bad), "; ".join(bad[:4])))
        continue
    ok(name, "%d instructions; the model's results are the reference's" % (len(w32) // 2))

for name, stage, options, reason in REFUSALS:
    spv = os.path.join(SPV, name.split(":")[0] + ".spv")
    code, text = run([HARNESS, "compile", stage, spv, os.path.join(OUT, name.replace(":", "-")), options])
    if code == 0:
        fail(name, "compiled; it must be refused (%s)" % reason)
    elif reason not in text:
        fail(name, "refused without %r: %s" % (reason, text.strip()))
    else:
        ok(name, "refused: %s" % text.strip().split(": ", 1)[-1])

print("%d passed, %d failed" % (len(passes), len(failures)))
sys.exit(1 if failures else 0)
