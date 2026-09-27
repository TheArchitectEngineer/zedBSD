/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Inline layout: a block's inline content (its text boxes, through the
 * inline boxes that hold them) cut into pieces at the break opportunities,
 * whitespace collapsed as white-space asks, and packed greedily into line
 * boxes that are aligned and stacked.
 *
 * The first pass sets every piece on the baseline; inline boxes contribute
 * their style (through the text they hold) but no borders or padding yet.
 */

#include "layout/layout.h"

#include <errno.h>
#include <string.h>

/* How many spaces a tab stands for in preserved whitespace. */
#define INLINE_TAB_SPACES	8

/*
 * One piece of inline content: a word (or a run that cannot break), a
 * collapsed space, or a forced line break.  A collapsed space is drawn as
 * one space however much whitespace it stands for.
 */
struct inline_piece {
	struct layout_box *box;
	const uint16_t *text;
	size_t length;
	layout_unit width;
	int space;
	int forced_break;
	int uses_fallback;
	struct text_font font;
};

/*
 * The state of cutting a block's content into pieces: the pieces so far,
 * the word being gathered and whether the last character was collapsed
 * whitespace.
 */
struct inline_cutter {
	struct layout_tree *tree;
	struct wb_vector pieces;
	struct layout_box *box;
	const uint16_t *word;
	size_t word_length;
	layout_unit word_width;
	int word_fallback;
	uint32_t previous;
	int after_space;
	struct text_font font;
	int error;
};

/* The one space a collapsed run of whitespace is drawn as. */
static const uint16_t inline_space[1] = { 0x20U };

static void inline_collect(struct inline_cutter *cutter, struct layout_box *box);
static void inline_cut_text(struct inline_cutter *cutter, struct layout_box *box);
static void inline_flush_word(struct inline_cutter *cutter);
static void inline_add_piece(struct inline_cutter *cutter, const uint16_t *text, size_t length, layout_unit width, int space, int forced_break);
static layout_unit inline_advance(struct inline_cutter *cutter, uint32_t code_point);
static int inline_build_lines(struct layout_tree *tree, struct layout_box *box, const struct inline_piece *pieces, size_t count);
static int inline_finish_line(struct layout_tree *tree, struct layout_box *box, const struct inline_piece *pieces, size_t start, size_t end, layout_unit *cursor, struct wb_vector *lines, int first_line);
static void inline_line_height(struct layout_tree *tree, const struct css_style *style, layout_unit *above, layout_unit *below);
static void inline_font_extent(struct layout_tree *tree, const struct text_font *font, const struct css_style *style, layout_unit *above, layout_unit *below);
static void inline_piece_extent(struct layout_tree *tree, const struct inline_piece *piece, layout_unit *above, layout_unit *below);

/*
 * Lays out a block's inline content into lines; the block's content
 * height becomes the lines' total height.
 */
int
layout_inline(
	struct layout_tree *tree,
	struct layout_box *box)
{
	struct inline_cutter cutter;
	struct layout_box *child;
	int error;

	/* Cuts the content into pieces. */
	memset(&cutter, 0, sizeof(cutter));
	cutter.tree = tree;
	cutter.after_space = 1;
	wb_vector_init(&cutter.pieces, sizeof(struct inline_piece));
	for (child = box->first_child; child != NULL; child = child->next)
		inline_collect(&cutter, child);
	inline_flush_word(&cutter);
	if (cutter.error != 0) {
		wb_vector_release(&cutter.pieces);
		return cutter.error;
	}

	/* Packs the pieces into lines. */
	error = inline_build_lines(tree, box, cutter.pieces.items, cutter.pieces.count);
	wb_vector_release(&cutter.pieces);
	if (error != 0)
		return error;

	/* Succeeded: the block has its lines and its height. */
	return 0;
}

/* Cuts an inline-level box's content: text, a line break, or an inline box's children. */
static void
inline_collect(
	struct inline_cutter *cutter,
	struct layout_box *box)
{
	struct layout_box *child;

	/* A box out of the flow is no part of the lines. */
	if (box->out_of_flow)
		return;

