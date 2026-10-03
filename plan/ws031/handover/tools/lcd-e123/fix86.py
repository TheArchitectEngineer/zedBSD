#!/usr/bin/env python3
"""E-123 round 86 fixup: the two log strings lost their escaped newline, and the ktest needs klog."""
import sys
root = sys.argv[1].rstrip("/") + "/"
L = root + "src/drivers/gpu/i915/parity/lcd/"
NL = chr(10)
BS = chr(92)

p = L + "hpd_ktest.c"
s = open(p).read()
bad = '"not share the state the interrupt handler reaches)' + NL + '");'
if bad in s:
    s = s.replace(bad, '"not share the state the interrupt handler reaches)' + BS + 'n");')
if '#include <kern/klog.h>' not in s:
    s = s.replace('#include <kern/sched.h>', '#include <kern/klog.h>' + NL + '#include <kern/sched.h>', 1)
open(p, "w").write(s)

p = L + "parity_hotplug_glue.inc"
s = open(p).read()
bad = '"digital=%u | epoch=%llu t=%llu' + NL + '", connector->base.name'
if bad in s:
    s = s.replace(bad, '"digital=%u | epoch=%llu t=%llu' + BS + 'n", connector->base.name')
open(p, "w").write(s)
print("fixed")
