import sys
p = sys.argv[1] + "/src/drivers/gpu/i915/parity/native_precheck.c"
s = open(p).read()
a = s.index('	kern_logf("i915: parity N0 pipe-A domain:')
s = s[:a] + '	if (!r->pipe[0].readable)\n		kern_logf("i915: parity N0 pipe-A domain: not read (pipe A\'s wells are off)\n");\n	else\n	' + s[a+1:]
open(p, "w").write(s); print("ok")
