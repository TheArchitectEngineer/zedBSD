/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The entropy decoding of a sequential Huffman scan (ITU T.81 F.2): the
 * tables made ready (a lookup of the first bits, then the longest codes
 * per length), the bits of the scan with its stuffed bytes, each block's
 * DC difference and AC run lengths, and the restart markers.  Each block
 * goes through the inverse DCT into its component's plane at once.
 *
 * Bad data does not stop the decoding, and goes as libjpeg's does: past a
 * marker the bits read as zeros, and once a code has used them the rest
 * of the segment's MCUs stay zero (uniform gray); a code that is in no
 * table is reported and read as 0.
 */

#include "internal.h"

#include <string.h>

/* The bits the accumulator is filled to (it holds 64). */
#define JPEG_BITS_FILL		56

static void jpeg_compat_fill_bits(j_decompress_ptr cinfo);
static void jpeg_compat_consume(j_decompress_ptr cinfo, int count);
static void jpeg_compat_reset_bits(j_decompress_ptr cinfo);
static int jpeg_compat_decode(j_decompress_ptr cinfo, const struct jpeg_huffman *huffman);
static int jpeg_compat_receive(j_decompress_ptr cinfo, int size);
static void jpeg_compat_decode_block(j_decompress_ptr cinfo, int index, JDIMENSION column, JDIMENSION row, int skip);
static void jpeg_compat_store_block(j_decompress_ptr cinfo, int index, JDIMENSION column, JDIMENSION row, const JCOEF *coefficients);
static void jpeg_compat_check_tables(j_decompress_ptr cinfo);
static void jpeg_compat_restart(j_decompress_ptr cinfo);
static void jpeg_compat_next_mcu(j_decompress_ptr cinfo, JDIMENSION done);

/*
 * Makes a Huffman table ready for decoding from its code counts and
 * values (the canonical codes of T.81 C.2).  A table whose codes do not
 * fit their lengths is an error.
 */
void
jpeg_compat_build_huffman(
	j_decompress_ptr cinfo,
	const JHUFF_TBL *table,
	struct jpeg_huffman *huffman)
{
	unsigned length;
	unsigned count;
	unsigned first;
	unsigned last;
	int32_t code;
	int32_t value;

	/* Each length's codes, in order: the lookup for the short ones, and the bounds for all. */
	memset(huffman, 0, sizeof(*huffman));
	memcpy(huffman->values, table->huffval, sizeof(huffman->values));
	code = 0;
	value = 0;
	for (length = 1; length <= 16U; length++) {
		huffman->valoffset[length] = value - code;
		for (count = 0; count < table->bits[length]; count++) {
			if (length <= JPEG_FAST_BITS) {
				/* Every lookup index that starts with the code. */
				first = (unsigned)code << (JPEG_FAST_BITS - length);
				last = first + (1U << (JPEG_FAST_BITS - length));
				for (; first < last; first++)
					huffman->fast[first] = (uint16_t)((length << 8) | table->huffval[value]);
			}

			/* The next code and value. */
			code++;
			value++;
		}

		/* The largest code of this length (-1 when it has none), which must fit the length. */
		huffman->maxcode[length] = code - 1;
		if (table->bits[length] == 0)
			huffman->maxcode[length] = -1;
		if (code > ((int32_t)1 << length))
			jpeg_compat_fail((j_common_ptr)cinfo, JERR_BAD_TABLE);
		code <<= 1;
	}

	/* Succeeded: the table can decode. */
	huffman->maxcode[17] = INT32_MAX;
	huffman->defined = 1;
}

/*
 * Decodes the scan whose header was just read: its MCUs (one block of
 * its component when it has one, else each component's blocks in turn),
 * with the restart markers between intervals.
 */
