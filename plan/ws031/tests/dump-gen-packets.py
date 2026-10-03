"""Resolves a genxml file through its <import> chain and dumps packet layouts."""
import os, sys, xml.etree.ElementTree as ET

BASE = "plan/ws031/mesa-refs/mesa/src/intel/genxml"

def load(name, seen=None):
    """Returns {('instruction'|'struct'|'enum', name): element}, child overriding parent."""
    path = os.path.join(BASE, name)
    root = ET.parse(path).getroot()
    out = {}
    for imp in root.findall("import"):
        excluded = {e.get("name") for e in imp.findall("exclude")}
        for key, el in load(imp.get("name")).items():
            if key[1] not in excluded:
                out[key] = el
    for tag in ("instruction", "struct", "enum", "register"):
        for el in root.findall(tag):
            out[(tag, el.get("name"))] = el
    return out

DB = load(sys.argv[1] if len(sys.argv) > 1 and sys.argv[1].endswith(".xml") else "gen120.xml")
args = [a for a in sys.argv[1:] if not a.endswith(".xml")]
mode = args[0] if args else "len"
names = args[1:]

def find(n):
    for tag in ("instruction", "struct", "enum", "register"):
        if (tag, n) in DB:
            return tag, DB[(tag, n)]
    return None, None

def header(el):
    """ORs the dword-0 defaults: the command header with a zero DWord Length."""
    v = 0
    for f in el.findall("field"):
        if f.get("dword") not in (None, "0") or f.get("default") is None:
            continue
        hi, lo = (int(x) for x in f.get("bits").split(":"))
        v |= (int(f.get("default")) & ((1 << (hi - lo + 1)) - 1)) << lo
    return v

if mode == "len":
    want = names or []
    print("%-44s %-5s %-5s %s" % ("NAME", "LEN", "BIAS", "HEADER"))
    for n in want:
        tag, el = find(n)
        if el is None:
            print("%-44s NOT FOUND" % n)
        elif tag == "instruction":
            print("%-44s %-5s %-5s 0x%08x" % (n, el.get("length"), el.get("bias"), header(el)))
        else:
            print("%-44s %-5s %s" % (n, el.get("length"), tag))
elif mode == "fields":
    for n in names:
        tag, el = find(n)
        if el is None:
            print("=== %s: NOT FOUND\n" % n); continue
        print("=== %s (%s) len=%s bias=%s header=0x%08x" % (
            n, tag, el.get("length"), el.get("bias"),
            header(el) if tag == "instruction" else 0))
        for f in el.findall("field"):
            extra = ""
            if f.get("default"): extra += " default=" + f.get("default")
            if f.get("nonzero"): extra += " NONZERO"
            print("  dw%-3s %-10s %-48s %s%s" % (f.get("dword"), f.get("bits"), f.get("name"), f.get("type"), extra))
            e = DB.get(("enum", f.get("type")))
            if e is not None:
                print("        values: " + ", ".join("%s=%s" % (v.get("name"), v.get("value")) for v in e.findall("value")))
        for g in el.findall("group"):
            print("  GROUP count=%s start=%s size=%s" % (g.get("count"), g.get("start"), g.get("size")))
            for f in g.findall("field"):
                print("    dw%-3s %-10s %-46s %s" % (f.get("dword"), f.get("bits"), f.get("name"), f.get("type")))
        print()
elif mode == "enum":
    for n in names:
        e = DB.get(("enum", n))
        print("=== %s" % n)
        if e is not None:
            for v in e.findall("value"):
                print("  %-44s %s" % (v.get("name"), v.get("value")))
