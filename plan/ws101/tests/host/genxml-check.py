#!/usr/bin/env python3
# ws101-p004: decodes a dispatch's batch and interface descriptor with Mesa's genxml and checks their fields.
#
# The Gen12.0 definitions are Mesa 25.0.7's gen120.xml with its imports merged by Mesa's own intel_genxml.py, so every
# field is checked where Mesa places it, by the name Mesa gives it, independently of the constants of
# src/drivers/gpu/i915/intel/genxml.h.
#
#   genxml-check.py GENXML_DIR OUT-NAME      reads OUT-NAME.bin, OUT-NAME.idd and OUT-NAME.expect
#
# Every dword of the batch must start or continue a Gen12.0 instruction, and no bit outside an instruction's fields may
# be set.  The lines of the expect file are:
#   NAME F=V ...            every occurrence of the instruction NAME has these fields
#   first NAME F=V ...      its first occurrence has them; last NAME: its last
#   exact NAME F=V ...      as NAME, and every field it does not list (other than the header's) is zero
#   tail NAME ...           the batch ends with these instructions, in this order
#   before-gpgpu NAME F=V   the instruction just before the switch to GPGPU (PIPELINE_SELECT 2); after-gpgpu: just
#                           after it; before-3d: just before the last PIPELINE_SELECT
#   idd F=V ...             the interface descriptor (INTERFACE_DESCRIPTOR_DATA) has these fields and zeros elsewhere
# A field is named as Mesa names it without spaces and punctuation (Thread Group ID X Dimension: ThreadGroupIDXDimension).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import re
import struct
import sys

HEADER_FIELDS = ('Command Type', 'Command SubType', '3D Command Opcode', '3D Command Sub Opcode', 'Pipeline',
                 'Media Command Opcode', 'SubOpcode', 'DWord Length')


def key(name):
    return re.sub('[^A-Za-z0-9]', '', name)


class Item:
    def __init__(self, element):
        self.name = element.attrib['name']
        self.bias = int(element.attrib.get('bias', '0'))
        self.length = int(element.attrib.get('length', '0'))
        self.fields = []
        for field in element.iter('field'):
            start = int(field.attrib['start'])
            end = int(field.attrib['end'])
            self.fields.append((field.attrib['name'], start, end, field.attrib.get('type', 'uint'),
                                field.attrib.get('default')))
        # The header's fixed bits: the fields with a default in dword 0, except the length.
        self.mask = 0
        self.value = 0
        for name, start, end, _, default in self.fields:
            if default is not None and end < 32 and name != 'DWord Length':
                bits = ((1 << (end - start + 1)) - 1) << start
                self.mask |= bits
                self.value |= (int(default, 0) << start) & bits

    def dwords(self, first):
        for name, start, end, _, _ in self.fields:
            if name == 'DWord Length':
                return ((first >> start) & ((1 << (end - start + 1)) - 1)) + self.bias
        return self.length

    def decode(self, words):
        bits = 0
        for index, word in enumerate(words):
            bits |= word << (32 * index)
        covered = 0
        values = {}
        for name, start, end, kind, _ in self.fields:
            width = end - start + 1
            value = (bits >> start) & ((1 << width) - 1)
            covered |= ((1 << width) - 1) << start
            # An address or offset keeps its place in its qword: the low bits the field leaves out are zero.
            if kind in ('address', 'offset'):
                value <<= start % 32
            values[key(name)] = value
        stray = bits & ~covered
        return values, stray


def load(directory):
    sys.path.insert(0, directory)
    import intel_genxml
    root = intel_genxml.GenXml(directory + '/gen120.xml', import_xml=True).et.getroot()
    # The render engine's instructions only: a video engine's may share a header with a media command.
    instructions = [Item(e) for e in root
                    if e.tag == 'instruction' and 'render' in e.attrib.get('engine', 'render').split('|')]
    structs = {e.attrib['name']: Item(e) for e in root if e.tag == 'struct'}
    return instructions, structs