void
jpeg_compat_decode_scan(
	j_decompress_ptr cinfo)
{
	struct jpeg_decomp_master *master;
	jpeg_component_info *component;
	JDIMENSION across;
	JDIMENSION down;
	JDIMENSION column;
	JDIMENSION row;
	JDIMENSION done;
	int index;
	int slot;
	int skip;
	int h;
	int v;

	/* The tables it needs, and a clean start: no bits, predictions of 0, the first interval. */
	master = cinfo->master;
	jpeg_compat_check_tables(cinfo);
	jpeg_compat_reset_bits(cinfo);
	master->insufficient = 0;
	for (slot = 0; slot < master->scan_count; slot++)
		master->planes[master->scan_components[slot]].prediction = 0;
	master->restarts_left = cinfo->restart_interval;
	master->next_restart = 0;

	/* One component: its blocks row by row, each block an MCU. */
	done = 0;
	if (master->scan_count == 1) {
		index = master->scan_components[0];
		component = &master->components[index];
		across = (component->downsampled_width + DCTSIZE - 1U) / DCTSIZE;
		down = (component->downsampled_height + DCTSIZE - 1U) / DCTSIZE;
		for (row = 0; row < down; row++) {
			for (column = 0; column < across; column++) {
				jpeg_compat_next_mcu(cinfo, done);
				skip = master->insufficient;
				jpeg_compat_decode_block(cinfo, index, column, row, skip);
				done++;
			}
		}

		/* Succeeded: the component is decoded. */
		master->planes[index].decoded = 1;
		return;
	}

	/* Several: each MCU holds each component's h by v blocks. */
	for (row = 0; row < master->mcus_down; row++) {
		for (column = 0; column < master->mcus_across; column++) {
			jpeg_compat_next_mcu(cinfo, done);
			skip = master->insufficient;
			for (slot = 0; slot < master->scan_count; slot++) {
				index = master->scan_components[slot];
				component = &master->components[index];
				for (v = 0; v < component->v_samp_factor; v++) {
					for (h = 0; h < component->h_samp_factor; h++)
						jpeg_compat_decode_block(cinfo, index, column * (JDIMENSION)component->h_samp_factor + (JDIMENSION)h, row * (JDIMENSION)component->v_samp_factor + (JDIMENSION)v, skip);
				}
			}

			/* One more MCU of the scan. */
			done++;
		}
	}

	/* Succeeded: the scan's components are decoded. */
	for (slot = 0; slot < master->scan_count; slot++)
		master->planes[master->scan_components[slot]].decoded = 1;
}

/* Fills the accumulator from the source: stuffed 0xFF 0x00 is a 0xFF byte, and past a marker come zeros. */
static void
jpeg_compat_fill_bits(
	j_decompress_ptr cinfo)
{
	struct jpeg_decomp_master *master;
	int byte;
	int next;

	/* Whole bytes until the accumulator is full enough. */
	master = cinfo->master;
	while (master->bit_count <= JPEG_BITS_FILL) {
		/* A marker was met: the scan's data is over, and zeros follow. */
		if (master->marker != 0) {
			master->bits <<= 8;
			master->bit_count += 8;
			master->pad_bits += 8;
			continue;
		}

		/* A data byte, or 0xFF: stuffed data, fill, or a marker. */
		byte = jpeg_compat_read_byte(cinfo);
		if (byte == 0xFF) {
			do {
				next = jpeg_compat_read_byte(cinfo);
			} while (next == 0xFF);
			if (next != 0) {
				master->marker = next;
				continue;
			}
		}

		/* The byte's bits at the bottom. */
		master->bits = (master->bits << 8) | (uint64_t)byte;
		master->bit_count += 8;
	}
}

/* Uses bits of the accumulator; using the zeros after a marker means the data ran out. */
static void
jpeg_compat_consume(
	j_decompress_ptr cinfo,
	int count)
{
	struct jpeg_decomp_master *master;

	/* The bits go. */
	master = cinfo->master;
	master->bit_count -= count;

	/* Into the zeros: reported once, and the segment's later MCUs are left at zero. */
	if (master->bit_count < master->pad_bits) {
		if (!master->insufficient)
			jpeg_compat_warn((j_common_ptr)cinfo, JWRN_HIT_MARKER);
		master->insufficient = 1;
		master->pad_bits = master->bit_count;
	}
}

/* Empties the accumulator (at the start of a scan and at a restart marker). */
static void
jpeg_compat_reset_bits(
	j_decompress_ptr cinfo)
{
	struct jpeg_decomp_master *master;

	/* No bits, and no zeros. */
	master = cinfo->master;
	master->bits = 0;
	master->bit_count = 0;
	master->pad_bits = 0;
}

