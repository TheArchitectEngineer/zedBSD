#!/usr/bin/env python3
#
# zedBSD
# Copyright (C) 2026 Awe Morris
#
# SPDX-License-Identifier: Zlib
#
# An independent decoder of Gen12 video command streams (ws083 D24).
#
# It reads the genxml files of a Mesa source tree as data, resolves their
# imports the way genxml defines them (an item defined in a file replaces
# the imported item of the same tag and name, an <exclude> drops one, the
# imports are resolved depth first), and decodes a batch of dwords back into
# instructions and field values.  The builder in the kernel
# (src/drivers/gpu/i915/render/video-mfx.c) is checked against what this
# decoder reads, so a wrong bit position in the builder's transcription
# (intel/genxml-video.h) is not hidden by a golden made from the same
# transcription.  Nothing of Mesa is copied into the tree: the XML is read
# at run time from the directory given.
#
# The Mesa tree is pinned by the SHA-256 of the files used; a missing
# or different file fails loudly.
#
# Usage:
#   genxml-decode.py --mesa DIR --self-test
#   genxml-decode.py --mesa DIR --dump INSTRUCTION
#   genxml-decode.py --mesa DIR --batch FILE --expect FILE
#
# The batch file is little-endian dwords.  The expect file has one check a
# line, fields separated by tabs:
#   I	<index>	<instruction name>		(the index-th instruction is this one)
#   F	<index>	<field name>	<value>		(a field of the index-th instruction)
#   N	<count>					(the batch has this many instructions)
# A value is decimal or 0x hexadecimal; a negative decimal is a signed
# field.  A field of a group is named "<name>[<n>]", a field of a struct
# field "<name>.<subfield>".

import argparse
import hashlib
import os
import struct
import sys
import xml.etree.ElementTree as ET

# The genxml files the decoder reads and their SHA-256 (Mesa 25.0.7, the
# Debian source package 25.0.7-2+deb13u1; design.md §7): gen120.xml and the
# whole chain it imports.
PINNED = {
    'gen120.xml': 'e2452c7dd2d19f9c506f487ce98e938984b6bdf8a3ea2504c64afdd4facd542e',
    'gen110.xml': '6598e556ffedf4fe051c78af3bb08030a39af865c76e2784dba5374646fce35d',
    'gen90.xml': 'd86fb566b9292280e2d6a385107711fb7ee8008e5c54bb3ba70a2c0343262ddb',
    'gen80.xml': '2962677cf69dc947345fd88bd7010427900160eb7a7b076463e6e8d28772439d',
    'gen75.xml': 'a56886791a06675a2d0b9f578935c8c4fb5d8b172acc15548fc332bc027f12e0',
    'gen70.xml': 'dd7c942fc12afd2defdc435997ccdb5b48841d344f40625dbb2b4e746eca09ef',
    'gen60.xml': '30fac841448b4239bbaedf92a77424ec054629d28f266c7190b3f592f4b9185c',
    'gen50.xml': '79eec59bd4c5a0b63afa12ebe1834c5bf377c4f17df2d289e7ad06c3b0f72c45',
    'gen45.xml': 'ac0c117b87bc7b317351fd2328d21bc38bcc8156a7098ebff6a8537aab6da43f',
    'gen40.xml': '8fe6663fa39cdfc6cd1cc4481e464824c2dcc5ea7d7d1a4d2e4a5791aa32f206',
}

# The item tags that take part in import resolution.
ITEM_TAGS = ('enum', 'struct', 'instruction', 'register')

# The header fields that identify an instruction, with their defaults.
HEADER_FIELDS = ('Command Type', 'Pipeline', 'Media Command Opcode', 'SubOpcode A',
                 'SubOpcode B', 'SubOpcode', 'Command Subtype', 'MI Command Opcode')


def fail(message):
    """Stops with a message on stderr."""
    sys.stderr.write('genxml-decode: ' + message + '\n')
    sys.exit(2)