	/* Text is cut into words and spaces. */
	if (box->kind == LAYOUT_TEXT) {
		inline_cut_text(cutter, box);
		return;
	}

	/* A line break ends the line. */
	if (box->kind == LAYOUT_LINE_BREAK) {
		inline_flush_word(cutter);
		cutter->box = box;
		inline_add_piece(cutter, NULL, 0, 0, 0, 1);
		cutter->after_space = 1;
		return;
	}

	/* An inline box's children, in order. */
	for (child = box->first_child; child != NULL; child = child->next)
		inline_collect(cutter, child);
}

/* Cuts a text box's text into words, collapsed spaces and forced breaks. */
static void
inline_cut_text(
	struct inline_cutter *cutter,
	struct layout_box *box)
{
	struct text_font font;
	uint32_t code_point;
	size_t offset;
	size_t used;
	int font_changed;
	int preserve;
	int wrap_lines;
	int space;
	int opportunity;
	int tab;

	/* The font and the white-space behaviour of the text. */
	layout_font_of(cutter->tree, &box->style, &font);
	preserve = 0;
	if (box->style.white_space == CSS_WHITE_SPACE_PRE || box->style.white_space == CSS_WHITE_SPACE_PRE_WRAP)
		preserve = 1;
	wrap_lines = 1;
	if (box->style.white_space == CSS_WHITE_SPACE_PRE || box->style.white_space == CSS_WHITE_SPACE_NOWRAP)
		wrap_lines = 0;

	/* A new box or font ends the word before it (pieces have one font). */
	font_changed = memcmp(&font, &cutter->font, sizeof(font));
	if (cutter->word != NULL) {
		if (cutter->box != box) {
			/* The word so far belongs to the box before. */
			inline_flush_word(cutter);
		} else if (font_changed != 0) {
			/* The word so far is in another font. */
			inline_flush_word(cutter);
		}
	}

	/* The characters that follow belong to this box and font. */
	cutter->box = box;
	cutter->font = font;

	/* Walks the code points. */
	offset = 0;
	while (offset < box->text_length && cutter->error == 0) {
		used = wb_utf16_decode(box->text + offset, box->text_length - offset, &code_point);
		space = text_is_space(code_point);

		/* A line feed is a forced break where line breaks are kept. */
		if (code_point == 0x0aU && (preserve || box->style.white_space == CSS_WHITE_SPACE_PRE_LINE)) {
			inline_flush_word(cutter);
			inline_add_piece(cutter, NULL, 0, 0, 0, 1);
			cutter->after_space = 1;
			cutter->previous = 0;
			offset += used;
			continue;
		}

		/* Collapsible whitespace becomes one space, and none after another. */
		if (space && !preserve) {
			inline_flush_word(cutter);
			if (!cutter->after_space)
				inline_add_piece(cutter, inline_space, 1, inline_advance(cutter, 0x20U), 1, 0);
			cutter->after_space = 1;
			cutter->previous = 0x20U;
			offset += used;
			continue;
		}

		/* A break opportunity before this character ends the word (only where lines wrap). */
		opportunity = text_break_between(cutter->previous, code_point);
		if (cutter->word != NULL && opportunity && wrap_lines)
			inline_flush_word(cutter);

		/* The character joins the word; a preserved tab is as wide as several spaces. */
		if (cutter->word == NULL) {
			cutter->word = box->text + offset;
			cutter->word_length = 0;
			cutter->word_width = 0;
			cutter->word_fallback = 0;
		}

		/* The character's advance widens the word. */
		cutter->word_length += used;
		tab = 0;
		if (code_point == 0x09U)
			tab = 1;
		if (tab) {
			cutter->word_width += inline_advance(cutter, 0x20U) * INLINE_TAB_SPACES;
		} else {
			cutter->word_width += inline_advance(cutter, code_point);
		}

		/* The next character follows a character that is not collapsed whitespace. */
		cutter->after_space = 0;
		cutter->previous = code_point;
		offset += used;
	}
}

/* Ends the word being gathered and adds it as a piece. */
static void
inline_flush_word(
	struct inline_cutter *cutter)
{
	/* Nothing gathered is nothing to add. */
	if (cutter->word == NULL)
		return;

