#!/usr/bin/env python3
"""WS031: extract the C1 artifacts a zedBSD parity run actually submitted from its
debugcon log, write them as hex + raw binary, and diff two batches dword by dword.

  eu_artifact.py extract <run.log> <out-prefix>
      -> <prefix>-batch.hex/.bin, <prefix>-idd.bin, <prefix>-kernel.bin, <prefix>-manifest.txt
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
    elif len(sys.argv) == 4 and sys.argv[1] == "diff":
        diff(sys.argv[2], sys.argv[3])
    else:
        sys.exit(__doc__)