def parse(words, instructions):
    decoded = []
    at = 0
    while at < len(words):
        matches = [i for i in instructions if (words[at] & i.mask) == i.value and i.mask != 0]
        if not matches:
            raise SystemExit('FAIL dword %d (0x%08x) starts no Gen12.0 instruction' % (at, words[at]))
        # The most specific header wins (a media command also matches its pipeline's shorter headers).
        best = max(bin(i.mask).count('1') for i in matches)
        matches = [i for i in matches if bin(i.mask).count('1') == best]
        if len(matches) != 1:
            raise SystemExit('FAIL dword %d (0x%08x) is ambiguous: %s' % (at, words[at], [i.name for i in matches]))
        item = matches[0]
        count = item.dwords(words[at])
        if at + count > len(words):
            raise SystemExit('FAIL %s at dword %d runs past the batch' % (item.name, at))
        values, stray = item.decode(words[at:at + count])
        if stray != 0:
            raise SystemExit('FAIL %s at dword %d sets bits outside its fields: 0x%x' % (item.name, at, stray))
        decoded.append((item, values, at))
        at += count
    return decoded


def check_fields(label, item, values, pairs, exact):
    for name, want in pairs.items():
        if name not in values:
            raise SystemExit('FAIL %s: %s has no field %s' % (label, item.name, name))
        if values[name] != want:
            raise SystemExit('FAIL %s: %s %s is 0x%x, not 0x%x' % (label, item.name, name, values[name], want))
    if exact:
        header = {key(h) for h in HEADER_FIELDS}
        for name, value in values.items():
            if name not in pairs and name not in header and value != 0:
                raise SystemExit('FAIL %s: %s %s is 0x%x, not 0' % (label, item.name, name, value))


def pairs_of(tokens):
    pairs = {}
    for token in tokens:
        name, value = token.split('=')
        pairs[name] = int(value, 0)
    return pairs


def main():
    directory, out = sys.argv[1], sys.argv[2]
    instructions, structs = load(directory)
    with open(out + '.bin', 'rb') as file:
        data = file.read()
    words = list(struct.unpack('<%dI' % (len(data) // 4), data))
    decoded = parse(words, instructions)
    names = [item.name for item, _, _ in decoded]
    selects = [n for n, (item, values, _) in enumerate(decoded) if item.name == 'PIPELINE_SELECT']
    gpgpu = [n for n in selects if decoded[n][1]['PipelineSelection'] == 2]
    checks = 0
    with open(out + '.expect') as file:
        for line in file:
            tokens = line.split()
            if not tokens:
                continue
            verb = tokens[0]
            if verb == 'tail':
                if names[-(len(tokens) - 1):] != tokens[1:]:
                    raise SystemExit('FAIL the batch ends with %s, not %s' % (names[-(len(tokens) - 1):], tokens[1:]))
            elif verb == 'idd':
                with open(out + '.idd', 'rb') as idd:
                    raw = idd.read()
                item = structs['INTERFACE_DESCRIPTOR_DATA']
                values, stray = item.decode(list(struct.unpack('<8I', raw)))
                if stray != 0:
                    raise SystemExit('FAIL the interface descriptor sets bits outside its fields: 0x%x' % stray)
                check_fields('idd', item, values, pairs_of(tokens[1:]), True)
            elif verb in ('first', 'last', 'exact') or verb.isupper():
                name = tokens[1] if not verb.isupper() else verb
                rest = tokens[2:] if not verb.isupper() else tokens[1:]
                found = [d for d in decoded if d[0].name == name]
                if not found:
                    raise SystemExit('FAIL no %s in the batch' % name)
                if verb == 'first':
                    found = found[:1]
                elif verb == 'last':
                    found = found[-1:]
                for item, values, at in found:
                    check_fields('%s at dword %d' % (verb, at), item, values, pairs_of(rest), verb == 'exact')
            elif verb in ('before-gpgpu', 'after-gpgpu', 'before-3d'):
                if len(gpgpu) != 1:
                    raise SystemExit('FAIL the batch switches to GPGPU %d times, not once' % len(gpgpu))
                where = {'before-gpgpu': gpgpu[0] - 1, 'after-gpgpu': gpgpu[0] + 1, 'before-3d': selects[-1] - 1}[verb]
                item, values, at = decoded[where]
                if item.name != tokens[1]:
                    raise SystemExit('FAIL %s is %s, not %s' % (verb, item.name, tokens[1]))
                check_fields(verb, item, values, pairs_of(tokens[2:]), False)
            else:
                raise SystemExit('FAIL unknown expect line: %s' % line.strip())
            checks += 1
    print('%s: %d instructions of %d dwords decoded by Mesa genxml (gen120), %d expectations hold' %
          (out.rsplit('/', 1)[-1], len(decoded), len(words), checks))


main()
