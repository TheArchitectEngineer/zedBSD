#!/usr/bin/env python3
"""WS031 E-114: make port_lcd_calc.py table-driven for the modeset bodies.
  - port_lcd_modeset.json (next to the generator) lists, per reference source file, the EXTRA functions to keep
    in the existing generated files, NEW generated files, and the register-macro roots per reference header;
  - macro_closure(): the #define blocks of the root macros plus every macro of the same header they use;
  - static functions get forward declarations, so the keep-list order does not have to be a call order.
usage: patch_generator_e114.py <path to port_lcd_calc.py>"""
import sys
NL = chr(10)
p = sys.argv[1]
s = open(p).read()
def rep(old, new):
    global s
    assert s.count(old) == 1, old[:90]
    s = s.replace(old, new)

rep("def between(text, a, b, src, label):", '''SPEC_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "port_lcd_modeset.json")
SPEC = json.load(open(SPEC_PATH))
manifest["sources"]["port_lcd_modeset.json"] = hashlib.sha256(open(SPEC_PATH, "rb").read()).hexdigest()

def extra(text, srcname, src):
    """the EXTRA functions port_lcd_modeset.json lists for this reference file"""
    return "".join(func(text, n, src) + NL for n in SPEC["extra"].get(srcname, []))

def protos(body):
    """forward declarations of every static function defined in `body` (generated text, not reference text)"""
    out = []
    for m in re.finditer(r"(?m)^static [^;{}]*?\\([^;{}]*?\\)\\s*\\n\\{", body):
        head = body[m.start():m.end()].rsplit("{", 1)[0].rstrip()
        if " inline " not in head.split("(")[0] + " ":
            out.append(head + ";")
    return ("/* zedBSD: forward declarations of the static functions below (generated; the keep-list is not in call order) */" + NL +
            NL.join(out) + NL + NL) if out else ""

def macro_closure(text, roots, src, label):
    """the #define blocks (with continuation lines) of `roots` and of every macro of `text` they use, in file order"""
    defs, order = {}, []
    for m in re.finditer(r"(?m)^#[ \\t]*define[ \\t]+([A-Za-z_]\\w*)(?:.*\\\\\\n)*.*\\n", text):
        if m.group(1) not in defs:
            defs[m.group(1)] = m.group(0)
            order.append(m.group(1))
    want, todo = set(), [r for r in roots]
    for r in roots:
        assert r in defs, (label, "no such macro", r)
    while todo:
        n = todo.pop()
        if n in want:
            continue
        want.add(n)
        for tok in set(re.findall(r"[A-Za-z_]\\w*", defs[n].split(None, 2)[2] if len(defs[n].split(None, 2)) > 2 else "")):
            if tok in defs and tok not in want:
                todo.append(tok)
    manifest["kept"].setdefault(src, []).append(label + ": " + str(len(want)) + " macros from " + str(len(roots)) + " roots")
    return "".join(defs[n] for n in order if n in want)

def between(text, a, b, src, label):''')

# the existing per-source keep-lists take the EXTRA functions, and every such file gets forward declarations
rep('''    body += func(dpc, n, "display/intel_dp.c") + NL
lic = dpc[:first_comment_end(dpc)]''', '''    body += func(dpc, n, "display/intel_dp.c") + NL
body += extra(dpc, "intel_dp.c", "display/intel_dp.c")
body = protos(body) + body
lic = dpc[:first_comment_end(dpc)]''')
rep('''    body += func(dis, n, "display/intel_display.c") + NL
lic = dis[:first_comment_end(dis)]''', '''    body += func(dis, n, "display/intel_display.c") + NL
body += extra(dis, "intel_display.c", "display/intel_display.c")
body = protos(body) + body
lic = dis[:first_comment_end(dis)]''')
rep('''    body += func(ddi, n, "display/intel_ddi.c") + NL
lic = ddi[:first_comment_end(ddi)]''', '''    body += func(ddi, n, "display/intel_ddi.c") + NL
body += extra(ddi, "intel_ddi.c", "display/intel_ddi.c")
body = protos(body) + body
lic = ddi[:first_comment_end(ddi)]''')
rep('''body += func(pm, "icl_calc_dp_combo_pll", "display/intel_dpll_mgr.c") + NL + func(pm, "icl_calc_dpll_state", "display/intel_dpll_mgr.c")
''', '''body += func(pm, "icl_calc_dp_combo_pll", "display/intel_dpll_mgr.c") + NL + func(pm, "icl_calc_dpll_state", "display/intel_dpll_mgr.c")
body += NL + extra(pm, "intel_dpll_mgr.c", "display/intel_dpll_mgr.c")
body = protos(body) + body
''')

