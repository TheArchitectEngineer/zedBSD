#!/usr/bin/env python3
"""WS031: which wire opcodes does libvulkan emit, which does the i915 kernel executor handle,
and which does the vkdemo application need?  Read-only, facts from the source tree."""
import os, re, collections
root = os.path.expanduser("~/zedBSD")
lv = os.path.join(root, "userland/base/libvulkan")
vk = os.path.join(root, "src/drivers/gpu/i915/vk")

ops = {}
for m in re.finditer(r"VULKAN_OPCODE_(\w+)\s*=\s*(\d+)", open(os.path.join(lv, "opcodes.h")).read()):
    ops[m.group(1)] = int(m.group(2))
num2name = {v: k for k, v in ops.items()}

# opcodes referenced by libvulkan encoders
emitted = collections.Counter()
for f in os.listdir(lv):
    if f.endswith((".c", ".inc")) and not f.endswith("~"):
        s = open(os.path.join(lv, f), errors="replace").read()
        for m in re.finditer(r"VULKAN_OPCODE_(\w+)", s):
            emitted[m.group(1)] += 1

# kernel executor: "case NU: /* name */ return fn(...)" per module, plus builtin numbers
handled = {}
for f in ("res.c", "pipe.c", "cmdbuf.c", "sync.c", "wsi.c", "cmd.c"):
    s = open(os.path.join(vk, f), errors="replace").read()
    for m in re.finditer(r"case (\d+)U:\s*/\*\s*(\w+)\s*\*/\s*\n\s*return (\w+)\(", s):
        handled[int(m.group(1))] = (f, m.group(3))
    for m in re.finditer(r"if \(opcode == (\d+)U\)", s):
        handled.setdefault(int(m.group(1)), (f, "builtin"))

def body(fn):
    for f in os.listdir(vk):
        if not f.endswith(".c"):
            continue
        s = open(os.path.join(vk, f), errors="replace").read()
        m = re.search(r"\n" + re.escape(fn) + r"\(\s*\n(?:.*?\n)*?\{\n(.*?)\n\}\n", s, re.S)
        if m:
            return m.group(1)
    return ""

def quality(fn):
    b = body(fn)
    if not b:
        return "?"
    code = [l for l in b.split("\n") if l.strip() and not l.strip().startswith(("/*", "*", "//"))]
    if re.search(r"no-?op|accepted|not yet|TODO|for now", b, re.I) or len(code) <= 6:
        return "thin(%d lines)" % len(code)
    return "impl(%d lines)" % len(code)

# vkdemo's API use
demo = collections.Counter()
dd = os.path.join(root, "userland/base/vkdemo")
for d, _, fs in os.walk(dd):
    for f in fs:
        if f.endswith((".c", ".h")):
            for m in re.finditer(r"\b(vk[A-Z]\w+)\s*\(", open(os.path.join(d, f), errors="replace").read()):
                demo[m.group(1)] += 1

fam = [("instance/device/queue", range(0, 21)), ("memory/buffer/image bind", range(21, 35)),
       ("fence/semaphore/event/query", range(35, 50)), ("buffer/view/image/view", range(50, 59)),
       ("shader/pipeline cache/pipeline/layout", range(59, 70)), ("sampler/descriptor", range(70, 80)),
       ("framebuffer/render pass", range(80, 85)), ("command pool/buffer", range(85, 93)),
       ("vkCmd* recording", range(93, 137)), ("version/1.1+/transport", range(137, 400))]
print("| family | wire opcodes | emitted by libvulkan | handler in i915 vk/ | needed by vkdemo | vkdemo needs but no handler |")
print("|---|---|---|---|---|---|")
tot = [0, 0, 0, 0, 0]
missing_all = []
for name, rng in fam:
    inr = [n for n in num2name if n in rng]
    em = [n for n in inr if emitted[num2name[n]]]
    hd = [n for n in inr if n in handled]
    nd = [n for n in inr if demo[num2name[n]]]
    miss = [num2name[n] for n in nd if n not in handled]
    missing_all += miss
    for i, v in enumerate((len(inr), len(em), len(hd), len(nd), len(miss))):
        tot[i] += v
    print("| %s | %d | %d | %d | %d | %d |" % (name, len(inr), len(em), len(hd), len(nd), len(miss)))
print("| **total** | %d | %d | %d | %d | %d |" % tuple(tot))
print("\nhandlers present (%d):" % len(handled))
for n in sorted(handled):
    f, fn = handled[n]
    print("  %3d %-34s %-10s %-44s %s" % (n, num2name.get(n, "?"), f, fn, quality(fn) if fn != "builtin" else "builtin"))
print("\nvkdemo calls %d distinct vk* functions; without a kernel handler (%d):" % (len(demo), len(missing_all)))
print("  " + ", ".join(missing_all))
cl = sorted(k for k in demo if k[0:2] == "vk" and k not in ops)
print("\nvkdemo calls with no wire opcode of the same name (client-side / WSI / extension) (%d):" % len(cl))
print("  " + ", ".join(cl))
