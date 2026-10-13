#!/usr/bin/env python3
#
# zedBSD
# Copyright (C) 2026 Awe Morris
#
# SPDX-License-Identifier: Zlib
#
# ws229-p004: decodes the render path's batches and state for Gen12 and Gen9 and compares them.
#
# Mesa's genxml is read as data through plan/ws083/tests/genxml-decode.py (its import resolution and its SHA-256
# pins): gen120.xml for the version 12 files, gen90.xml (with gen80.xml, gen75.xml ... it imports) for version 9.
# Nothing of Mesa is copied into the tree.
#
#   gen9-check.py GENXML-DIR OUT-DIR
#
# OUT-DIR holds VER-NAME.bin from state-test.c.  Every batch must decode instruction by instruction with the
# version's own genxml: each dword starts or continues an instruction the render engine has in that version, and no
# bit outside an instruction's fields is set.  Every structure (surface state, sampler, interface descriptor) must set
# no bit outside its fields.  Then the two versions are compared: the batches instruction by instruction, after the
# instructions one version has and the other has not (listed below); every field both versions name must hold the
# same value unless the difference is listed below with its reason, and a field only Gen12 names must be zero in
# the Gen12 file unless it is listed as one Gen9 does without.

import difflib
import importlib.util
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DECODER = os.path.join(HERE, '..', '..', '..', 'ws083', 'tests', 'genxml-decode.py')

# The batches and the structures state-test.c writes, and the genxml struct each structure is.
BATCHES = ('draw-depth', 'draw-indexed', 'rect-fill', 'rect-copy', 'dispatch-shared', 'dispatch-indirect')
STRUCTURES = {
    'rss-target': 'RENDER_SURFACE_STATE',
    'rss-texture': 'RENDER_SURFACE_STATE',
    'rss-depth': 'RENDER_SURFACE_STATE',
    'sampler-linear': 'SAMPLER_STATE',
    'sampler-border': 'SAMPLER_STATE',
    'idd-shared': 'INTERFACE_DESCRIPTOR_DATA',
    'idd-indirect': 'INTERFACE_DESCRIPTOR_DATA',
}

# The header fields that identify an instruction (the dword-0 fields with a default, other than the length).
LENGTH_FIELD = 'DWord Length'

# Instructions only the Gen12 batches have: Gen9 has no such command (gen90.xml), or programs it another way.
GEN12_ONLY = {
    '3DSTATE_VF_SGVS_2': 'Gen11+: the extended system-generated values',
    '3DSTATE_PRIMITIVE_REPLICATION': 'Gen12 only',
    '3DSTATE_DEPTH_BOUNDS': 'Gen12 only',
    '3DSTATE_CPS_POINTERS': 'Gen11+: coarse pixel shading',
    '3DSTATE_URB_ALLOC_VS': 'Gen12: Gen9 has 3DSTATE_URB_VS',
    '3DSTATE_URB_ALLOC_HS': 'Gen12: Gen9 has 3DSTATE_URB_HS',
    '3DSTATE_URB_ALLOC_DS': 'Gen12: Gen9 has 3DSTATE_URB_DS',
    '3DSTATE_URB_ALLOC_GS': 'Gen12: Gen9 has 3DSTATE_URB_GS',
}

# Instructions only the Gen9 batches have.
GEN9_ONLY = {
    '3DSTATE_URB_VS': 'the Gen9 URB allocation',
    '3DSTATE_URB_HS': 'the Gen9 URB allocation',
    '3DSTATE_URB_DS': 'the Gen9 URB allocation',
    '3DSTATE_URB_GS': 'the Gen9 URB allocation',
    'MI_LOAD_REGISTER_IMM': 'L3CNTLREG: the shared local memory lives in the L3 on Gen9',
    'PIPE_CONTROL': 'the flushes around an L3 change, and the PIPELINE_SELECT flushes Gen9 splits in two',
    '3DSTATE_CC_STATE_POINTERS': 'the colour calc pointer cleared before PIPELINE_SELECT to GPGPU (SKL PRM)',
    'MEDIA_VFE_STATE': 'the VFE state re-emitted before PIPELINE_SELECT to 3D (anv, Gen9)',
}