class GenXml:
    """The resolved items of one genxml file and everything it imports."""

    def __init__(self, directory, name, loading=None):
        if loading is None:
            loading = set()
        if name in loading:
            fail('import cycle at ' + name)
        loading.add(name)

        path = os.path.join(directory, name)
        if name not in PINNED:
            fail('file not pinned: ' + name)
        try:
            with open(path, 'rb') as handle:
                data = handle.read()
        except OSError as error:
            fail('cannot read %s: %s' % (path, error))
        digest = hashlib.sha256(data).hexdigest()
        if digest != PINNED[name]:
            fail('%s has SHA-256 %s, expected %s' % (path, digest, PINNED[name]))

        root = ET.fromstring(data)
        self.items = {}

        # The file's own items win over anything imported.
        for element in root:
            if element.tag in ITEM_TAGS:
                self.items[(element.tag, element.attrib['name'])] = element

        # The imports, each without its excluded names.
        for element in root:
            if element.tag != 'import':
                continue
            excluded = set()
            for child in element:
                if child.tag != 'exclude':
                    fail('unexpected <%s> in an import of %s' % (child.tag, name))
                excluded.add(child.attrib['name'])
            imported = GenXml(directory, element.attrib['name'], loading)
            for key, item in imported.items.items():
                if key[1] in excluded:
                    continue
                if key in self.items:
                    continue
                self.items[key] = item

        loading.discard(name)
        self.name = name

    def instruction(self, name):
        return self.items.get(('instruction', name))

    def struct(self, name):
        return self.items.get(('struct', name))

    def instructions(self):
        return [item for key, item in self.items.items() if key[0] == 'instruction']


def bits(words, start, end):
    """Reads bits start..end (inclusive) of a little-endian dword array."""
    value = 0
    for bit in range(start, end + 1):
        word = bit // 32
        if word >= len(words):
            fail('field bit %d past the instruction' % bit)
        if (words[word] >> (bit % 32)) & 1:
            value |= 1 << (bit - start)
    return value


def field_value(xml, words, field, start, end, prefix, out):
    """Decodes one field at absolute bits start..end into out."""
    kind = field.attrib.get('type', 'uint')
    raw = bits(words, start, end)
    width = end - start + 1
    name = prefix + field.attrib['name']

    if kind == 'address' or kind == 'offset':
        # An address keeps its bits where they are: the value is the address.
        out[name] = raw << (start % 32)
        return
    if kind == 'int' or kind.startswith('s'):
        if kind == 'int' or kind[1:2].isdigit():
            if raw & (1 << (width - 1)):
                raw -= 1 << width
        out[name] = raw
        return
    if kind in ('uint', 'bool', 'mbo', 'mbz', 'float') or kind.startswith('u'):
        out[name] = raw
        return

    # A field typed by a struct: its fields relative to the field's start.
    sub = xml.struct(kind)
    if sub is None:
        out[name] = raw
        return
    walk_fields(xml, words, sub, start, name + '.', out)


def walk_fields(xml, words, element, base, prefix, out):
    """Decodes the fields and groups of an instruction or struct from bit base."""
    for child in element:
        if child.tag == 'field':
            field_value(xml, words, child,
                        base + int(child.attrib['start']),
                        base + int(child.attrib['end']), prefix, out)
        elif child.tag == 'group':
            count = int(child.attrib['count'])
            size = int(child.attrib['size'])
            start = int(child.attrib['start'])
            for index in range(count):
                group = {}
                walk_fields(xml, words, child, base + start + index * size, '', group)
                for key, value in group.items():
                    out['%s%s[%d]' % (prefix, key, index)] = value


def header_defaults(instruction):
    """The identifying header fields of an instruction: (start, end, default)."""
    found = []
    for child in instruction:
        if child.tag != 'field':
            continue
        if child.attrib['name'] in HEADER_FIELDS and 'default' in child.attrib:
            found.append((int(child.attrib['start']), int(child.attrib['end']),
                          int(child.attrib['default'], 0)))
    return found


def length_field(instruction):
    for child in instruction:
        if child.tag == 'field' and child.attrib['name'] == 'DWord Length':
            return int(child.attrib['start']), int(child.attrib['end'])
    return None


def video_capable(instruction):
    engine = instruction.attrib.get('engine')
    if engine is None:
        return True
    return 'video' in engine.split('|')


def identify(xml, word):
    """Finds the one video-capable instruction whose header matches a dword."""
    matches = []
    for instruction in xml.instructions():
        if not video_capable(instruction):
            continue
        header = header_defaults(instruction)
        if not header:
            continue
        hit = True
        for start, end, default in header:
            if bits([word], start, end) != default:
                hit = False
                break
        if hit:
            matches.append((len(header), instruction))
    if not matches:
        return None
    matches.sort(key=lambda pair: -pair[0])
    if len(matches) > 1 and matches[0][0] == matches[1][0]:
        names = [m[1].attrib['name'] for m in matches if m[0] == matches[0][0]]
        fail('dword 0x%08x matches several instructions: %s' % (word, ', '.join(names)))
    return matches[0][1]


