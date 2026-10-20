#!/usr/bin/env python3
# PowerVR の firmware の trace（dmesg の "powervr: dump: trace" の行）を mainline の pvr_rogue_fwif_sf.h の CREATESFID 表で読む。
#   python3 -I plan/tools/vf2/sfdec.py pvr_rogue_fwif_sf.h dmesg.txt
# header は Linux の drivers/gpu/drm/imagination/pvr_rogue_fwif_sf.h（curl で取る）。ws269 p010q、2026-10-20。
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import re,sys
h=open(sys.argv[1]).read()
G={}
for i,n in enumerate(re.findall(r'ROGUE_FW_GROUP_(\w+),',h.split('enum')[1].split('}')[0])): G[n]=i
ID={}
for m in re.finditer(r'ROGUE_FW_LOG_CREATESFID\((\d+),\s*ROGUE_FW_GROUP_(\w+),\s*(\d+)\),\s*"((?:[^"\\]|\\.)*)"',h):
    a,g,n,f=m.groups(); a=int(a); n=int(n)
    ID[a|G[g]<<12|n<<16|0x70000000]=(g,f,n)
w=[]
for l in open(sys.argv[2]):
    m=re.search(r'dump: trace\s+\d+: (.*)',l)
    if m: w+= [int(x,16) for x in m.group(1).split()]
    elif 'left out' in l: w.append(None)
i=0
while i<len(w):
    x=w[i]
    if x in ID:
        g,f,n=ID[x]; a=w[i+3:i+3+n]
        if None not in a:
            try: s=re.sub(r'%[-0-9.]*l*[dux]',lambda m:'{}',f).format(*[hex(v) for v in a])
            except Exception: s=f+str(a)
            print(g,s); i+=3+n; continue
    if x is None: print('---gap---')
    i+=1