# Fields both versions name whose values differ, by instruction or structure, with the reason.  Every field whose
# name has MOCS in it may differ too: the uncached entry is index 3 on Gen12 and index 0 on Gen9 (Linux
# skl_mocs_table, isl).
DIFFERENT = {
    ('STATE_BASE_ADDRESS', 'DWord Length'): '19 dwords on Gen9 (no bindless sampler state base), 22 on Gen12',
    ('3DSTATE_STENCIL_BUFFER', 'DWord Length'): 'five dwords on Gen9, eight on Gen12',
    ('PIPELINE_SELECT', 'Mask Bits'): 'anv: 0x13 on Gen12, 0x3 on Gen9 (bits 1:0 only)',
    ('PIPELINE_SELECT', 'Media Sampler DOP Clock Gate Enable'): 'anv sets it on Gen12 only',
    ('PIPE_CONTROL', 'Post Sync Operation'): 'Gen9: the VF cache invalidation needs a post-sync write (SKL PRM)',
    ('PIPE_CONTROL', 'Address'): 'the address of that post-sync write',
    ('PIPE_CONTROL', 'DC Flush Enable'): 'Gen9 flushes the data cache where Gen12 flushes the HDC pipeline (anv)',
    ('3DSTATE_VS', 'Maximum Number of Threads'): '336 vertex threads on Gen9 (Mesa GFX9_HW_INFO), 546 on Gen12',
    ('MEDIA_VFE_STATE', 'Maximum Number of Threads'): '56 threads a subslice on Gen9, 112 a dual-subslice on Gen12',
    ('3DSTATE_DEPTH_BUFFER', 'Surface Type'): 'Gen9 types a stencil-only depth buffer 2D (isl, GFX_VER < 12)',
    ('3DSTATE_DEPTH_BUFFER', 'Width'): 'as the Surface Type',
    ('3DSTATE_DEPTH_BUFFER', 'Height'): 'as the Surface Type',
}

# Fields only Gen12 names that the Gen12 files may set: Gen9 has no such bit and does without it.
GEN12_FIELDS_DROPPED = {
    ('PIPE_CONTROL', 'HDC Pipeline Flush Enable'): 'Gen12 only; Gen9 flushes the data cache instead',
    ('STATE_BASE_ADDRESS', 'Bindless Sampler State Base Address Modify Enable'): 'Gen12 only',
    ('STATE_BASE_ADDRESS', 'Bindless Sampler State MOCS'): 'Gen12 only',
    ('VERTEX_BUFFER_STATE', 'L3 Bypass Disable'): 'Gen12 only',
    ('3DSTATE_VERTEX_BUFFERS', 'Vertex Buffer State.L3 Bypass Disable'): 'Gen12 only',
    ('3DSTATE_INDEX_BUFFER', 'L3 Bypass Disable'): 'Gen12 only',
    ('RENDER_SURFACE_STATE', 'Enable Unorm Path In Color Pipe'): 'Gen12 only',
    ('INTERFACE_DESCRIPTOR_DATA', 'Thread Preemption Disable'): 'Gen12 only; Gen9 has no mid-thread preemption to disable',
    ('3DSTATE_STENCIL_BUFFER', 'Stencil Write Enable'): 'moved to 3DSTATE_DEPTH_BUFFER on Gen9',
    ('3DSTATE_STENCIL_BUFFER', 'Surface Type'): 'Gen9 has Stencil Buffer Enable instead',
    ('3DSTATE_STENCIL_BUFFER', 'Width'): 'Gen9 takes the extent from 3DSTATE_DEPTH_BUFFER',
    ('3DSTATE_STENCIL_BUFFER', 'Height'): 'as the Width',
    ('3DSTATE_STENCIL_BUFFER', 'Depth'): 'as the Width',
    ('3DSTATE_STENCIL_BUFFER', 'Minimum Array Element'): 'as the Width',
    ('3DSTATE_STENCIL_BUFFER', 'Render Target View Extent'): 'as the Width',
    ('3DSTATE_SF', 'Deref Block Size'): 'Gen12 only (intel_get_urb_config)',
}

# Fields only Gen9 names that the Gen9 files may set.
GEN9_FIELDS_ADDED = {
    ('3DSTATE_STENCIL_BUFFER', 'Stencil Buffer Enable'): 'the Gen9 form of a stencil buffer',
    ('3DSTATE_DEPTH_BUFFER', 'Stencil Write Enable'): 'Gen9 enables the stencil write in the depth buffer packet',
    ('MEDIA_VFE_STATE', 'Reset Gateway Timer'): 'anv sets it below Gen11',
}


def fail(message):
    sys.stderr.write('FAIL ' + message + '\n')
    sys.exit(1)


