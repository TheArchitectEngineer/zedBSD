#!/usr/bin/env python3
"""WS031 E-123 round 75: the shared kworkqueue becomes IRQ-safe (queue_work() is callable from an interrupt handler, as
in Linux): every wq->lock section takes spin_lock_irqsave / spin_unlock_irqrestore, the pattern the completion in the
same file already uses (its waitq_sleep / waitq_wake_all run under an IRQ-saved lock).  Needed by the HPD decode
(intel_hpd_irq_handler queues the hotplug work from the IRQ) and the real GSE dispatch.  usage: round75.py <repo root>"""
import sys, re
root = sys.argv[1].rstrip("/") + "/"
p = root + "src/drivers/gpu/i915/parity/backend_sync.c"
s = open(p).read()
NL = chr(10)
out = []
pos = 0
# walk function bodies: a line "{" at column 0 opens, a line "}" at column 0 closes
for m in re.finditer(r"\n\{\n(.*?)\n\}\n", s, re.S):
    body = m.group(1)
    if "spin_lock(&wq->lock);" in body or "spin_unlock(&wq->lock);" in body:
        nb = body.replace("spin_lock(&wq->lock);", "f = spin_lock_irqsave(&wq->lock);")
        nb = nb.replace("spin_unlock(&wq->lock);", "spin_unlock_irqrestore(&wq->lock, f);")
        nb = "\tunsigned long f;   /* IRQ-safe: queue_work() may be called from an interrupt handler */" + NL + nb
        out.append(s[pos:m.start(1)])
        out.append(nb)
        pos = m.end(1)
out.append(s[pos:])
t = "".join(out)
assert "spin_lock(&wq->lock)" not in t and "spin_unlock(&wq->lock)" not in t
open(p, "w").write(t)
print("converted", t.count("spin_lock_irqsave(&wq->lock)"), "lock sites")
