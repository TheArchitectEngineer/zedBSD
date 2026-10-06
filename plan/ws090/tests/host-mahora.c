/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws090-p020 (q812): the desktop's Mahora fonts and their companions on the
 * host.  libtruetype's companions are checked (the signs found in the
 * monospaced fallback and numbered after the face's own glyphs, Mahora
 * Bold drawn for the bold weight and refused beside Mahora Mono, the
 * companion's units scaled to the face's em, the line left the face's own,
 * missing files left out), and lines of the interface's text are drawn
 * with libkeiland's text into a picture for the user to look at: ASCII,
 * the signs and Latin letters Mahora lacks, Japanese from the fallback, in
 * both weights, and the monospaced text with box drawing.
 *
 *   host-mahora FONTS-DIR PICTURE.ppm
 *
 * FONTS-DIR holds the installed names (keiland.ttf, keiland-bold.ttf,
 * keiland-mono.ttf, keiland-fallback-mono.ttf, keiland-fallback.ttf); the
 * program is built with KEILAND_DATADIR at its parent.  Prints one line per
 * check and "host-mahora: PASS" or "host-mahora: FAIL".
 */

#include <keiland.h>
#include <truetype.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The picture's size. */
#define PICTURE_WIDTH		900
#define PICTURE_HEIGHT		560

/* The longest path built. */
#define PATH_MAX_BYTES		1024U

/* One line drawn: its text, size, weight and whether it is the monospaced font's. */
struct sample {
	const char *text;
	unsigned pixels;
	int bold;
	int mono;
};

static void check(int ok, const char *what);
static int font_read(const char *path, void **data, size_t *size);
static struct truetype_face *face_open(const char *directory, const char *name, void **data, int companions);
static void check_companions(const char *directory);
static void draw_samples(const char *directory, const char *picture);
static void write_ppm(const char *path, const uint32_t *pixels);

/* How many checks failed. */
static int failures;

/* The lines drawn, each with the line's ascent and descent marked. */
static const struct sample samples[] = {
	{ "Settings  Network  Sound  Display  (Mahora Regular 16)", 16U, 0, 0 },
	{ "Settings  Network  Sound  Display  (Mahora Bold 16)", 16U, 1, 0 },
	{ "Signs: \xc2\xb7 \xe2\x86\x92 \xe2\x80\xa6 \xc3\x97 \xc2\xa9 \xe2\x80\x94 \xe2\x80\x98q\xe2\x80\x99 \xe2\x80\x9cq\xe2\x80\x9d \xe2\x80\xa2 \xc2\xb0 \xc2\xb1", 16U, 0, 0 },
	{ "Latin: Caf\xc3\xa9 na\xc3\xafve Stra\xc3\x9f" "e \xe2\x82\xac" "100 \xc3\x85ngstr\xc3\xb6m", 16U, 0, 0 },
	{ "Latin: Caf\xc3\xa9 na\xc3\xafve Stra\xc3\x9f" "e \xe2\x82\xac" "100 (bold)", 16U, 1, 0 },
	{ "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\xe3\x81\xae\xe8\xa1\xa8\xe7\xa4\xba Japanese \xe3\x81\x8b\xe3\x81\xaa \xe3\x82\xab\xe3\x83\x8a", 16U, 0, 0 },
	{ "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\xe3\x81\xae\xe8\xa1\xa8\xe7\xa4\xba Japanese (bold)", 16U, 1, 0 },
	{ "Title p007.txt \xe2\x80\x94 Text Editor   Ln 1, Col 3", 13U, 0, 0 },
	{ "Large heading 24 \xe2\x86\x92 \xe6\x97\xa5\xe6\x9c\xac", 24U, 1, 0 },
	{ "$ ls -l  \xe2\x94\x82 \xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80 box \xe2\x94\x80\xe2\x94\xbc\xe2\x94\x80 \xe2\x96\x88\xe2\x96\x91 (Mahora Mono)", 16U, 0, 1 },
	{ "int main(void) { return 0; } // \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", 16U, 0, 1 },
};

