#!/usr/bin/env python3
# Compares two golden write records scene by scene, leaving out the register ranges given as LO-HI (hex), and
# prints the differences of what is left: plan/ws260/tests/golden-diff.py OLD NEW [LO-HI ...].
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import sys
def load(p):
    scenes=[];cur=None
    for l in open(p):
        if l.startswith('scene'):
            cur=[];scenes.append((l.strip(),cur))
        else:
            o,v=l.split(); cur.append((int(o,16),int(v,16)))
    return scenes
a=load(sys.argv[1]); b=load(sys.argv[2])
ranges=[tuple(int(x,16) for x in r.split('-')) for r in sys.argv[3:]]
def keep(w): return not any(lo<=w[0]<=hi for lo,hi in ranges)
print('scenes',len(a),len(b))
for i,((ha,wa),(hb,wb)) in enumerate(zip(a,b)):
    fa=[w for w in wa if keep(w)]; fb=[w for w in wb if keep(w)]
    print(i, ha,'|',hb, 'filtered-same' if fa==fb else 'FILTERED-DIFF %d %d'%(len(fa),len(fb)))
    if fa!=fb:
        import difflib
        for d in list(difflib.unified_diff(['%05x %08x'%w for w in fa],['%05x %08x'%w for w in fb],lineterm='',n=1))[:30]: print('   ',d)