def load_decoder():
    spec = importlib.util.spec_from_file_location('genxml_decode', DECODER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Item:
    """One instruction or structure of a resolved genxml: its fields and how a dword 0 identifies it."""

    def __init__(self, decoder, xml, element):
        self.name = element.attrib['name']
        self.element = element
        self.xml = xml
        self.decoder = decoder
        self.bias = int(element.attrib.get('bias', '2'))
        self.length = int(element.attrib.get('length', '0'))
        self.mask = 0
        self.value = 0
        self.length_bits = None
        for child in element:
            if child.tag != 'field':
                continue
            start = int(child.attrib['start'])
            end = int(child.attrib['end'])
            if child.attrib['name'] == LENGTH_FIELD:
                self.length_bits = (start, end)
                continue
            if 'default' in child.attrib and end < 32:
                bits = ((1 << (end - start + 1)) - 1) << start
                self.mask |= bits
                self.value |= (int(child.attrib['default'], 0) << start) & bits

    def dwords(self, first):
        if self.length_bits is None:
            return self.length
        start, end = self.length_bits
        return ((first >> start) & ((1 << (end - start + 1)) - 1)) + self.bias

    def walk(self, element, whole, total, base, prefix, fields):
        """Decodes the fields under element from bit base; returns the bits they cover.

        A field past the end of the dwords given is absent (3DPRIMITIVE's
        extended parameters), and a group of count 0 repeats to the end.
        """
        covered = 0
        for child in element:
            if child.tag == 'field':
                start = base + int(child.attrib['start'])
                end = base + int(child.attrib['end'])
                if end >= total:
                    continue
                width = end - start + 1
                value = (whole >> start) & ((1 << width) - 1)
                kind = child.attrib.get('type', 'uint')
                if kind in ('address', 'offset'):
                    value <<= start % 32
                sub = self.xml.struct(kind)
                if sub is not None:
                    # A field typed by a structure covers only the bits of the structure's own fields.
                    covered |= self.walk(sub, whole, total, start, prefix + child.attrib['name'] + '.', fields)
                    continue
                covered |= ((1 << width) - 1) << start
                fields[prefix + child.attrib['name']] = value
            elif child.tag == 'group':
                count = int(child.attrib['count'])
                size = int(child.attrib['size'])
                start = base + int(child.attrib['start'])
                index = 0
                while (count == 0 or index < count) and start + (index + 1) * size <= total:
                    group = {}
                    covered |= self.walk(child, whole, total, start + index * size, '', group)
                    for key, value in group.items():
                        fields['%s%s[%d]' % (prefix, key, index)] = value
                    index += 1
        return covered

    def decode(self, words):
        fields = {}
        whole = 0
        for index, word in enumerate(words):
            whole |= word << (32 * index)
        covered = self.walk(self.element, whole, 32 * len(words), 0, '', fields)
        stray = whole & ~covered
        return fields, stray


def render_capable(element):
    engine = element.attrib.get('engine')
    if engine is None:
        return True
    return 'render' in engine.split('|')


def load(decoder, directory, root):
    decoder.xml_directory = directory
    xml = decoder.GenXml(directory, root)
    instructions = []
    structures = {}
    for (tag, name), element in xml.items.items():
        if tag == 'instruction' and render_capable(element):
            instructions.append(Item(decoder, xml, element))
        elif tag == 'struct':
            structures[name] = Item(decoder, xml, element)
    return instructions, structures


def parse(label, words, instructions):
    """Decodes a batch into [(name, fields)], refusing an unknown dword or a stray bit."""
    decoded = []
    at = 0
    while at < len(words):
        matches = [item for item in instructions if item.mask != 0 and (words[at] & item.mask) == item.value]
        if not matches:
            fail('%s: dword %d (0x%08x) starts no instruction of this version' % (label, at, words[at]))
        best = max(bin(item.mask).count('1') for item in matches)
        matches = [item for item in matches if bin(item.mask).count('1') == best]
        if len(matches) != 1:
            fail('%s: dword %d (0x%08x) is ambiguous: %s' % (label, at, words[at], [m.name for m in matches]))
        item = matches[0]
        count = item.dwords(words[at])
        if at + count > len(words):
            fail('%s: %s at dword %d runs past the batch' % (label, item.name, at))
        fields, stray = item.decode(words[at:at + count])
        if stray != 0:
            fail('%s: %s at dword %d sets bits outside its fields: 0x%x' % (label, item.name, at, stray))
        decoded.append((item.name, fields))
        at += count
    return decoded


def read_words(path):
    with open(path, 'rb') as handle:
        data = handle.read()
    if len(data) % 4 != 0:
        fail('%s is not whole dwords' % path)
    return list(struct.unpack('<%dI' % (len(data) // 4), data))


def listed(table, name, field):
    if table is DIFFERENT and 'MOCS' in field:
        return True
    if (name, field) in table:
        return True
    if ('*', field) in table:
        return True
    return False


def compare_fields(label, name, old, new, problems):
    """Compares one Gen12 item's fields with its Gen9 counterpart's."""
    for field in sorted(set(old) | set(new)):
        if field in old and field in new:
            if old[field] != new[field] and not listed(DIFFERENT, name, field.split('[')[0]):
                problems.append('%s: %s %s is 0x%x on Gen12, 0x%x on Gen9' % (label, name, field, old[field], new[field]))
        elif field in old:
            if old[field] != 0 and not listed(GEN12_FIELDS_DROPPED, name, field.split('[')[0]):
                problems.append('%s: %s %s = 0x%x on Gen12 has no Gen9 field' % (label, name, field, old[field]))
        else:
            if new[field] != 0 and not listed(GEN9_FIELDS_ADDED, name, field.split('[')[0]):
                problems.append('%s: %s %s = 0x%x on Gen9 has no Gen12 field' % (label, name, field, new[field]))


# PIPE_CONTROL fields that differ by version on purpose, left out when PIPE_CONTROLs are matched up.
PIPE_CONTROL_VERSION_FIELDS = ('HDC Pipeline Flush Enable', 'DC Flush Enable', 'Post Sync Operation', 'Address')


def align_key(item):
    """What two batches are aligned by: the instruction, and for a PIPE_CONTROL the flags it sets."""
    name, fields = item
    if name != 'PIPE_CONTROL':
        return name
    flags = sorted(field for field, value in fields.items()
                   if value != 0 and field not in PIPE_CONTROL_VERSION_FIELDS and not field.startswith('Command '))
    return name + ':' + ','.join(flags)


def compare_batches(label, old, new, problems):
    """Aligns two decoded batches by instruction and compares the matched ones."""
    names_old = [name for name, _ in old]
    names_new = [name for name, _ in new]
    matcher = difflib.SequenceMatcher(a=[align_key(item) for item in old], b=[align_key(item) for item in new],
                                      autojunk=False)
    for opcode, a0, a1, b0, b1 in matcher.get_opcodes():
        if opcode == 'equal':
            for index in range(a1 - a0):
                compare_fields('%s #%d' % (label, a0 + index), names_old[a0 + index], old[a0 + index][1],
                               new[b0 + index][1], problems)
            continue
        for name in names_old[a0:a1]:
            if name not in GEN12_ONLY:
                problems.append('%s: Gen12 %s has no Gen9 counterpart' % (label, name))
        for name in names_new[b0:b1]:
            if name not in GEN9_ONLY:
                problems.append('%s: Gen9 %s has no Gen12 counterpart' % (label, name))


def main():
    if len(sys.argv) != 3:
        fail('usage: gen9-check.py GENXML-DIR OUT-DIR')
    directory, out = sys.argv[1], sys.argv[2]
    decoder = load_decoder()
    gen12_instructions, gen12_structures = load(decoder, directory, 'gen120.xml')
    gen9_instructions, gen9_structures = load(decoder, directory, 'gen90.xml')
    problems = []

    # The batches: each decodes in its own version, then the two are compared.
    instructions = 0
    for name in BATCHES:
        old = parse('12-' + name, read_words(os.path.join(out, '12-%s.bin' % name)), gen12_instructions)
        new = parse('9-' + name, read_words(os.path.join(out, '9-%s.bin' % name)), gen9_instructions)
        instructions += len(old) + len(new)
        compare_batches(name, old, new, problems)

    # The structures, likewise.
    for name, kind in STRUCTURES.items():
        item12 = gen12_structures[kind]
        item9 = gen9_structures[kind]
        old, stray = item12.decode(read_words(os.path.join(out, '12-%s.bin' % name)))
        if stray != 0:
            fail('12-%s: %s sets bits outside its fields: 0x%x' % (name, kind, stray))
        new, stray = item9.decode(read_words(os.path.join(out, '9-%s.bin' % name)))
        if stray != 0:
            fail('9-%s: %s sets bits outside its fields: 0x%x' % (name, kind, stray))
        compare_fields(name, kind, old, new, problems)

    for problem in problems:
        sys.stderr.write('FAIL ' + problem + '\n')
    if problems:
        sys.exit(1)
    print('genxml: %d batches (%d instructions) and %d structures decode with gen120.xml and gen90.xml; '
          'every field agrees or differs as listed' % (len(BATCHES) * 2, instructions, len(STRUCTURES) * 2))


if __name__ == '__main__':
    main()