def decode(xml, words):
    """Decodes a batch into a list of (name, dwords, fields)."""
    result = []
    at = 0
    while at < len(words):
        instruction = identify(xml, words[at])
        if instruction is None:
            fail('unknown instruction 0x%08x at dword %d' % (words[at], at))
        bias = int(instruction.attrib.get('bias', '2'))
        length_bits = length_field(instruction)
        if length_bits is None:
            length = int(instruction.attrib['length'])
        else:
            length = bits([words[at]], length_bits[0], length_bits[1]) + bias
        if at + length > len(words):
            fail('%s at dword %d runs past the batch' % (instruction.attrib['name'], at))
        body = words[at:at + length]
        fields = {}
        walk_fields(xml, body, instruction, 0, '', fields)
        result.append((instruction.attrib['name'], length, fields))
        at += length
    return result


def parse_value(text):
    text = text.strip()
    if text.startswith('-'):
        return -int(text[1:], 0)
    return int(text, 0)


def check(xml, batch_path, expect_path):
    with open(batch_path, 'rb') as handle:
        data = handle.read()
    if len(data) % 4 != 0:
        fail('batch is not whole dwords')
    words = list(struct.unpack('<%dI' % (len(data) // 4), data))
    decoded = decode(xml, words)
    failures = 0
    checks = 0
    with open(expect_path, 'r', encoding='utf-8') as handle:
        for number, line in enumerate(handle, 1):
            line = line.rstrip('\n')
            if not line or line.startswith('#'):
                continue
            parts = line.split('\t')
            checks += 1
            if parts[0] == 'N':
                if len(decoded) != int(parts[1]):
                    sys.stderr.write('line %d: %d instructions, expected %s\n' % (number, len(decoded), parts[1]))
                    failures += 1
            elif parts[0] == 'I':
                index = int(parts[1])
                if index >= len(decoded) or decoded[index][0] != parts[2]:
                    got = decoded[index][0] if index < len(decoded) else 'nothing'
                    sys.stderr.write('line %d: instruction %d is %s, expected %s\n' % (number, index, got, parts[2]))
                    failures += 1
            elif parts[0] == 'F':
                index = int(parts[1])
                if index >= len(decoded):
                    sys.stderr.write('line %d: no instruction %d\n' % (number, index))
                    failures += 1
                    continue
                fields = decoded[index][2]
                if parts[2] not in fields:
                    sys.stderr.write('line %d: %s has no field "%s"\n' % (number, decoded[index][0], parts[2]))
                    failures += 1
                    continue
                if fields[parts[2]] != parse_value(parts[3]):
                    sys.stderr.write('line %d: %s[%d] "%s" = %s, expected %s\n' % (
                        number, decoded[index][0], index, parts[2], hex(fields[parts[2]]), parts[3]))
                    failures += 1
            else:
                fail('line %d: unknown check %s' % (number, parts[0]))
    print('genxml-decode: %d instructions, %d checks, %d failures' % (len(decoded), checks, failures))
    return failures


def self_test(xml):
    """Checks the import resolution on facts known from the files (D24)."""
    failures = 0

    def expect(what, got, wanted):
        nonlocal failures
        if got != wanted:
            sys.stderr.write('self-test: %s is %r, expected %r\n' % (what, got, wanted))
            failures += 1

    def length(name):
        instruction = xml.instruction(name)
        if instruction is None:
            return None
        return int(instruction.attrib['length'])

    # Gen12 takes gen110's 21-dword image state, not gen80's 14-dword one.
    expect('MFX_AVC_IMG_STATE length', length('MFX_AVC_IMG_STATE'), 21)
    expect('MFD_AVC_SLICEADDR length', length('MFD_AVC_SLICEADDR'), 4)
    expect('MFD_AVC_BSD_OBJECT length', length('MFD_AVC_BSD_OBJECT'), 7)
    expect('MFX_PIPE_BUF_ADDR_STATE length', length('MFX_PIPE_BUF_ADDR_STATE'), 65)
    expect('MFX_IND_OBJ_BASE_ADDR_STATE length', length('MFX_IND_OBJ_BASE_ADDR_STATE'), 26)
    expect('MFX_BSP_BUF_BASE_ADDR_STATE length', length('MFX_BSP_BUF_BASE_ADDR_STATE'), 10)
    expect('MFX_AVC_DIRECTMODE_STATE length', length('MFX_AVC_DIRECTMODE_STATE'), 71)
    expect('MFD_AVC_DPB_STATE length', length('MFD_AVC_DPB_STATE'), 27)
    expect('MFD_AVC_PICID_STATE length', length('MFD_AVC_PICID_STATE'), 10)
    expect('MFX_QM_STATE length', length('MFX_QM_STATE'), 18)
    expect('MFX_SURFACE_STATE length', length('MFX_SURFACE_STATE'), 6)
    expect('MFX_PIPE_MODE_SELECT length', length('MFX_PIPE_MODE_SELECT'), 5)
    expect('MI_FORCE_WAKEUP length', length('MI_FORCE_WAKEUP'), 2)
    expect('MI_FLUSH_DW length', length('MI_FLUSH_DW'), 5)
    expect('MFX_WAIT length', length('MFX_WAIT'), 1)

    # Which file each one comes from: the resolved element is the one that
    # file defines itself (compared by its serialized XML).
    def defined_in(name, tag, item):
        root = ET.parse(os.path.join(xml_directory, name)).getroot()
        for element in root:
            if element.tag == tag and element.attrib.get('name') == item:
                return ET.tostring(element) == ET.tostring(xml.items[(tag, item)])
        return False

    expect('MFX_AVC_IMG_STATE from gen110', defined_in('gen110.xml', 'instruction', 'MFX_AVC_IMG_STATE'), True)
    expect('MFD_AVC_DPB_STATE from gen90', defined_in('gen90.xml', 'instruction', 'MFD_AVC_DPB_STATE'), True)
    expect('MFX_AVC_DIRECTMODE_STATE from gen80', defined_in('gen80.xml', 'instruction', 'MFX_AVC_DIRECTMODE_STATE'), True)
    expect('MFD_AVC_PICID_STATE from gen75', defined_in('gen75.xml', 'instruction', 'MFD_AVC_PICID_STATE'), True)
    expect('MI_FORCE_WAKEUP from gen120', defined_in('gen120.xml', 'instruction', 'MI_FORCE_WAKEUP'), True)

    # MFX_WAIT's Command Subtype default is 1 (gen75.xml).
    wait = xml.instruction('MFX_WAIT')
    subtype = None
    for child in wait:
        if child.tag == 'field' and child.attrib['name'] == 'Command Subtype':
            subtype = int(child.attrib['default'])
    expect('MFX_WAIT Command Subtype default', subtype, 1)

    # gen110's 3DSTATE_CPS is excluded by gen120 and not defined again.
    expect('3DSTATE_CPS excluded', xml.instruction('3DSTATE_CPS') is None, True)

    # A decode of a hand-made MFX_WAIT and a MI_FLUSH_DW.
    decoded = decode(xml, [0x68000100, 0x13000003, 0, 0, 0, 0])
    expect('decoded names', [d[0] for d in decoded], ['MFX_WAIT', 'MI_FLUSH_DW'])
    expect('MFX_WAIT sync flag', decoded[0][2]['MFX Sync Control Flag'], 1)

    print('genxml-decode self-test: %d failures' % failures)
    return failures


def dump(xml, name):
    instruction = xml.instruction(name)
    if instruction is None:
        instruction = xml.struct(name)
    if instruction is None:
        fail('no instruction or struct ' + name)
    print('%s length=%s bias=%s' % (name, instruction.attrib.get('length'), instruction.attrib.get('bias')))

    def show(element, indent):
        for child in element:
            if child.tag == 'field':
                print('%s%s [%s..%s] %s%s' % (indent, child.attrib['name'], child.attrib['start'], child.attrib['end'],
                                             child.attrib.get('type', 'uint'),
                                             (' default=' + child.attrib['default']) if 'default' in child.attrib else ''))
                for value in child:
                    if value.tag == 'value':
                        print('%s    %s = %s' % (indent, value.attrib['name'], value.attrib['value']))
            elif child.tag == 'group':
                print('%sgroup count=%s start=%s size=%s' % (indent, child.attrib['count'], child.attrib['start'], child.attrib['size']))
                show(child, indent + '  ')
    show(instruction, '  ')


def main():
    global xml_directory
    parser = argparse.ArgumentParser()
    parser.add_argument('--mesa', required=True, help='the Mesa source tree')
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--dump')
    parser.add_argument('--batch')
    parser.add_argument('--expect')
    arguments = parser.parse_args()

    xml_directory = os.path.join(arguments.mesa, 'src', 'intel', 'genxml')
    if not os.path.isdir(xml_directory):
        fail('no genxml directory under ' + arguments.mesa)
    xml = GenXml(xml_directory, 'gen120.xml')

    if arguments.self_test:
        sys.exit(1 if self_test(xml) else 0)
    if arguments.dump:
        dump(xml, arguments.dump)
        sys.exit(0)
    if arguments.batch and arguments.expect:
        sys.exit(1 if check(xml, arguments.batch, arguments.expect) else 0)
    parser.print_help()
    sys.exit(2)


xml_directory = None

if __name__ == '__main__':
    main()