/*
 * Runs the checks and draws the picture.
 */
int
main(
	int argc,
	char **argv)
{
	/* The fonts' directory and the picture. */
	if (argc != 3) {
		fprintf(stderr, "usage: host-mahora FONTS-DIR PICTURE.ppm\n");
		return 2;
	}

	/* The companions, then the picture. */
	check_companions(argv[1]);
	draw_samples(argv[1], argv[2]);

	/* The outcome. */
	if (failures != 0) {
		printf("host-mahora: FAIL (%d)\n", failures);
		return 1;
	}
	printf("host-mahora: PASS\n");
	return 0;
}

/* Reports one check. */
static void
check(
	int ok,
	const char *what)
{
	/* The line, and the count of failures. */
	printf("%s: %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

/* Reads a whole file into memory of its own. */
static int
font_read(
	const char *path,
	void **data,
	size_t *size)
{
	FILE *file;
	long length;
	size_t got;

	/* The file and its length. */
	file = fopen(path, "rb");
	if (file == NULL)
		return errno;
	(void)fseek(file, 0L, SEEK_END);
	length = ftell(file);
	(void)fseek(file, 0L, SEEK_SET);
	if (length <= 0) {
		(void)fclose(file);
		return EINVAL;
	}

	/* Its bytes. */
	*data = malloc((size_t)length);
	if (*data == NULL) {
		(void)fclose(file);
		return ENOMEM;
	}
	got = fread(*data, 1U, (size_t)length, file);
	(void)fclose(file);
	if (got != (size_t)length) {
		free(*data);
		return EIO;
	}

	/* Succeeded. */
	*size = got;
	return 0;
}

/* Opens one installed font of the directory, with the companions (1), without (0), or with missing ones (2). */
static struct truetype_face *
face_open(
	const char *directory,
	const char *name,
	void **data,
	int companions)
{
	struct truetype_face *face;
	char path[PATH_MAX_BYTES];
	char bold[PATH_MAX_BYTES];
	char next[PATH_MAX_BYTES];
	size_t size;
	int error;

	/* The file and its face. */
	size = 0;
	(void)snprintf(path, sizeof(path), "%s/%s", directory, name);
	error = font_read(path, data, &size);
	if (error != 0)
		return NULL;
	error = truetype_open(*data, size, 0U, &face);
	if (error != 0)
		return NULL;

	/* The companions asked for. */
	(void)snprintf(bold, sizeof(bold), "%s/keiland-bold.ttf", directory);
	(void)snprintf(next, sizeof(next), "%s/keiland-fallback-mono.ttf", directory);
	if (companions == 2) {
		(void)snprintf(bold, sizeof(bold), "%s/no-such-bold.ttf", directory);
		(void)snprintf(next, sizeof(next), "%s/no-such-fallback.ttf", directory);
	}
	if (companions != 0) {
		error = truetype_open_companions(face, bold, next);
		check(error == 0, "truetype_open_companions answers 0");
	}

	/* Succeeded. */
	return face;
}

/* Checks libtruetype's companions with the installed fonts. */
static void
check_companions(
	const char *directory)
{
	struct truetype_glyph_outline outline;
	struct truetype_metrics plain_line;
	struct truetype_metrics line;
	struct truetype_glyph regular;
	struct truetype_glyph bold;
	struct truetype_face *plain;
	struct truetype_face *face;
	struct truetype_face *mono;
	struct truetype_face *bare;
	void *plain_data;
	void *data;
	void *mono_data;
	void *bare_data;
	unsigned letter;
	unsigned arrow;
	unsigned kanji;
	int advance;
	int error;

	/* Mahora Regular with and without its companions, Mahora Mono with them, and with files that are not there. */
	face = face_open(directory, "keiland.ttf", &data, 1);
	plain = face_open(directory, "keiland.ttf", &plain_data, 0);
	mono = face_open(directory, "keiland-mono.ttf", &mono_data, 1);
	bare = face_open(directory, "keiland.ttf", &bare_data, 2);
	check(face != NULL && plain != NULL && mono != NULL && bare != NULL, "the four faces open");
	if (face == NULL || plain == NULL || mono == NULL || bare == NULL)
		return;

	/* The glyphs: a letter of Mahora's own, an arrow of the fallback's numbered after them, no kanji (the caller's fallback). */
	letter = truetype_glyph_index(face, 'A');
	arrow = truetype_glyph_index(face, 0x2192U);
	kanji = truetype_glyph_index(face, 0x65e5U);
	check(letter != 0U && letter < 96U, "A is Mahora's own glyph");
	check(arrow >= 96U, "the arrow comes from the companion, numbered after Mahora's 96 glyphs");
	check(kanji == 0U, "the kanji is left to the caller's fallback");
	check(truetype_glyph_index(bare, 0x2192U) == 0U, "without the companion's file the arrow is missing (and nothing else changes)");
	check(truetype_glyph_index(bare, 'A') == letter, "without the companions A is the same glyph");

	/* The bold face: Mahora Bold draws A as it is (as wide, other pixels), not the arrow, and never Mahora Mono. */
	check(truetype_glyph_bold_face(face, letter) == 1, "A is drawn from Mahora Bold");
	check(truetype_glyph_bold_face(face, arrow) == 0, "the arrow is not Mahora Bold's");
	check(truetype_glyph_bold_face(mono, truetype_glyph_index(mono, 'A')) == 0, "Mahora Bold is refused beside Mahora Mono (other widths)");
	(void)truetype_set_pixel_size(face, 32U);
	(void)truetype_set_bold(face, 0);
	error = truetype_glyph_metrics(face, letter, &regular);
	check(error == 0, "A's regular metrics");
	(void)truetype_set_bold(face, 1);
	error = truetype_glyph_metrics(face, letter, &bold);
	check(error == 0, "A's bold metrics");
	check(bold.advance == regular.advance, "bold A advances as regular A (Mahora Bold, not widened)");
	check(bold.width >= regular.width, "bold A is at least as wide as regular A");
	error = truetype_glyph_metrics(face, arrow, &bold);
	check(error == 0 && bold.width != 0U, "the bold arrow is drawn (widened, from the companion)");
	(void)truetype_set_bold(face, 0);

	/* The line stays Mahora's own whatever the companions. */
	(void)truetype_set_pixel_size(plain, 32U);
	(void)truetype_metrics(face, &line);
	(void)truetype_metrics(plain, &plain_line);
	check(line.ascent == plain_line.ascent && line.descent == plain_line.descent && line.line_height == plain_line.line_height, "the line is Mahora's own with the companions");
	printf("line at 32 px: ascent=%d descent=%d height=%d\n", line.ascent, line.descent, line.line_height);

	/* The companion's units in the face's em: JetBrains Mono's 600 of 1000 are 1229 of Mahora Mono's 2048. */
	arrow = truetype_glyph_index(mono, 0x2192U);
	error = truetype_glyph_design_advance(mono, arrow, &advance);
	check(error == 0 && advance == 1229, "the arrow's design advance is in Mahora Mono's em (1229)");
	memset(&outline, 0, sizeof(outline));
	error = truetype_glyph_outline(mono, arrow, &outline);
	check((error == 0 || error == ENOSPC) && outline.advance == 1229, "the arrow's outline is in Mahora Mono's em");
	error = truetype_glyph_design_advance(mono, truetype_glyph_index(mono, 'A'), &advance);
	check(error == 0 && advance == 1229, "Mahora Mono's A advances 1229");

	/* The faces close with their companions. */
	truetype_close(face);
	truetype_close(plain);
	truetype_close(mono);
	truetype_close(bare);
	free(data);
	free(plain_data);
	free(mono_data);
	free(bare_data);
}

/* Draws the samples with libkeiland's text (the applications' own), each line's ascent and descent marked. */
static void
draw_samples(
	const char *directory,
	const char *picture)
{
	struct kl_text_line line;
	struct kl_canvas canvas;
	struct kl_text ui;
	struct kl_text mono;
	struct kl_text *text;
	struct kl_rect rect;
	char regular[PATH_MAX_BYTES];
	char monospaced[PATH_MAX_BYTES];
	char fallback[PATH_MAX_BYTES];
	uint32_t *pixels;
	size_t index;
	int baseline;
	int drawn;
	int top;
	int error;

	/* The interface's font and the monospaced one, each with the CJK fallback. */
	(void)snprintf(regular, sizeof(regular), "%s/keiland.ttf", directory);
	(void)snprintf(monospaced, sizeof(monospaced), "%s/keiland-mono.ttf", directory);
	(void)snprintf(fallback, sizeof(fallback), "%s/keiland-fallback.ttf", directory);
	error = kl_text_open(&ui, regular, fallback);
	check(error == 0, "libkeiland's text opens Mahora");
	if (error != 0)
		return;
	error = kl_text_open(&mono, monospaced, fallback);
	check(error == 0, "libkeiland's text opens Mahora Mono");
	if (error != 0)
		return;

	/* A white picture. */
	pixels = malloc((size_t)PICTURE_WIDTH * PICTURE_HEIGHT * sizeof(*pixels));
	if (pixels == NULL)
		return;
	error = kl_canvas_init(&canvas, pixels, PICTURE_WIDTH, PICTURE_WIDTH, PICTURE_HEIGHT);
	check(error == 0, "the canvas");
	rect.x = 0;
	rect.y = 0;
	rect.width = PICTURE_WIDTH;
	rect.height = PICTURE_HEIGHT;
	kl_canvas_fill(&canvas, &rect, 0xffffffffU);

	/* Each sample, its line's box tinted and its baseline drawn. */
	top = 12;
	for (index = 0; index < sizeof(samples) / sizeof(samples[0]); index++) {
		text = &ui;
		if (samples[index].mono)
			text = &mono;
		kl_text_metrics(text, samples[index].pixels, &line);
		baseline = top + line.ascent;
		rect.x = 12;
		rect.y = top;
		rect.width = PICTURE_WIDTH - 24;
		rect.height = line.height;
		kl_canvas_fill(&canvas, &rect, 0xffe8f0ffU);
		rect.y = baseline;
		rect.height = 1;
		kl_canvas_fill(&canvas, &rect, 0xffb0c4e8U);
		drawn = kl_text_draw(text, &canvas, 16, baseline, samples[index].text, strlen(samples[index].text), samples[index].pixels, samples[index].bold, 0xff1d2230U);
		check(drawn > 0, samples[index].text);
		top += line.height + 14;
	}

	/* The picture. */
	write_ppm(picture, pixels);
	kl_canvas_release(&canvas);
	free(pixels);
	kl_text_close(&ui);
	kl_text_close(&mono);
}

/* Writes the picture as a PPM file. */
static void
write_ppm(
	const char *path,
	const uint32_t *pixels)
{
	unsigned char rgb[3];
	FILE *file;
	size_t index;

	/* The file and its header. */
	file = fopen(path, "wb");
	if (file == NULL) {
		check(0, "the picture opens");
		return;
	}
	fprintf(file, "P6\n%d %d\n255\n", PICTURE_WIDTH, PICTURE_HEIGHT);

	/* Each pixel. */
	for (index = 0; index < (size_t)PICTURE_WIDTH * PICTURE_HEIGHT; index++) {
		rgb[0] = (unsigned char)((pixels[index] >> 16) & 0xffU);
		rgb[1] = (unsigned char)((pixels[index] >> 8) & 0xffU);
		rgb[2] = (unsigned char)(pixels[index] & 0xffU);
		(void)fwrite(rgb, 1U, sizeof(rgb), file);
	}

	/* Done. */
	(void)fclose(file);
}
