#!/usr/bin/env python3
# pm4-decode.py REGISTERS_JSON DUMP: the type-3 packets of a dump (one hex word a line, "#" lines kept), each register
# write as "NAME = value" by Mesa's gfx10.json (src/amd/registers), other packets by opcode with their words.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import json
import sys

OPS = {0x10: 'NOP', 0x12: 'CLEAR_STATE', 0x15: 'DISPATCH_DIRECT', 0x27: 'DRAW_INDEX_2', 0x28: 'CONTEXT_CONTROL',
       0x2a: 'INDEX_TYPE', 0x2d: 'DRAW_INDEX_AUTO', 0x2f: 'NUM_INSTANCES', 0x33: 'INDEX_BASE', 0x34: 'DRAW_INDEX_OFFSET_2',
       0x37: 'WRITE_DATA', 0x3c: 'WAIT_REG_MEM', 0x3f: 'INDIRECT_BUFFER', 0x40: 'COPY_DATA', 0x46: 'EVENT_WRITE',
       0x49: 'RELEASE_MEM', 0x4a: 'PREAMBLE_CNTL', 0x50: 'DMA_DATA', 0x58: 'ACQUIRE_MEM', 0x68: 'SET_CONFIG_REG',
       0x69: 'SET_CONTEXT_REG', 0x76: 'SET_SH_REG', 0x79: 'SET_UCONFIG_REG', 0x7a: 'SET_UCONFIG_REG_INDEX',
       0x9b: 'SET_SH_REG_INDEX', 0x9a: 'SET_SH_REG_OFFSET', 0x13: 'INDEX_BUFFER_SIZE', 0x3d: 'SET_PREDICATION',
       0xa0: 'LOAD_CONTEXT_REG_INDEX'}
BASES = {0x68: 0x8000, 0x69: 0x28000, 0x76: 0xb000, 0x9b: 0xb000, 0x79: 0x30000, 0x7a: 0x30000}

data = json.load(open(sys.argv[1]))
names = {}
for r in data['register_mappings']:
    if r['map']['to'] == 'mm':
        names.setdefault(r['map']['at'], r['name'])
words = []
for line in open(sys.argv[2]):
    line = line.strip()
    if line.startswith('#'):
        words.append(line)
    elif line:
        words.append(int(line, 16))
i = 0
while i < len(words):
    w = words[i]
    if isinstance(w, str):
        print(w)
        i += 1
        continue
    if (w >> 30) != 3:
        print('  word %08x' % w)
        i += 1
        continue
    op = (w >> 8) & 0xff
    count = ((w >> 16) & 0x3fff) + 1
    if count == 0x4000:
        # The one-word NOP (count 0x3fff) the drivers pad with.
        i += 1
        continue
    body = [x for x in words[i + 1:i + 1 + count] if not isinstance(x, str)]
    name = OPS.get(op, 'OP_%02x' % op)
    if op in BASES and body:
        offset = body[0] & 0xffff
        index = body[0] >> 28
        for k, value in enumerate(body[1:]):
            address = BASES[op] + (offset + k) * 4
            print('  %s%s %s = 0x%08x' % (name, '[%d]' % index if op in (0x9b, 0x7a) else '', names.get(address, hex(address)), value))
    else:
        print('  %s %s' % (name, ' '.join('%08x' % x for x in body)))
    i += 1 + count