	/* Adds the word. */
	inline_add_piece(cutter, cutter->word, cutter->word_length, cutter->word_width, 0, 0);
	cutter->word = NULL;
	cutter->word_length = 0;
	cutter->word_width = 0;
	cutter->word_fallback = 0;
}

/* Adds a piece of the current box and font. */
static void
inline_add_piece(
	struct inline_cutter *cutter,
	const uint16_t *text,
	size_t length,
	layout_unit width,
	int space,
	int forced_break)
{
	struct inline_piece piece;
	int error;

	/* Fills the piece. */
	memset(&piece, 0, sizeof(piece));
	piece.box = cutter->box;
	piece.text = text;
	piece.length = length;
	piece.width = width;
	piece.space = space;
	piece.forced_break = forced_break;
	piece.font = cutter->font;

	/* A word drawn partly in the fallback face remembers it for the line's height. */
	if (text != NULL && !space)
		piece.uses_fallback = cutter->word_fallback;

	/* Appends it. */
	error = wb_vector_push(&cutter->pieces, &piece);
	if (error != 0)
		cutter->error = error;
}

/* Measures a code point's advance in the current font, in layout units. */
static layout_unit
inline_advance(
	struct inline_cutter *cutter,
	uint32_t code_point)
{
	struct text_glyph glyph;
	int error;

	/* Asks the text system for the glyph. */
	error = text_glyph(cutter->tree->text, &cutter->font, code_point, 0, &glyph);
	if (error != 0) {
		cutter->error = error;
		return 0;
	}

	/* A glyph the font's own face lacks came from the fallback face. */
	if (glyph.face != cutter->font.face)
		cutter->word_fallback = 1;

	/* Reports the advance in layout units. */
	return (layout_unit)glyph.advance_units;
}

/* Packs pieces into lines greedily and stores the lines in the block. */
static int
inline_build_lines(
	struct layout_tree *tree,
	struct layout_box *box,
	const struct inline_piece *pieces,
	size_t count)
{
	struct wb_vector lines;
	layout_unit cursor;
	layout_unit x;
	size_t start;
	size_t index;
	int wrap_lines;
	int error;

	/* Lines wrap unless white-space forbids it. */
	wrap_lines = 1;
	if (box->style.white_space == CSS_WHITE_SPACE_PRE || box->style.white_space == CSS_WHITE_SPACE_NOWRAP)
		wrap_lines = 0;

	/* Walks the pieces, ending a line at a forced break or when the next word does not fit. */
	wb_vector_init(&lines, sizeof(struct layout_line));
	cursor = 0;
	start = 0;
	x = 0;
	index = 0;
	while (index < count) {
		/* Spaces at the start of a line are dropped. */
		if (pieces[index].space && index == start) {
			start++;
			index++;
			continue;
		}

		/* A forced break ends the line with what came before it. */
		if (pieces[index].forced_break) {
			error = inline_finish_line(tree, box, pieces, start, index, &cursor, &lines, lines.count == 0);
			if (error != 0) {
				wb_vector_release(&lines);
				return error;
			}

			/* The next line starts after the break. */
			index++;
			start = index;
			x = 0;
			continue;
		}

		/* A word that overflows a line that has something ends the line before it. */
		if (wrap_lines && !pieces[index].space && x + pieces[index].width > box->width && index > start) {
			error = inline_finish_line(tree, box, pieces, start, index, &cursor, &lines, lines.count == 0);
			if (error != 0) {
				wb_vector_release(&lines);
				return error;
			}

			/* The next line starts with the word. */
			start = index;
			x = 0;
			continue;
		}

		/* The piece goes on the line. */
		x += pieces[index].width;
		index++;
	}

	/* The last line. */
	if (start < count) {
		error = inline_finish_line(tree, box, pieces, start, count, &cursor, &lines, lines.count == 0);
		if (error != 0) {
			wb_vector_release(&lines);
			return error;
		}
	}

	/* Moves the lines into the arena. */
	box->line_count = lines.count;
	box->lines = NULL;
	if (lines.count != 0) {
		box->lines = wb_arena_alloc(&tree->arena, lines.count * sizeof(struct layout_line));
		if (box->lines == NULL) {
			wb_vector_release(&lines);
			return ENOMEM;
		}

		/* Copies the lines out of the growing vector. */
		memcpy(box->lines, lines.items, lines.count * sizeof(struct layout_line));
	}

	/* The lines now live in the arena. */
	wb_vector_release(&lines);

	/* The block is as tall as its lines. */
	box->height = cursor;

	/* Succeeded: the lines are stored. */
	return 0;
}

