import sys
p = sys.argv[1] + "/src/drivers/gpu/i915/parity/native_precheck.c"
s = open(p).read()
a = s.index("parity_native_precheck(const struct parity_native_deps *d, struct parity_native_report *r)")
b = s.index("\tunsigned p;\n\n", a)
assert b - a < 200
s = s[:b] + s[b + len("\tunsigned p;\n\n"):]
old = "(power domain off: counted inactive, as the reference readout "
assert s.count(old) == 1
s = s.replace(old, "(its wells' STATE bits are off: counted inactive, as the reference readout ")
open(p, "w").write(s); print("fixed")
