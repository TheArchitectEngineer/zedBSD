"""Generate the parity gen12 LRC offset tables straight from the 6.8.12 reference.

Hand-copying a 90-line encoded table is exactly the kind of step that introduces a
silent one-byte error, so the tables are EXTRACTED, not transcribed.  The only
edits are the macro renames and the u8 -> uint8_t type.
"""
import os, re

REF = os.path.expanduser(
    "~/zedBSD/plan/ws031/linux-parity/linux-reference/i915-src/gt/intel_lrc.c")
OUT = os.path.expanduser(
    "~/zedBSD/src/drivers/gpu/i915/parity/gt_lrc_offsets.inc")

src = open(REF).read()


def grab(name):
    m = re.search(r"^static const u8 %s\[\] = \{\n(.*?)^\};" % name, src, re.S | re.M)
    assert m, name
    return m.group(1)


def convert(body):
    body = body.replace("NOP(", "PARITY_LRC_NOP(")
    body = body.replace("LRI(", "PARITY_LRC_LRI(")
    body = body.replace("REG16(", "PARITY_LRC_REG16(")
    # REG( must not match the tail of PARITY_LRC_REG16( or of PARITY_LRC_REG(
    body = re.sub(r"(?<![_0-9A-Za-z])REG\(", "PARITY_LRC_REG(", body)
    body = re.sub(r"(?<![_0-9A-Za-z])POSTED(?![_0-9A-Za-z])", "PARITY_LRC_POSTED", body)
    body = re.sub(r"(?<![_0-9A-Za-z])END(?![_0-9A-Za-z])", "PARITY_LRC_END", body)
    return body


rcs = convert(grab("gen12_rcs_offsets"))
xcs = convert(grab("gen12_xcs_offsets"))

hdr = """/*
 * WS031 Linux-parity — gen12 LRC register-state offset tables.
 *
 * GENERATED from the 6.8.12 reference (gt/intel_lrc.c, gen12_rcs_offsets and
 * gen12_xcs_offsets) by scratchpad/gen_lrc_offsets.py.  The tables are extracted
 * rather than transcribed: a single wrong byte here silently mis-places a
 * register in the context image.  The only changes are the macro renames and
 * u8 -> uint8_t.
 *
 * Checked against the big-bang transcription (Linux 7.1): the encoded bytes are
 * IDENTICAL for both tables, so this part of the LRC layout did not move
 * between 6.8.12 and 7.1.
 *
 * Encoding (reference set_offsets()):
 *   bit 7 set          skip that many state dwords
 *   otherwise          bits 0-5 = register count, bits 6-7 = flags
 *   REG(x)             a 7-bit offset group
 *   REG16(x)           two 7-bit groups, high group first (bit 7 = continue)
 */
"""

body = hdr
body += "\n#define PARITY_LRC_NOP(x)            ((uint8_t)(0x80u | (x)))\n"
body += "#define PARITY_LRC_LRI(count, flags) ((uint8_t)(((flags) << 6) | (count)))\n"
body += "#define PARITY_LRC_POSTED            1u\n"
body += "#define PARITY_LRC_REG(x)            ((uint8_t)((x) >> 2))\n"
body += ("#define PARITY_LRC_REG16(x) \\\n"
         "\t((uint8_t)(((x) >> 9) | 0x80u)), ((uint8_t)(((x) >> 2) & 0x7fu))\n")
body += "#define PARITY_LRC_END               0\n\n"
body += "static const uint8_t parity_gen12_rcs_offsets[] = {\n" + rcs + "};\n\n"
body += "static const uint8_t parity_gen12_xcs_offsets[] = {\n" + xcs + "};\n"

open(OUT, "w").write(body)
print("wrote", OUT)
print("rcs entries:", rcs.count(","), " xcs entries:", xcs.count(","))
