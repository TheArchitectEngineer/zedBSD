/*
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

#define PARITY_LRC_NOP(x)            ((uint8_t)(0x80u | (x)))
#define PARITY_LRC_LRI(count, flags) ((uint8_t)(((flags) << 6) | (count)))
#define PARITY_LRC_POSTED            1u
#define PARITY_LRC_REG(x)            ((uint8_t)((x) >> 2))
#define PARITY_LRC_REG16(x) \
	((uint8_t)(((x) >> 9) | 0x80u)), ((uint8_t)(((x) >> 2) & 0x7fu))
#define PARITY_LRC_END               0

static const uint8_t parity_gen12_rcs_offsets[] = {
	PARITY_LRC_NOP(1),
	PARITY_LRC_LRI(13, PARITY_LRC_POSTED),
	PARITY_LRC_REG16(0x244),
	PARITY_LRC_REG(0x034),
	PARITY_LRC_REG(0x030),
	PARITY_LRC_REG(0x038),
	PARITY_LRC_REG(0x03c),
	PARITY_LRC_REG(0x168),
	PARITY_LRC_REG(0x140),
	PARITY_LRC_REG(0x110),
	PARITY_LRC_REG(0x1c0),
	PARITY_LRC_REG(0x1c4),
	PARITY_LRC_REG(0x1c8),
	PARITY_LRC_REG(0x180),
	PARITY_LRC_REG16(0x2b4),

	PARITY_LRC_NOP(5),
	PARITY_LRC_LRI(9, PARITY_LRC_POSTED),
	PARITY_LRC_REG16(0x3a8),
	PARITY_LRC_REG16(0x28c),
	PARITY_LRC_REG16(0x288),
	PARITY_LRC_REG16(0x284),
	PARITY_LRC_REG16(0x280),
	PARITY_LRC_REG16(0x27c),
	PARITY_LRC_REG16(0x278),
	PARITY_LRC_REG16(0x274),
	PARITY_LRC_REG16(0x270),

	PARITY_LRC_LRI(3, PARITY_LRC_POSTED),
	PARITY_LRC_REG(0x1b0),
	PARITY_LRC_REG16(0x5a8),
	PARITY_LRC_REG16(0x5ac),

	PARITY_LRC_NOP(6),
	PARITY_LRC_LRI(1, 0),
	PARITY_LRC_REG(0x0c8),
	PARITY_LRC_NOP(3 + 9 + 1),

	PARITY_LRC_LRI(51, PARITY_LRC_POSTED),
	PARITY_LRC_REG16(0x588),
	PARITY_LRC_REG16(0x588),
	PARITY_LRC_REG16(0x588),
	PARITY_LRC_REG16(0x588),
	PARITY_LRC_REG16(0x588),
	PARITY_LRC_REG16(0x588),
	PARITY_LRC_REG(0x028),
	PARITY_LRC_REG(0x09c),
	PARITY_LRC_REG(0x0c0),
	PARITY_LRC_REG(0x178),
	PARITY_LRC_REG(0x17c),
	PARITY_LRC_REG16(0x358),
	PARITY_LRC_REG(0x170),
	PARITY_LRC_REG(0x150),
	PARITY_LRC_REG(0x154),
	PARITY_LRC_REG(0x158),
	PARITY_LRC_REG16(0x41c),
	PARITY_LRC_REG16(0x600),
	PARITY_LRC_REG16(0x604),
	PARITY_LRC_REG16(0x608),
	PARITY_LRC_REG16(0x60c),
	PARITY_LRC_REG16(0x610),
	PARITY_LRC_REG16(0x614),
	PARITY_LRC_REG16(0x618),
	PARITY_LRC_REG16(0x61c),
	PARITY_LRC_REG16(0x620),
	PARITY_LRC_REG16(0x624),
	PARITY_LRC_REG16(0x628),
	PARITY_LRC_REG16(0x62c),
	PARITY_LRC_REG16(0x630),
	PARITY_LRC_REG16(0x634),
	PARITY_LRC_REG16(0x638),
	PARITY_LRC_REG16(0x63c),
	PARITY_LRC_REG16(0x640),
	PARITY_LRC_REG16(0x644),
	PARITY_LRC_REG16(0x648),
	PARITY_LRC_REG16(0x64c),
	PARITY_LRC_REG16(0x650),
	PARITY_LRC_REG16(0x654),
	PARITY_LRC_REG16(0x658),
	PARITY_LRC_REG16(0x65c),
	PARITY_LRC_REG16(0x660),
	PARITY_LRC_REG16(0x664),
	PARITY_LRC_REG16(0x668),
	PARITY_LRC_REG16(0x66c),
	PARITY_LRC_REG16(0x670),
	PARITY_LRC_REG16(0x674),
	PARITY_LRC_REG16(0x678),
	PARITY_LRC_REG16(0x67c),
	PARITY_LRC_REG(0x068),
	PARITY_LRC_REG(0x084),
	PARITY_LRC_NOP(1),

	PARITY_LRC_END
};

static const uint8_t parity_gen12_xcs_offsets[] = {
	PARITY_LRC_NOP(1),
	PARITY_LRC_LRI(13, PARITY_LRC_POSTED),
	PARITY_LRC_REG16(0x244),
	PARITY_LRC_REG(0x034),
	PARITY_LRC_REG(0x030),
	PARITY_LRC_REG(0x038),
	PARITY_LRC_REG(0x03c),
	PARITY_LRC_REG(0x168),
	PARITY_LRC_REG(0x140),
	PARITY_LRC_REG(0x110),
	PARITY_LRC_REG(0x1c0),
	PARITY_LRC_REG(0x1c4),
	PARITY_LRC_REG(0x1c8),
	PARITY_LRC_REG(0x180),
	PARITY_LRC_REG16(0x2b4),

	PARITY_LRC_NOP(5),
	PARITY_LRC_LRI(9, PARITY_LRC_POSTED),
	PARITY_LRC_REG16(0x3a8),
	PARITY_LRC_REG16(0x28c),
	PARITY_LRC_REG16(0x288),
	PARITY_LRC_REG16(0x284),
	PARITY_LRC_REG16(0x280),
	PARITY_LRC_REG16(0x27c),
	PARITY_LRC_REG16(0x278),
	PARITY_LRC_REG16(0x274),
	PARITY_LRC_REG16(0x270),

	PARITY_LRC_END
};