/* Decodes one Huffman code; a code in no table is reported and read as 0. */
static int
jpeg_compat_decode(
	j_decompress_ptr cinfo,
	const struct jpeg_huffman *huffman)
{
	struct jpeg_decomp_master *master;
	uint32_t look;
	int32_t code;
	int length;
	unsigned entry;

	/* Enough bits for the longest code. */
	master = cinfo->master;
	if (master->bit_count < 16)
		jpeg_compat_fill_bits(cinfo);

	/* A short code, from the lookup. */
	look = (uint32_t)(master->bits >> (master->bit_count - JPEG_FAST_BITS)) & ((1U << JPEG_FAST_BITS) - 1U);
	entry = huffman->fast[look];
	if (entry != 0) {
		jpeg_compat_consume(cinfo, (int)(entry >> 8));
		return (int)(entry & 0xFFU);
	}

	/* A longer one, length by length. */
	for (length = JPEG_FAST_BITS + 1; length <= 16; length++) {
		code = (int32_t)((master->bits >> (master->bit_count - length)) & ((1U << length) - 1U));
		if (code <= huffman->maxcode[length]) {
			jpeg_compat_consume(cinfo, length);
			return huffman->values[(code + huffman->valoffset[length]) & 0xFF];
		}
	}

	/* No code matches: corrupt data, read as 0 after its sixteen bits (as libjpeg does). */
	jpeg_compat_warn((j_common_ptr)cinfo, JWRN_HUFF_BAD_CODE);
	jpeg_compat_consume(cinfo, 16);
	return 0;
}

/* Reads a value of a size category (T.81 F.2.2.1): size bits, extended to its sign. */
static int
jpeg_compat_receive(
	j_decompress_ptr cinfo,
	int size)
{
	struct jpeg_decomp_master *master;
	int value;

	/* Nothing for size 0; a corrupt size larger than a sample's is cut. */
	if (size == 0)
		return 0;
	if (size > 16)
		size = 16;

	/* The bits. */
	master = cinfo->master;
	if (master->bit_count < size)
		jpeg_compat_fill_bits(cinfo);
	value = (int)((master->bits >> (master->bit_count - size)) & ((1U << size) - 1U));
	jpeg_compat_consume(cinfo, size);

	/* A value with its top bit clear is negative. */
	if (value < (1 << (size - 1)))
		value -= (1 << size) - 1;
	return value;
}

/*
 * Decodes one block of a component into its coefficients and puts its
 * samples at a block position of the plane; skip leaves the block at
 * zero (the data ran out earlier in the segment).
 */
static void
jpeg_compat_decode_block(
	j_decompress_ptr cinfo,
	int index,
	JDIMENSION column,
	JDIMENSION row,
	int skip)
{
	struct jpeg_decomp_master *master;
	jpeg_component_info *component;
	struct jpeg_plane *plane;
	JCOEF coefficients[DCTSIZE2];
	int position;
	int symbol;
	int run;
	int size;
	int value;

	/* The DC coefficient: the difference from the prediction. */
	master = cinfo->master;
	component = &master->components[index];
	plane = &master->planes[index];
	memset(coefficients, 0, sizeof(coefficients));
	if (skip) {
		jpeg_compat_store_block(cinfo, index, column, row, coefficients);
		return;
	}

	/* The DC difference's size, then its value. */
	size = jpeg_compat_decode(cinfo, &master->dc[component->dc_tbl_no]);
	value = jpeg_compat_receive(cinfo, size);
	plane->prediction += value;
	coefficients[0] = (JCOEF)plane->prediction;

	/* The AC coefficients: runs of zeros, each ended by a value, until EOB. */
	for (position = 1; position < DCTSIZE2; position++) {
		symbol = jpeg_compat_decode(cinfo, &master->ac[component->ac_tbl_no]);
		run = symbol >> 4;
		size = symbol & 15;
		if (size != 0) {
			position += run;
			value = jpeg_compat_receive(cinfo, size);
			coefficients[jpeg_compat_natural_order[position]] = (JCOEF)value;
			continue;
		}

		/* ZRL (sixteen zeros) goes on; anything else is EOB. */
		if (run != 15)
			break;
		position += 15;
	}

	/* The samples. */
	jpeg_compat_store_block(cinfo, index, column, row, coefficients);
}

