/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The font's own measures, in its design units: the size of its em, its
 * ascent, descent and line gap, and each glyph's advance.
 *
 * A caller that lays text out at fractional sizes (the Web browser) scales
 * these itself instead of taking the whole-pixel measures of
 * truetype_metrics and truetype_glyph_metrics.
 */

#include "internal.h"

#include <errno.h>

/*
 * Reports the face's design measures: units per em, ascent, descent (a
 * negative number) and line gap.
 */
int
truetype_design_metrics(
	const struct truetype_face *face,
	struct truetype_design_metrics *metrics)
{
	/* Refuses missing arguments. */
	if (face == NULL || metrics == NULL)
		return EINVAL;

	/* Copies the head and hhea values the face read when it opened. */
	metrics->units_per_em = face->units_per_em;
	metrics->ascent = face->ascent;
	metrics->descent = face->descent;
	metrics->line_gap = face->line_gap;

	/* Succeeded: the measures are filled. */
	return 0;
}

/*
 * Reports a glyph's advance width in design units.
 */
int
truetype_glyph_design_advance(
	const struct truetype_face *face,
	unsigned glyph,
	int *advance)
{
	unsigned index;

	/* Refuses missing arguments and a font without advance widths. */
	if (face == NULL || advance == NULL)
		return EINVAL;
	if (face->hmetric_count == 0)
		return EINVAL;

	/* Glyphs past the long metrics share the last advance. */
	index = glyph;
	if (index >= face->hmetric_count)
		index = (unsigned)(face->hmetric_count - 1U);

	/* The entry must be inside the table. */
	if ((uint32_t)index * 4U + 2U > face->hmtx_size)
		return EINVAL;

	/* Succeeded: the advance is the entry's first field. */
	*advance = (int)truetype_u16(face->hmtx + (size_t)index * 4U);
	return 0;
}
