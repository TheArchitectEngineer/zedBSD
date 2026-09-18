#!/usr/bin/env python3
"""WS031: extract the C1 artifacts a zedBSD parity run actually submitted from its
debugcon log, write them as hex + raw binary, and diff two batches dword by dword.

  eu_artifact.py extract <run.log> <out-prefix>
      -> <prefix>-batch.hex/.bin, <prefix>-idd.bin, <prefix>-kernel.bin, <prefix>-manifest.txt
  eu_artifact.py extract-draw <run.log> <out-prefix>
      -> <prefix>-batch.hex/.bin, <prefix>-state.bin (4096-byte state page), <prefix>-manifest.txt
  eu_artifact.py extract-tex <run.log> <out-prefix>
      -> batch/state/texture/rt .bin + SHA-256, the render target compared with an expected
         image computed independently here, and <prefix>-rt.ppm / -expected.ppm for viewing
  eu_artifact.py diff <old-batch.hex> <new-batch.hex>
      -> every differing dword: index, byte offset, old, new, xor

Byte order in the .bin files is the little-endian order the GPU reads.
"""
import hashlib, re, struct, sys

def words_from_hex(path):
    w = []
    for line in open(path):
        w += [int(x, 16) for x in line.split()]
    return w

def extract(log, prefix):
    batch, idd, kernel, meta = {}, [], {}, {}
    for line in open(log, errors="replace"):
        m = re.search(r"EU-TEST batch\[(\d+)\]: (.*)$", line)
        if m:
            batch[int(m.group(1))] = [int(x, 16) for x in m.group(2).split()]
        m = re.search(r"EU-TEST fixture-idd\[@\d+\]: (.*)$", line)
        if m:
            idd = [int(x, 16) for x in m.group(1).split()]
        m = re.search(r"EU-TEST fixture-kernel\[(\d+)\]: (.*)$", line)
        if m:
            kernel[int(m.group(1))] = [int(x, 16) for x in m.group(2).split()]
        m = re.search(r"EU-TEST fixture: .*batch_hash=(\w+) fixture_hash=(\w+) batch_dwords=(\d+)", line)
        if m:
            meta = {"fnv_batch": m.group(1), "fnv_fixture": m.group(2), "dwords": int(m.group(3))}
    bw = []
    for k in sorted(batch):
        bw += batch[k]
    bw = bw[:meta.get("dwords", len(bw))]
    kw = []
    for k in sorted(kernel):
        kw += kernel[k]
    out = []
    with open(prefix + "-batch.hex", "w") as f:
        for i in range(0, len(bw), 8):
            f.write(" ".join("%08x" % x for x in bw[i:i + 8]) + "\n")
    for name, ws in (("batch", bw), ("idd", idd), ("kernel", kw)):
        raw = struct.pack("<%dI" % len(ws), *ws)
        open("%s-%s.bin" % (prefix, name), "wb").write(raw)
        out.append("%-7s bytes=%-5d sha256=%s" % (name, len(raw), hashlib.sha256(raw).hexdigest()))
    out.append("fnv1a64 batch=%s fixture(idd..kernel)=%s" % (meta.get("fnv_batch"), meta.get("fnv_fixture")))
    open(prefix + "-manifest.txt", "w").write("\n".join(out) + "\n")
    print("\n".join(out))