/* Puts a block's samples (its inverse DCT) at a block position of its component's plane. */
static void
jpeg_compat_store_block(
	j_decompress_ptr cinfo,
	int index,
	JDIMENSION column,
	JDIMENSION row,
	const JCOEF *coefficients)
{
	struct jpeg_decomp_master *master;
	jpeg_component_info *component;
	struct jpeg_plane *plane;
	JSAMPLE *out;

	/* A block outside the plane (a corrupt frame) is dropped. */
	master = cinfo->master;
	component = &master->components[index];
	plane = &master->planes[index];
	if ((size_t)(row + 1U) * DCTSIZE > plane->rows || (size_t)(column + 1U) * DCTSIZE > plane->stride)
		return;

	/* The inverse DCT into the plane. */
	out = plane->samples + (size_t)row * DCTSIZE * plane->stride + (size_t)column * DCTSIZE;
	jpeg_compat_idct(coefficients, master->quant[component->quant_tbl_no].quantval, out, plane->stride);
}

/* Refuses a scan whose component names a table the file has not defined. */
static void
jpeg_compat_check_tables(
	j_decompress_ptr cinfo)
{
	struct jpeg_decomp_master *master;
	jpeg_component_info *component;
	int slot;

	/* Each component of the scan: its DC, AC and quantization tables. */
	master = cinfo->master;
	for (slot = 0; slot < master->scan_count; slot++) {
		component = &master->components[master->scan_components[slot]];
		if (!master->dc[component->dc_tbl_no].defined)
			jpeg_compat_fail_number((j_common_ptr)cinfo, JERR_NO_TABLE, component->dc_tbl_no, 0);
		if (!master->ac[component->ac_tbl_no].defined)
			jpeg_compat_fail_number((j_common_ptr)cinfo, JERR_NO_TABLE, 0x10 + component->ac_tbl_no, 0);
		if (!master->quant[component->quant_tbl_no].sent_table)
			jpeg_compat_fail_number((j_common_ptr)cinfo, JERR_NO_TABLE, component->quant_tbl_no, 0);
	}
}

/* Starts an MCU: at the end of a restart interval, the restart marker comes first. */
static void
jpeg_compat_next_mcu(
	j_decompress_ptr cinfo,
	JDIMENSION done)
{
	struct jpeg_decomp_master *master;

	/* No intervals, or the first MCU. */
	master = cinfo->master;
	if (cinfo->restart_interval == 0)
		return;
	if (done != 0 && master->restarts_left == 0)
		jpeg_compat_restart(cinfo);

	/* One MCU of the interval is used. */
	master->restarts_left--;
}

/*
 * Reads a restart marker: the bits left are padding, the marker must be
 * the next RSTn (else the source's resync decides), and the predictions
 * start again.
 */
static void
jpeg_compat_restart(
	j_decompress_ptr cinfo)
{
	struct jpeg_decomp_master *master;
	int marker;
	int slot;

	/* The marker the bits ran into, or the next one in the source. */
	master = cinfo->master;
	jpeg_compat_reset_bits(cinfo);
	marker = master->marker;
	if (marker == 0) {
		marker = jpeg_compat_next_marker(cinfo);
		master->marker = marker;
	}

	/* The expected RSTn is consumed; another RSTn is reported and consumed; any other marker stays. */
	if (marker != JPEG_RST0 + master->next_restart) {
		cinfo->src->resync_to_restart(cinfo, master->next_restart);
	}

	/* A restart marker is used up; another marker waits for the marker reader. */
	if (marker >= JPEG_RST0 && marker <= JPEG_RST0 + 7)
		master->marker = 0;

	/* Data again after a restart marker; still none when another marker is in the way. */
	if (master->marker == 0)
		master->insufficient = 0;

	/* A new interval with new predictions. */
	for (slot = 0; slot < master->scan_count; slot++)
		master->planes[master->scan_components[slot]].prediction = 0;
	master->next_restart = (master->next_restart + 1) & 7;
	master->restarts_left = cinfo->restart_interval;
}