/* Makes one line of pieces [start, end): its fragments, height, baseline and alignment. */
static int
inline_finish_line(
	struct layout_tree *tree,
	struct layout_box *box,
	const struct inline_piece *pieces,
	size_t start,
	size_t end,
	layout_unit *cursor,
	struct wb_vector *lines,
	int first_line)
{
	struct layout_line line;
	struct layout_fragment *fragment;
	struct text_metrics metrics;
	struct text_font font;
	struct text_glyph glyph;
	layout_unit above;
	layout_unit below;
	layout_unit piece_above;
	layout_unit piece_below;
	layout_unit x;
	layout_unit room;
	size_t count;
	size_t index;
	int error;

	/* Trailing spaces hang past the line and are dropped. */
	while (end > start && pieces[end - 1U].space)
		end--;

	/* The strut: the block's own font and line height. */
	inline_line_height(tree, &box->style, &above, &below);

	/* Counts the fragments: every piece but the forced breaks. */
	memset(&line, 0, sizeof(line));
	count = 0;
	for (index = start; index < end; index++) {
		if (!pieces[index].forced_break)
			count++;
	}

	/* A list item's first line holds its marker too. */
	if (first_line)
		count++;

	/* Allocates the fragments of a line that has any. */
	if (count != 0) {
		line.fragments = wb_arena_zalloc(&tree->arena, count * sizeof(struct layout_fragment));
		if (line.fragments == NULL)
			return ENOMEM;
	}

	/* Sets one fragment per piece along the line; each raises the line to fit it. */
	x = 0;
	for (index = start; index < end; index++) {
		/* A forced break has no fragment. */
		if (pieces[index].forced_break)
			continue;

		/* The fragment's text, font and place. */
		fragment = &line.fragments[line.fragment_count];
		line.fragment_count++;
		fragment->box = pieces[index].box;
		fragment->text = pieces[index].text;
		fragment->length = pieces[index].length;
		fragment->font = pieces[index].font;
		fragment->color = pieces[index].box->style.color;
		fragment->underline = pieces[index].box->style.underline;
		fragment->x = x;
		fragment->width = pieces[index].width;

		/* The font's ascent and descent, which the painting places the glyphs by. */
		error = text_font_metrics(tree->text, &pieces[index].font, &metrics);
		if (error != 0)
			return error;
		fragment->ascent = (layout_unit)metrics.ascent * LAYOUT_UNIT;
		fragment->descent = (layout_unit)metrics.descent * LAYOUT_UNIT;

		/* The piece's reach above and below the baseline raises the line. */
		inline_piece_extent(tree, &pieces[index], &piece_above, &piece_below);
		if (piece_above > above)
			above = piece_above;
		if (piece_below > below)
			below = piece_below;

		/* The next piece starts where this one ends. */
		x += pieces[index].width;
	}

	/* A list item's marker hangs to the left of its first line. */
	if (first_line && box->marker_length != 0) {
		fragment = &line.fragments[line.fragment_count];
		line.fragment_count++;
		layout_font_of(tree, &box->style, &font);
		fragment->box = box;
		fragment->text = box->marker;
		fragment->length = box->marker_length;
		fragment->font = font;
		fragment->color = box->style.color;
		fragment->width = 0;

		/* The marker is as wide as its characters. */
		for (index = 0; index < box->marker_length; index++) {
			error = text_glyph(tree->text, &font, box->marker[index], 0, &glyph);
			if (error != 0)
				return error;
			fragment->width += glyph.advance_units;
		}

		/* It ends a space's width before the content's left edge. */
		error = text_glyph(tree->text, &font, 0x20U, 0, &glyph);
		if (error != 0)
			return error;
		fragment->x = -(fragment->width + glyph.advance_units);

		/* The font's ascent and descent, which the painting places the glyphs by. */
		error = text_font_metrics(tree->text, &font, &metrics);
		if (error != 0)
			return error;
		fragment->ascent = (layout_unit)metrics.ascent * LAYOUT_UNIT;
		fragment->descent = (layout_unit)metrics.descent * LAYOUT_UNIT;
	}

	/* Aligns the line in the block. */
	room = box->width - x;
	line.left = 0;
	if (room > 0 && box->style.text_align == CSS_TEXT_ALIGN_CENTER)
		line.left = room / 2;
	if (room > 0 && box->style.text_align == CSS_TEXT_ALIGN_RIGHT)
		line.left = room;

	/* Stacks the line under the ones before. */
	line.y = *cursor;
	line.height = above + below;
	line.baseline = above;
	*cursor += line.height;

	/* Adds it. */
	error = wb_vector_push(lines, &line);
	if (error != 0)
		return ENOMEM;

	/* Succeeded: the line is made. */
	return 0;
}