def extract_draw(log, prefix):
    batch, state, dwords = {}, [0] * 1024, None
    for line in open(log, errors="replace"):
        m = re.search(r"DRAW-TEST batch\[(\d+)\]: (.*)$", line)
        if m:
            batch[int(m.group(1))] = [int(x, 16) for x in m.group(2).split()]
        m = re.search(r"DRAW-TEST state\[(\d+)\]: (.*)$", line)
        if m:
            k = int(m.group(1))
            for i, x in enumerate(m.group(2).split()):
                state[k + i] = int(x, 16)
        m = re.search(r"DRAW-TEST fixture: .*batch_hash=(\w+) state_hash=(\w+) batch_dwords=(\d+)", line)
        if m:
            fnv = (m.group(1), m.group(2)); dwords = int(m.group(3))
    bw = []
    for k in sorted(batch):
        bw += batch[k]
    bw = bw[:dwords]
    with open(prefix + "-batch.hex", "w") as f:
        for i in range(0, len(bw), 8):
            f.write(" ".join("%08x" % x for x in bw[i:i + 8]) + "\n")
    out = []
    for name, ws in (("batch", bw), ("state", state)):
        raw = struct.pack("<%dI" % len(ws), *ws)
        open("%s-%s.bin" % (prefix, name), "wb").write(raw)
        out.append("%-7s bytes=%-5d sha256=%s" % (name, len(raw), hashlib.sha256(raw).hexdigest()))
    out.append("fnv1a64 batch=%s state(page as submitted; the log is read after the run, so the marker dwords are the GPU-written values)=%s" % fnv)
    open(prefix + "-manifest.txt", "w").write("\n".join(out) + "\n")
    print("\n".join(out))

def tex_pattern(variant=0):
    """The T1 test image: RGBA bytes, texel (u,v) at [(v*8+u)*4] (same formula as draw_fixture.h)."""
    out = bytearray(256)
    for v in range(8):
        for u in range(8):
            o = (v * 8 + u) * 4
            if variant == 0:
                out[o:o + 4] = bytes((16 + 32 * u, 16 + 32 * v, 16 + 32 * ((u + 3 * v) & 7), 255))
            elif variant == 1:
                out[o:o + 4] = bytes((239 - 32 * v, 16 + 32 * u, 16 + 32 * ((3 * u + v) & 7), 255))
            else:
                out[o:o + 4] = bytes((240 - 32 * u, 240 - 32 * v, 16 + 32 * ((u ^ v) & 7), 255))
    return bytes(out)