new = '''
# ======================================================================= E-114: table-driven modeset bodies
for spec in SPEC["new_files"]:
    base = ref if spec["dir"] == "i915" else drmref
    stext = rd(os.path.join(base, os.path.dirname(spec["source"])), os.path.basename(spec["source"]))
    body = ""
    for part in spec.get("ranges", []):
        body += between(stext, part[0], part[1], spec["source"], part[2]) + NL
    for n in spec["functions"]:
        body += func(stext, n, spec["source"]) + NL
    body = protos(body) + body
    lic = stext[:first_comment_end(stext)]
    text = lic + note("Linux v6.8.12 " + spec["path"], manifest["sources"][os.path.basename(spec["source"])],
        [" - kept: " + ", ".join(spec["functions"][i:i + 4]) + ("," if i + 4 < len(spec["functions"]) else ";")
         if i == 0 else "   " + ", ".join(spec["functions"][i:i + 4]) + ("," if i + 4 < len(spec["functions"]) else ";")
         for i in range(0, len(spec["functions"]), 4)] +
        [" - the includes are replaced by: " + ", ".join(spec["includes"]) + ";"] +
        ([" - " + spec["glue"] + " (zedBSD code) is included at the end of the file."] if spec.get("glue") else [])) + NL
    for inc in spec["includes"]:
        text += '#include "' + inc + '"' + NL
    text += NL + body
    if spec.get("glue"):
        text += NL + '#include "' + spec["glue"] + '"' + NL
    wr(spec["out"], text)

for spec in SPEC.get("range_headers", []):
    base = ref if spec["dir"] == "i915" else drmref
    stext = rd(os.path.join(base, os.path.dirname(spec["source"])), os.path.basename(spec["source"]))
    guard = "PARITY_" + re.sub(r"\W", "_", spec["out"]).upper()
    lic = stext[:first_comment_end(stext)]
    body = NL.join(between(stext, part[0], part[1], spec["source"], part[2]) for part in spec["ranges"])
    wr(spec["out"], lic + NL + NL + "/*" + NL +
       " * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference " + spec["path"] + NL +
       " * (sha256 " + manifest["sources"][os.path.basename(spec["source"])] + ") by tools/port_lcd_calc.py: " + NL +
       " * " + ", ".join(part[2] for part in spec["ranges"]) + "." + NL +
       " * The notice above is the source file's own.  Do not edit by hand." + NL + " */" + NL +
       "#ifndef " + guard + NL + "#define " + guard + NL + NL + body + NL + "#endif /* " + guard + " */" + NL)

for spec in SPEC["macro_headers"]:
    base = ref if spec["dir"] == "i915" else drmref
    stext = rd(os.path.join(base, os.path.dirname(spec["source"])), os.path.basename(spec["source"]))
    guard = "PARITY_" + re.sub(r"\\W", "_", spec["out"]).upper()
    lic = stext[:first_comment_end(stext)]
    wr(spec["out"], lic + NL + NL + "/*" + NL +
       " * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference " + spec["path"] + NL +
       " * (sha256 " + manifest["sources"][os.path.basename(spec["source"])] + ") by tools/port_lcd_calc.py:" + NL +
       " * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use." + NL +
       " * The notice above is the source file's own.  Do not edit by hand." + NL + " */" + NL +
       "#ifndef " + guard + NL + "#define " + guard + NL + NL +
       macro_closure(stext, spec["roots"], spec["source"], spec["out"]) + NL + "#endif /* " + guard + " */" + NL)

'''
rep('open(os.path.join(out, "port_lcd_calc.manifest.json"), "w")', new + 'open(os.path.join(out, "port_lcd_calc.manifest.json"), "w")')
open(p, "w").write(s)
print("generator patched (E-114)")