/* Measures how far a style's line reaches above and below the baseline, half-leading included. */
static void
inline_line_height(
	struct layout_tree *tree,
	const struct css_style *style,
	layout_unit *above,
	layout_unit *below)
{
	struct text_font font;

	/* The style's own font under the style's line-height. */
	layout_font_of(tree, style, &font);
	inline_font_extent(tree, &font, style, above, below);
}

/*
 * Measures how far a piece's line reaches above and below the baseline.
 *
 * With line-height: normal, a word drawn partly in the fallback face also
 * reaches as far as that face's own line does, as in Chromium, where every
 * font a run uses counts toward a normal line's height.
 */
static void
inline_piece_extent(
	struct layout_tree *tree,
	const struct inline_piece *piece,
	layout_unit *above,
	layout_unit *below)
{
	struct text_font fallback;
	layout_unit fallback_above;
	layout_unit fallback_below;

	/* The line of the piece's own style. */
	inline_line_height(tree, &piece->box->style, above, below);

	/* A given line height is not raised by the fonts used. */
	if (piece->box->style.line_height.unit != CSS_UNIT_NORMAL)
		return;

	/* A piece in its own face alone needs nothing more. */
	if (!piece->uses_fallback)
		return;

	/* The fallback face at the piece's size raises the extent where it reaches further. */
	fallback = piece->font;
	fallback.face = TEXT_FACE_FALLBACK;
	inline_font_extent(tree, &fallback, &piece->box->style, &fallback_above, &fallback_below);
	if (fallback_above > *above)
		*above = fallback_above;
	if (fallback_below > *below)
		*below = fallback_below;
}

/* Measures how far a font's line reaches above and below the baseline under a style's line-height. */
static void
inline_font_extent(
	struct layout_tree *tree,
	const struct text_font *font,
	const struct css_style *style,
	layout_unit *above,
	layout_unit *below)
{
	struct text_metrics metrics;
	layout_unit content;
	layout_unit height;
	layout_unit leading;
	int error;

	/* The font's measures, or the font size when the font cannot be measured. */
	error = text_font_metrics(tree->text, font, &metrics);
	if (error != 0) {
		metrics.ascent = (int)style->font_size;
		metrics.descent = 0;
		metrics.line_height = (int)style->font_size;
	}

	/* The glyphs' own height, from the ascent to the descent. */
	content = (layout_unit)(metrics.ascent + metrics.descent) * LAYOUT_UNIT;

	/* The used line height: normal, a multiple of the font size, or a length. */
	height = (layout_unit)metrics.line_height * LAYOUT_UNIT;
	if (style->line_height.unit == CSS_UNIT_NUMBER)
		height = layout_from_px(style->line_height.value * style->font_size);
	if (style->line_height.unit == CSS_UNIT_PX)
		height = layout_from_px(style->line_height.value);

	/* Half the leading goes above, half below. */
	leading = height - content;
	*above = (layout_unit)metrics.ascent * LAYOUT_UNIT + leading / 2;
	*below = height - *above;
}