def tex_expected(pattern):
    """32x32 B8G8R8A8 dwords: nearest, uv=(pixel+0.5)/32 over 8x8 -> texel (x//4, y//4); origin upper left."""
    exp = []
    for y in range(32):
        for x in range(32):
            r, g, b, a = pattern[((y // 4) * 8 + (x // 4)) * 4:][:4]
            exp.append(b | (g << 8) | (r << 16) | (a << 24))
    return exp

def write_ppm(path, dwords, scale=8):
    """B8G8R8A8 dwords -> binary PPM (RGB), each pixel enlarged for viewing."""
    w = h = 32
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w * scale, h * scale))
        for y in range(h):
            row = bytearray()
            for x in range(w):
                d = dwords[y * w + x]
                row += bytes(((d >> 16) & 255, (d >> 8) & 255, d & 255)) * scale
            f.write(bytes(row) * scale)

def extract_tex(log, prefix, tag="TEX-TEST", variant=0):
    sec = {"batch": {}, "state": {}, "tex": {}, "rt": {}}
    dwords = None
    for line in open(log, errors="replace"):
        m = re.search(tag + r" (batch|state|tex|rt)\[(\d+)\]: (.*)$", line)
        if m:
            sec[m.group(1)][int(m.group(2))] = [int(x, 16) for x in m.group(3).split()]
        m = re.search(tag + r" fixture: .*batch_dwords=(\d+)", line)
        if m:
            dwords = int(m.group(1))
    def flat(d, total=None):
        n = total if total else (max(d) + 8 if d else 0)
        ws = [0] * n
        for k, row in d.items():
            for i, x in enumerate(row):
                if k + i < n:
                    ws[k + i] = x
        return ws
    batch = flat(sec["batch"])[:dwords]
    state = flat(sec["state"], 1024)
    tex = flat(sec["tex"], 64)
    rt = flat(sec["rt"], 1024)
    with open(prefix + "-batch.hex", "w") as f:
        for i in range(0, len(batch), 8):
            f.write(" ".join("%08x" % x for x in batch[i:i + 8]) + "\n")
    out = []
    for name, ws in (("batch", batch), ("state", state), ("texture", tex), ("rt", rt)):
        raw = struct.pack("<%dI" % len(ws), *ws)
        open("%s-%s.bin" % (prefix, name), "wb").write(raw)
        out.append("%-8s bytes=%-5d sha256=%s" % (name, len(raw), hashlib.sha256(raw).hexdigest()))
    pattern = tex_pattern(variant)
    texraw = struct.pack("<64I", *tex)
    out.append("texture == CPU pattern(variant %d): %s" % (variant, texraw == pattern))
    exp = tex_expected(pattern)
    bad = [(i % 32, i // 32, exp[i], rt[i]) for i in range(1024) if exp[i] != rt[i]]
    out.append("rt vs expected (independent host computation): match=%d/1024" % (1024 - len(bad)))
    if bad:
        out.append("first mismatch x=%d y=%d expected=%08x observed=%08x" % bad[0])
    write_ppm(prefix + "-rt.ppm", rt)
    write_ppm(prefix + "-expected.ppm", exp)
    open(prefix + "-manifest.txt", "w").write("\n".join(out) + "\n")
    print("\n".join(out))

def fnv1a64(data):
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xffffffffffffffff
    return h

def verify_t3(log):
    """Every T3 step: the render-target hash in the log against the hash of the expected image
    computed here (independently of the kernel), and the texture hashes against the images."""
    exp_rt = {v: fnv1a64(struct.pack("<1024I", *tex_expected(tex_pattern(v)))) for v in range(3)}
    exp_tex = {fnv1a64(tex_pattern(v)): v for v in range(3)}
    ok = n = 0
    for line in open(log, errors="replace"):
        m = re.search(r"T3 step=(\d+) ctx=(\w) bind=(\w) upload=(\S+) expect_variant=(\d+) .*pass=(\d) .*"
                      r"rt_hash=(\w+) texA_hash=(\w+) texB_hash=(\w+)", line)
        if not m:
            continue
        n += 1
        step, ctx, bind, upload, ev, passed, rth, ah, bh = m.groups()
        ev = int(ev)
        a, b = exp_tex.get(int(ah, 16)), exp_tex.get(int(bh, 16))
        bound = a if bind == "A" else b
        good = int(rth, 16) == exp_rt[ev] and bound == ev and passed == "1"
        ok += good
        print("step %s ctx=%s bind=%s upload=%-4s texA=image%s texB=image%s expected=image%d rt_hash %s -> %s" % (
            step, ctx, bind, upload, a, b, ev, "matches host" if int(rth, 16) == exp_rt[ev] else "DIFFERS",
            "OK" if good else "BAD"))
    print("T3 host verification: %d/%d steps" % (ok, n))

def diff(a, b):
    wa, wb = words_from_hex(a), words_from_hex(b)
    print("old dwords=%d new dwords=%d" % (len(wa), len(wb)))
    n = 0
    for i in range(max(len(wa), len(wb))):
        x = wa[i] if i < len(wa) else None
        y = wb[i] if i < len(wb) else None
        if x != y:
            n += 1
            print("dword %3d  byte 0x%04x  old=%s new=%s xor=%s" % (
                i, i * 4, "%08x" % x if x is not None else "--------",
                "%08x" % y if y is not None else "--------",
                "%08x" % (x ^ y) if None not in (x, y) else "--------"))
    print("differing dwords: %d" % n)

if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "extract":
        extract(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 4 and sys.argv[1] == "extract-draw":
        extract_draw(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 4 and sys.argv[1] == "extract-tex":
        extract_tex(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 5 and sys.argv[1] == "extract-t3-last":
        extract_tex(sys.argv[2], sys.argv[3], tag="T3-LAST", variant=int(sys.argv[4]))
    elif len(sys.argv) == 3 and sys.argv[1] == "verify-t3":
        verify_t3(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == "diff":
        diff(sys.argv[2], sys.argv[3])
    else:
        sys.exit(__doc__)
