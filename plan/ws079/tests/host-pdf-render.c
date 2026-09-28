/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of libpdf's page interpretation and rasterizer (ws079-p006).
 *
 *   host-pdf-render notes OUT.pdf TEST.jpg   a three-page Notes-like document through the writer
 *   host-pdf-render ops OUT.pdf              a hand-built document of the stage-1 operators
 *   host-pdf-render render IN.pdf PREFIX DPI each page to PREFIX-N.ppm, one line per page
 *   host-pdf-render compare A.ppm B.ppm      the difference of two pictures, within the tolerance or not
 *   host-pdf-render fuzz IN.pdf ITERATIONS   mutated operator content and mutated files, rendered
 *   host-pdf-render blurcompare A.ppm B.ppm MEAN FRACTION
 *                                            compare, raw and after a 5x5 binomial blur of both, within
 *                                            the given tolerance of the blurred pictures (ws079-p007)
 *   host-pdf-render fuzzdoc IN.pdf ITERATIONS any document: bytes mutated inside its unfiltered streams
 *                                            (the xref stays valid) and anywhere in the file (ws079-p007)
 *
 * run-pdf-render.sh drives it against pdftoppm.  The hand-built document
 * writes its own xref, a Flate content stream and a Flate image with the
 * PNG predictors as stored (uncompressed) deflate blocks, so that the
 * test needs no compressor.
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pdf.h>

/* The size of the JPEG run-pdf-render.sh makes. */
#define TEST_JPEG_WIDTH 64
#define TEST_JPEG_HEIGHT 48

/* The hand-built document's page size and object count. */
#define OPS_PAGE 400
#define OPS_OBJECTS 32

/* The pixel difference counted as a real difference, and the tolerances of compare. */
#define COMPARE_THRESHOLD 64
#define COMPARE_MEAN_MAX 2.5
#define COMPARE_FRACTION_MAX 0.006

/* A growing byte buffer for the hand-built documents. */
struct buffer {
	unsigned char *data;
	size_t size;
	size_t capacity;
};

static int make_notes(const char *path, const char *jpeg_path);
static int draw_writing(struct pdf_writer *writer, double top, double seed, double width, double red, double green, double blue, double alpha);
static int make_ops(const char *path);
static int build_ops(const char *const contents[3], struct buffer *pdf);
static int render(const char *path, const char *prefix, double dpi);
static int render_document(struct pdf_document *document, const char *prefix, double dpi, int quiet);
static int compare(const char *first, const char *second);
static int read_ppm(const char *path, unsigned char **pixels, int *width, int *height);
static int fuzz(const char *path, long iterations);
static int blur_compare(const char *first, const char *second, double mean_max, double fraction_max);
static void measure(const unsigned char *a, int width_a, const unsigned char *b, int width_b, int width, int height, double *mean, double *fraction);
static unsigned char *blur(const unsigned char *pixels, int width, int height);
static int fuzz_document(const char *path, long iterations);
static int read_file(const char *path, unsigned char **data, size_t *size);
static void append(struct buffer *buffer, const void *data, size_t size);
static void append_text(struct buffer *buffer, const char *text);
static void append_stored_deflate(struct buffer *buffer, const unsigned char *data, size_t size);
static unsigned long next_random(unsigned long *state);

/* The operator content of the three pages of the hand-built document. */
static const char *const ops_contents[3] = {
	/* Page 1: fills, rules, colour spaces, curves, alpha and Multiply. */
	"0.9 g 0 0 400 400 re f\n"
	"1 0 0 rg 20 20 100 100 re f\n"
	"0 0 1 RG 0 1 0 rg 4 w 140 20 100 100 re B\n"
	"q 0.5 0 0 0.5 260 20 cm 0 0 0 0 k 0 0 200 200 re f 0.3 0.3 0.3 rg 10 10 180 180 re f Q\n"
	"0.2 0.2 0.6 rg 30 150 m 80 280 l 130 150 l 5 230 l 155 230 l h f*\n"
	"0.6 0.2 0.2 rg 190 150 m 240 280 l 290 150 l 165 230 l 315 230 l h f\n"
	"/CS1 cs 0.1 0.5 0.9 sc 320 150 60 60 re f\n"
	"0 0.5 0.5 rg 20 300 m 20 380 100 380 100 300 c 80 330 40 330 20 300 c f\n"
	"0.5 0 0.5 rg 120 300 m 160 390 200 300 v 170 320 140 320 y h f\n"
	"/GS1 gs 1 0.5 0 rg 230 290 100 100 re f\n"
	"/GS2 gs 0 1 1 rg 290 250 60 130 re f\n"
	"/GS0 gs BT /F1 12 Tf (text is stage 2) Tj ET\n",
	/* Page 2: strokes, caps, joins, dashes, a hairline, a translucent self-crossing stroke. */
	"0 0 0 RG 16 w 0 J 0 j 30 30 m 100 100 l 170 30 l S\n"
	"1 J 1 j 30 150 m 100 220 l 170 150 l S\n"
	"2 J 2 j 30 270 m 100 340 l 170 270 l S\n"
	"0 J 0 j 3 w [12 6] 0 d 220 30 m 380 30 l S\n"
	"[8 4 2 4] 3 d 1 J 220 50 m 380 80 l S\n"
	"[] 0 d 0 J 1 0 0 RG 0.5 w 220 100 m 380 130 l S\n"
	"0 w 220 150 m 380 150 l S\n"
	"q /GS3 gs 0 0 1 RG 10 w 1 j 1 J 220 180 m 300 300 380 180 v 220 250 l S Q\n"
	"q /GS3 gs 0 0.5 0 RG 1 0 0 rg 8 w 0 j 230 320 m 370 390 l 370 320 l 230 390 l h b Q\n"
	"0 0 0 RG 2 w q 1 0 0 0.3 0 280 cm 100 30 m 180 30 l S Q\n",
	/* Page 3: a clip, images with a soft mask and a stencil, a form, rotated and cropped. */
	"q 50 50 m 350 50 l 200 350 l h W n 0 0.6 0 rg 0 0 400 400 re f Q\n"
	"q 64 0 0 64 20 300 cm /Im1 Do Q\n"
	"q 0.2 0.1 0.8 rg 64 0 0 64 110 300 cm /Im2 Do Q\n"
	"q 1 0 0 1 250 250 cm /Fm1 Do Q\n"
	"q 64 0 0 64 300 60 cm /Im3 Do Q\n"
	"q 20 20 m 380 20 l 380 40 l 20 40 l h W* n 1 0 0 rg 0 0 400 400 re f Q\n"
};

/*
 * Runs the command named by the first argument.
 */
int
main(
	int argc,
	char **argv)
{
	int error;

	/* Chooses the command. */
	error = 2;
	if (argc == 4 && strcmp(argv[1], "notes") == 0)
		error = make_notes(argv[2], argv[3]);
	else if (argc == 3 && strcmp(argv[1], "ops") == 0)
		error = make_ops(argv[2]);
	else if (argc == 5 && strcmp(argv[1], "render") == 0)
		error = render(argv[2], argv[3], atof(argv[4]));
	else if (argc == 4 && strcmp(argv[1], "compare") == 0)
		error = compare(argv[2], argv[3]);
	else if (argc == 4 && strcmp(argv[1], "fuzz") == 0)
		error = fuzz(argv[2], atol(argv[3]));
	else if (argc == 6 && strcmp(argv[1], "blurcompare") == 0)
		error = blur_compare(argv[2], argv[3], atof(argv[4]), atof(argv[5]));
	else if (argc == 4 && strcmp(argv[1], "fuzzdoc") == 0)
		error = fuzz_document(argv[2], atol(argv[3]));
	else
		fprintf(stderr, "usage: host-pdf-render notes|ops|render|compare|fuzz|blurcompare|fuzzdoc ...\n");

	/* Reports the outcome. */
	if (error != 0)
		return 1;
	return 0;
}

/*
 * Writes a three-page document as Notes does: pages of pressure strokes
 * (dark ink, coloured ink, a translucent highlighter), a JPEG and a
 * translucent RGBA image.
 */
static int
make_notes(
	const char *path,
	const char *jpeg_path)
{
	struct pdf_writer *writer;
	unsigned char pixels[32 * 32 * 4];
	unsigned char *jpeg;
	FILE *file;
	long jpeg_size;
	size_t x;
	size_t y;
	int line;
	int error;

	/* Reads the JPEG. */
	file = fopen(jpeg_path, "rb");
	if (file == NULL)
		return errno;
	fseek(file, 0, SEEK_END);
	jpeg_size = ftell(file);
	fseek(file, 0, SEEK_SET);
	jpeg = malloc((size_t)jpeg_size);
	if (jpeg == NULL || fread(jpeg, 1, (size_t)jpeg_size, file) != (size_t)jpeg_size) {
		fclose(file);
		free(jpeg);
		return EIO;
	}
	fclose(file);

	/* The RGBA image: a green square, opaque at the top and clear at the bottom. */
	for (y = 0; y < 32; y++) {
		for (x = 0; x < 32; x++) {
			pixels[(y * 32 + x) * 4 + 0] = 20;
			pixels[(y * 32 + x) * 4 + 1] = (unsigned char)(120 + x * 4);
			pixels[(y * 32 + x) * 4 + 2] = 40;
			pixels[(y * 32 + x) * 4 + 3] = (unsigned char)(255 - y * 8);
		}
	}

	/* Page 1: lines of dark handwriting, and a highlighter over one of them. */
	error = pdf_writer_create(&writer);
	if (error != 0) {
		free(jpeg);
		return error;
	}
	error = pdf_writer_begin_page(writer, 595.276, 841.89);
	for (line = 0; line < 12 && error == 0; line++)
		error = draw_writing(writer, 80.0 + line * 55.0, (double)line, 2.2, 0.05, 0.05, 0.1, 1.0);
	if (error == 0)
		error = draw_writing(writer, 190.0, 40.0, 18.0, 1.0, 0.9, 0.0, 0.4);
	if (error == 0)
		error = pdf_writer_end_page(writer);

	/* Page 2: coloured ink and the images. */
	if (error == 0)
		error = pdf_writer_begin_page(writer, 595.276, 841.89);
	for (line = 0; line < 4 && error == 0; line++)
		error = draw_writing(writer, 80.0 + line * 60.0, 20.0 + line, 3.0, 0.8, 0.1 * line, 0.2, 1.0);
	if (error == 0)
		error = pdf_writer_set_fill_color(writer, 0.0, 0.0, 0.0, 1.0);
	if (error == 0)
		error = pdf_writer_draw_jpeg_image(writer, jpeg, (size_t)jpeg_size, TEST_JPEG_WIDTH, TEST_JPEG_HEIGHT, 3, 100.0, 400.0, 256.0, 192.0);
	if (error == 0)
		error = pdf_writer_draw_rgba_image(writer, pixels, 32, 32, 300.0, 420.0, 160.0, 160.0);
	if (error == 0)
		error = pdf_writer_end_page(writer);

	/* Page 3: a dense page of small strokes. */
	if (error == 0)
		error = pdf_writer_begin_page(writer, 595.276, 841.89);
	for (line = 0; line < 60 && error == 0; line++)
		error = draw_writing(writer, 40.0 + line * 13.0, 100.0 + line, 1.2, 0.1, 0.1, 0.4 + (line % 3) * 0.2, 1.0);
	if (error == 0)
		error = pdf_writer_end_page(writer);

	/* Saves the document. */
	if (error == 0)
		error = pdf_writer_attach_file(writer, "zedbsd-notes.bin", "application/x-zedbsd-notes", "ZNOT", 4);
	if (error == 0)
		error = pdf_writer_save(writer, path);
	pdf_writer_destroy(writer);
	free(jpeg);
	if (error != 0)
		fprintf(stderr, "host-pdf-render: notes: error %d\n", error);
	return error;
}

/* Draws one line of handwriting: loops of pressure strokes across the page. */
static int
draw_writing(
	struct pdf_writer *writer,
	double top,
	double seed,
	double width,
	double red,
	double green,
	double blue,
	double alpha)
{
	struct pdf_stroke_point points[160];
	struct pdf_point *outline;
	size_t outline_count;
	size_t index;
	double t;
	int word;
	int error;

	/* Six words of looping strokes. */
	error = pdf_writer_set_fill_color(writer, red, green, blue, alpha);
	for (word = 0; word < 6 && error == 0; word++) {
		for (index = 0; index < 160; index++) {
			t = (double)index / 159.0;
			points[index].x = 50.0 + word * 85.0 + t * 70.0 + 6.0 * sin(t * 25.0 + seed);
			points[index].y = top + 12.0 * sin(t * 12.0 + seed * 0.7 + word) * (0.6 + 0.4 * cos(t * 3.0));
			points[index].pressure = 0.3 + 0.7 * sin(t * 3.14159);
		}
		error = pdf_outline_stroke(points, 160, width, &outline, &outline_count);
		if (error != 0)
			break;
		error = pdf_writer_fill_outline(writer, outline, outline_count);
		pdf_outline_free(outline);
	}
	return error;
}

/* Writes the hand-built operator document. */
static int
make_ops(
	const char *path)
{
	struct buffer pdf;
	FILE *file;
	int error;

	/* Builds the document in memory. */
	memset(&pdf, 0, sizeof(pdf));
	error = build_ops(ops_contents, &pdf);
	if (error != 0)
		return error;

	/* Writes it out. */
	file = fopen(path, "wb");
	if (file == NULL) {
		free(pdf.data);
		return errno;
	}
	fwrite(pdf.data, 1, pdf.size, file);
	fclose(file);
	free(pdf.data);
	return 0;
}

/*
 * Builds the operator document around three page contents: the resources
 * (ExtGStates, a colour space, a soft-masked image, a stencil, a form, a
 * Flate image with PNG predictors), page 1's content as a stored Flate
 * stream, and page 3 rotated and cropped.
 */
static int
build_ops(
	const char *const contents[3],
	struct buffer *pdf)
{
	size_t offsets[OPS_OBJECTS];
	struct buffer image;
	struct buffer mask;
	struct buffer predicted;
	struct buffer deflated;
	unsigned char row[64 * 3];
	char text[512];
	size_t x;
	size_t y;
	size_t index;
	size_t xref;
	int object;

	/* The raw image (64x64 RGB), its soft mask (a radial falloff), and its PNG-predicted form. */
	memset(&image, 0, sizeof(image));
	memset(&mask, 0, sizeof(mask));
	memset(&predicted, 0, sizeof(predicted));
	memset(&deflated, 0, sizeof(deflated));
	for (y = 0; y < 64; y++) {
		for (x = 0; x < 64; x++) {
			row[x * 3 + 0] = (unsigned char)(x * 4);
			row[x * 3 + 1] = (unsigned char)(y * 4);
			row[x * 3 + 2] = (unsigned char)(255 - x * 2);
			text[0] = (char)(255 - (int)(sqrt((double)((x - 32) * (x - 32) + (y - 32) * (y - 32))) * 7.0));
			if ((x - 32) * (x - 32) + (y - 32) * (y - 32) > 36 * 36)
				text[0] = 0;
			append(&mask, text, 1);
		}
		append(&image, row, sizeof(row));
		/* The predicted row: type Up (2) on even rows, Paeth (4) on odd ones, from the raw rows. */
		text[0] = (char)((y % 2 == 0) ? 2 : 4);
		append(&predicted, text, 1);
		for (index = 0; index < sizeof(row); index++) {
			unsigned char above = 0;
			unsigned char left = 0;
			unsigned char upper_left = 0;
			int estimate;
			int pa;
			int pb;
			int pc;
			unsigned char prediction;
			if (y > 0)
				above = image.data[(y - 1) * sizeof(row) + index];
			if (index >= 3)
				left = row[index - 3];
			if (y > 0 && index >= 3)
				upper_left = image.data[(y - 1) * sizeof(row) + index - 3];
			prediction = above;
			if (y % 2 == 1) {
				estimate = left + above - upper_left;
				pa = abs(estimate - left);
				pb = abs(estimate - above);
				pc = abs(estimate - upper_left);
				prediction = upper_left;
				if (pa <= pb && pa <= pc)
					prediction = left;
				else if (pb <= pc)
					prediction = above;
			}
			text[0] = (char)(row[index] - prediction);
			append(&predicted, text, 1);
		}
	}
	append_stored_deflate(&deflated, predicted.data, predicted.size);

	/* The header. */
	memset(offsets, 0, sizeof(offsets));
	append_text(pdf, "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");

	/* 1: the catalog; 2: the page tree (the pages inherit the resources, 3). */
	offsets[1] = pdf->size;
	append_text(pdf, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
	offsets[2] = pdf->size;
	append_text(pdf, "2 0 obj\n<< /Type /Pages /Kids [10 0 R 11 0 R 12 0 R] /Count 3 /MediaBox [0 0 400 400] /Resources 3 0 R >>\nendobj\n");

	/* 3: the resources. */
	offsets[3] = pdf->size;
	append_text(pdf, "3 0 obj\n<< /ExtGState << /GS0 << /ca 1 /CA 1 /BM /Normal >> /GS1 << /ca 0.5 >> /GS2 << /ca 1 /BM /Multiply >> /GS3 << /CA 0.6 /ca 0.6 >> >>"
	    " /ColorSpace << /CS1 [/ICCBased 4 0 R] >>"
	    " /XObject << /Im1 5 0 R /Im2 7 0 R /Fm1 8 0 R /Im3 9 0 R >> >>\nendobj\n");

	/* 4: an ICC profile stream standing for RGB (its bytes are not read). */
	offsets[4] = pdf->size;
	append_text(pdf, "4 0 obj\n<< /N 3 /Length 4 >>\nstream\nICC!\nendstream\nendobj\n");

	/* 5: the raw RGB image with its soft mask (6). */
	offsets[5] = pdf->size;
	sprintf(text, "5 0 obj\n<< /Type /XObject /Subtype /Image /Width 64 /Height 64 /ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask 6 0 R /Length %lu >>\nstream\n", (unsigned long)image.size);
	append_text(pdf, text);
	append(pdf, image.data, image.size);
	append_text(pdf, "\nendstream\nendobj\n");
	offsets[6] = pdf->size;
	sprintf(text, "6 0 obj\n<< /Type /XObject /Subtype /Image /Width 64 /Height 64 /ColorSpace /DeviceGray /BitsPerComponent 8 /Length %lu >>\nstream\n", (unsigned long)mask.size);
	append_text(pdf, text);
	append(pdf, mask.data, mask.size);
	append_text(pdf, "\nendstream\nendobj\n");

	/* 7: a stencil mask of 64x64 one-bit samples, a checkerboard of 8-sample squares. */
	offsets[7] = pdf->size;
	append_text(pdf, "7 0 obj\n<< /Type /XObject /Subtype /Image /Width 64 /Height 64 /ImageMask true /BitsPerComponent 1 /Length 512 >>\nstream\n");
	for (y = 0; y < 64; y++) {
		for (x = 0; x < 8; x++) {
			text[0] = (char)((((y / 8) + x) % 2 == 0) ? 0xff : 0x00);
			append(pdf, text, 1);
		}
	}
	append_text(pdf, "\nendstream\nendobj\n");

	/* 8: a form whose content reaches past its bounding box, which clips it. */
	offsets[8] = pdf->size;
	sprintf(text, "8 0 obj\n<< /Type /XObject /Subtype /Form /BBox [0 0 100 100] /Matrix [1 0 0 1 10 10] /Length %lu >>\nstream\n%s\nendstream\nendobj\n",
	    (unsigned long)strlen("0 0 1 rg 50 50 m 150 50 150 150 50 150 c 0 150 0 50 50 50 c f 1 0 0 RG 3 w 0 0 m 120 120 l S"),
	    "0 0 1 rg 50 50 m 150 50 150 150 50 150 c 0 150 0 50 50 50 c f 1 0 0 RG 3 w 0 0 m 120 120 l S");
	append_text(pdf, text);

	/* 9: the same image as 5, Flate-compressed (stored blocks) with the PNG predictors. */
	offsets[9] = pdf->size;
	sprintf(text, "9 0 obj\n<< /Type /XObject /Subtype /Image /Width 64 /Height 64 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode"
	    " /DecodeParms << /Predictor 15 /Colors 3 /Columns 64 >> /Length %lu >>\nstream\n", (unsigned long)deflated.size);
	append_text(pdf, text);
	append(pdf, deflated.data, deflated.size);
	append_text(pdf, "\nendstream\nendobj\n");

	/* 10 to 12: the pages; 13 to 15: their contents (page 1's Flate-encoded). */
	offsets[10] = pdf->size;
	append_text(pdf, "10 0 obj\n<< /Type /Page /Parent 2 0 R /Contents 13 0 R >>\nendobj\n");
	offsets[11] = pdf->size;
	append_text(pdf, "11 0 obj\n<< /Type /Page /Parent 2 0 R /Contents [14 0 R] >>\nendobj\n");
	offsets[12] = pdf->size;
	append_text(pdf, "12 0 obj\n<< /Type /Page /Parent 2 0 R /Contents 15 0 R /Rotate 90 /CropBox [10 10 390 390] >>\nendobj\n");
	for (object = 13; object <= 15; object++) {
		offsets[object] = pdf->size;
		if (object == 13) {
			free(predicted.data);
			memset(&predicted, 0, sizeof(predicted));
			append_stored_deflate(&predicted, (const unsigned char *)contents[0], strlen(contents[0]));
			sprintf(text, "13 0 obj\n<< /Filter /FlateDecode /Length %lu >>\nstream\n", (unsigned long)predicted.size);
			append_text(pdf, text);
			append(pdf, predicted.data, predicted.size);
			free(predicted.data);
		} else {
			sprintf(text, "%d 0 obj\n<< /Length %lu >>\nstream\n", object, (unsigned long)strlen(contents[object - 13]));
			append_text(pdf, text);
			append_text(pdf, contents[object - 13]);
		}
		append_text(pdf, "\nendstream\nendobj\n");
	}

	/* The cross-reference table and the trailer. */
	xref = pdf->size;
	append_text(pdf, "xref\n0 16\n0000000000 65535 f \n");
	for (object = 1; object <= 15; object++) {
		sprintf(text, "%010lu 00000 n \n", (unsigned long)offsets[object]);
		append_text(pdf, text);
	}
	sprintf(text, "trailer\n<< /Size 16 /Root 1 0 R >>\nstartxref\n%lu\n%%%%EOF\n", (unsigned long)xref);
	append_text(pdf, text);

	/* Frees the parts. */
	free(image.data);
	free(mask.data);
	free(deflated.data);
	if (pdf->data == NULL)
		return ENOMEM;
	return 0;
}

/* Renders each page of a file. */
static int
render(
	const char *path,
	const char *prefix,
	double dpi)
{
	struct pdf_document *document;
	int error;

	/* Opens the document. */
	error = pdf_document_open(path, &document);
	if (error != 0) {
		fprintf(stderr, "host-pdf-render: %s: open error %d\n", path, error);
		return error;
	}

	/* Renders its pages. */
	error = render_document(document, prefix, dpi, 0);
	pdf_document_close(document);
	return error;
}

/*
 * Renders each page on white at a resolution, writing PREFIX-N.ppm unless
 * prefix is NULL.
 */
static int
render_document(
	struct pdf_document *document,
	const char *prefix,
	double dpi,
	int quiet)
{
	struct pdf_display_list *list;
	uint32_t *pixels;
	unsigned char *rgb;
	char name[1024];
	FILE *file;
	size_t pages;
	size_t page;
	size_t width;
	size_t height;
	size_t index;
	double scale;
	int error;

	/* Renders each page. */
	pages = pdf_document_page_count(document);
	scale = dpi / 72.0;
	for (page = 0; page < pages; page++) {
		error = pdf_page_render(document, page, &list);
		if (error != 0) {
			if (!quiet)
				fprintf(stderr, "page %lu: render error %d\n", (unsigned long)(page + 1), error);
			continue;
		}

		/* A white target of the page's size at the resolution. */
		width = (size_t)ceil(list->width * scale - 1e-6);
		height = (size_t)ceil(list->height * scale - 1e-6);
		if (width == 0 || height == 0 || width > 8192 || height > 8192) {
			pdf_display_list_destroy(list);
			continue;
		}
		pixels = malloc(width * height * 4);
		if (pixels == NULL) {
			pdf_display_list_destroy(list);
			return ENOMEM;
		}
		for (index = 0; index < width * height; index++)
			pixels[index] = 0xffffffffU;
		error = pdf_display_list_rasterize(list, pixels, width, width, height, scale, 0.0, 0.0);
		if (!quiet) {
			printf("page %lu size %lux%lu items %lu flags %u raster %d\n", (unsigned long)(page + 1), (unsigned long)width, (unsigned long)height,
			    (unsigned long)list->count, list->flags, error);
		}

		/* Writes the picture. */
		if (prefix != NULL && error == 0) {
			rgb = malloc(width * height * 3);
			if (rgb != NULL) {
				for (index = 0; index < width * height; index++) {
					rgb[index * 3 + 0] = (unsigned char)(pixels[index] >> 16);
					rgb[index * 3 + 1] = (unsigned char)(pixels[index] >> 8);
					rgb[index * 3 + 2] = (unsigned char)pixels[index];
				}
				sprintf(name, "%s-%lu.ppm", prefix, (unsigned long)(page + 1));
				file = fopen(name, "wb");
				if (file != NULL) {
					fprintf(file, "P6\n%lu %lu\n255\n", (unsigned long)width, (unsigned long)height);
					fwrite(rgb, 1, width * height * 3, file);
					fclose(file);
				}
				free(rgb);
			}
		}
		free(pixels);
		pdf_display_list_destroy(list);
	}
	return 0;
}

/*
 * Compares two pictures over the area they share: the mean difference of
 * the channels, and the share of pixels whose largest channel difference
 * passes the threshold.
 */
static int
compare(
	const char *first,
	const char *second)
{
	unsigned char *a;
	unsigned char *b;
	int width_a;
	int height_a;
	int width_b;
	int height_b;
	int width;
	int height;
	int x;
	int y;
	int channel;
	int difference;
	int largest;
	double total;
	double mean;
	double fraction;
	long over;
	int error;

	/* Reads both pictures. */
	error = read_ppm(first, &a, &width_a, &height_a);
	if (error != 0)
		return error;
	error = read_ppm(second, &b, &width_b, &height_b);
	if (error != 0) {
		free(a);
		return error;
	}

	/* Compares the shared area. */
	width = width_a < width_b ? width_a : width_b;
	height = height_a < height_b ? height_a : height_b;
	total = 0.0;
	over = 0;
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			largest = 0;
			for (channel = 0; channel < 3; channel++) {
				difference = abs((int)a[((size_t)y * width_a + x) * 3 + channel] - (int)b[((size_t)y * width_b + x) * 3 + channel]);
				total += difference;
				if (difference > largest)
					largest = difference;
			}
			if (largest > COMPARE_THRESHOLD)
				over++;
		}
	}
	mean = total / ((double)width * height * 3.0);
	fraction = (double)over / ((double)width * height);
	printf("compare %s %s: %dx%d vs %dx%d, mean %.3f, over %d: %ld (%.4f%%) %s\n", first, second, width_a, height_a, width_b, height_b,
	    mean, COMPARE_THRESHOLD, over, fraction * 100.0, (mean <= COMPARE_MEAN_MAX && fraction <= COMPARE_FRACTION_MAX) ? "ok" : "DIFFERENT");
	free(a);
	free(b);

	/* Succeeds within the tolerance. */
	if (mean > COMPARE_MEAN_MAX || fraction > COMPARE_FRACTION_MAX)
		return 1;
	if (abs(width_a - width_b) > 1 || abs(height_a - height_b) > 1)
		return 1;
	return 0;
}

/* Reads a binary PPM (P6, 255). */
static int
read_ppm(
	const char *path,
	unsigned char **pixels,
	int *width,
	int *height)
{
	FILE *file;
	int maximum;
	size_t size;

	/* Reads the header and the samples. */
	file = fopen(path, "rb");
	if (file == NULL)
		return errno;
	if (fscanf(file, "P6 %d %d %d", width, height, &maximum) != 3 || maximum != 255 || *width <= 0 || *height <= 0) {
		fclose(file);
		return EINVAL;
	}
	fgetc(file);
	size = (size_t)*width * (size_t)*height * 3;
	*pixels = malloc(size);
	if (*pixels == NULL || fread(*pixels, 1, size, file) != size) {
		fclose(file);
		free(*pixels);
		return EIO;
	}
	fclose(file);
	return 0;
}

/*
 * Renders mutated documents: the operator document with mutated content
 * (so that the cross-reference table stays valid and the interpreter sees
 * the damage), and a file with bytes mutated anywhere.
 */
static int
fuzz(
	const char *path,
	long iterations)
{
	static const char alphabet[] = " \n0123456789.-+/[]<>()qQcmlhfSWn*BbreEIgGkKrRdjJwMsvyDoT'\"%";
	static const char *const tokens[] = { "q ", "Q ", "cm ", "re ", "f* ", "W n ", "S ", "gs ", "/GS1 ", "/Im1 Do ", "/Fm1 Do ", "BI /W 4 ID xx EI ",
		"1e308 ", "-99999999999 ", "[ ", "] ", "<< ", ">> ", "( ", "99999 w ", "[0 0] 0 d ", "[0.0001] 0 d ", "0 0 0 0 0 0 cm ", "sh ", "c ", "v ", "y " };
	struct pdf_document *document;
	struct buffer pdf;
	unsigned char *original;
	unsigned char *mutated;
	char *contents[3];
	const char *const_contents[3];
	FILE *file;
	unsigned long random;
	size_t size;
	size_t length;
	size_t position;
	long iteration;
	int page;
	int changes;
	int change;
	int opened;
	int error;

	/* Reads the file to mutate. */
	file = fopen(path, "rb");
	if (file == NULL)
		return errno;
	fseek(file, 0, SEEK_END);
	size = (size_t)ftell(file);
	fseek(file, 0, SEEK_SET);
	original = malloc(size);
	mutated = malloc(size);
	if (original == NULL || mutated == NULL || fread(original, 1, size, file) != size) {
		fclose(file);
		return EIO;
	}
	fclose(file);

	/* Mutates the operator content. */
	random = 0x2545F4914F6CDD1DUL;
	opened = 0;
	for (iteration = 0; iteration < iterations; iteration++) {
		for (page = 0; page < 3; page++) {
			length = strlen(ops_contents[page]);
			contents[page] = malloc(length * 2 + 64);
			memcpy(contents[page], ops_contents[page], length + 1);
			changes = 1 + (int)(next_random(&random) % 8);
			for (change = 0; change < changes; change++) {
				length = strlen(contents[page]);
				position = next_random(&random) % (length + 1);
				switch (next_random(&random) % 4) {
				case 0:
					if (position < length)
						contents[page][position] = alphabet[next_random(&random) % (sizeof(alphabet) - 1)];
					break;
				case 1:
					if (length < strlen(ops_contents[page]) * 2) {
						const char *token = tokens[next_random(&random) % (sizeof(tokens) / sizeof(tokens[0]))];
						memmove(contents[page] + position + strlen(token), contents[page] + position, length - position + 1);
						memcpy(contents[page] + position, token, strlen(token));
					}
					break;
				case 2:
					if (position < length)
						memmove(contents[page] + position, contents[page] + position + 1, length - position);
					break;
				default:
					contents[page][position] = '\0';
					break;
				}
			}
			const_contents[page] = contents[page];
		}
		memset(&pdf, 0, sizeof(pdf));
		error = build_ops(const_contents, &pdf);
		for (page = 0; page < 3; page++)
			free(contents[page]);
		if (error != 0)
			return error;
		error = pdf_document_open_memory(pdf.data, pdf.size, &document);
		if (error == 0) {
			opened++;
			render_document(document, NULL, 18.0, 1);
			pdf_document_close(document);
		}
		free(pdf.data);
	}
	printf("fuzz content: %ld iterations, %d opened\n", iterations, opened);

	/* Mutates the file's bytes anywhere, half of the time within the content streams' part. */
	opened = 0;
	for (iteration = 0; iteration < iterations; iteration++) {
		memcpy(mutated, original, size);
		changes = 1 + (int)(next_random(&random) % 6);
		for (change = 0; change < changes; change++) {
			position = next_random(&random) % size;
			if (next_random(&random) % 2 == 0)
				position = size / 4 + next_random(&random) % (size / 2);
			mutated[position] = (unsigned char)alphabet[next_random(&random) % (sizeof(alphabet) - 1)];
		}
		error = pdf_document_open_memory(mutated, size, &document);
		if (error == 0) {
			opened++;
			render_document(document, NULL, 12.0, 1);
			pdf_document_close(document);
		}
	}
	printf("fuzz file: %ld iterations, %d opened\n", iterations, opened);
	free(original);
	free(mutated);
	return 0;
}

/*
 * Compares two pictures raw and after blurring both with a 5x5 binomial
 * kernel, and succeeds when the blurred ones are within the tolerance.
 *
 * poppler aligns glyphs to the pixel grid and libpdf does not, so the
 * edges of text differ by half a pixel; the blur takes that out, while a
 * glyph in the wrong place still differs.
 */
static int
blur_compare(
	const char *first,
	const char *second,
	double mean_max,
	double fraction_max)
{
	unsigned char *a;
	unsigned char *b;
	unsigned char *blurred_a;
	unsigned char *blurred_b;
	int width_a;
	int height_a;
	int width_b;
	int height_b;
	int width;
	int height;
	double raw_mean;
	double raw_fraction;
	double mean;
	double fraction;
	int within;
	int error;

	/* Reads both pictures. */
	error = read_ppm(first, &a, &width_a, &height_a);
	if (error != 0)
		return error;
	error = read_ppm(second, &b, &width_b, &height_b);
	if (error != 0) {
		free(a);
		return error;
	}

	/* Measures the shared area raw, and blurred. */
	width = width_a < width_b ? width_a : width_b;
	height = height_a < height_b ? height_a : height_b;
	measure(a, width_a, b, width_b, width, height, &raw_mean, &raw_fraction);
	blurred_a = blur(a, width_a, height_a);
	blurred_b = blur(b, width_b, height_b);
	if (blurred_a == NULL || blurred_b == NULL) {
		free(a);
		free(b);
		free(blurred_a);
		free(blurred_b);
		return ENOMEM;
	}
	measure(blurred_a, width_a, blurred_b, width_b, width, height, &mean, &fraction);
	free(a);
	free(b);
	free(blurred_a);
	free(blurred_b);

	/* Within the tolerance when the blurred pictures are, and the sizes agree to a pixel. */
	within = 1;
	if (mean > mean_max || fraction > fraction_max)
		within = 0;
	if (abs(width_a - width_b) > 1 || abs(height_a - height_b) > 1)
		within = 0;
	printf("blurcompare %s %s: %dx%d vs %dx%d, raw mean %.3f over %.3f%%, blurred mean %.3f over %.3f%% (max %.2f, %.2f%%) %s\n",
	    first, second, width_a, height_a, width_b, height_b, raw_mean, raw_fraction * 100.0, mean, fraction * 100.0,
	    mean_max, fraction_max * 100.0, within ? "ok" : "DIFFERENT");
	if (!within)
		return 1;
	return 0;
}

/* Measures the mean channel difference and the share of pixels past the threshold. */
static void
measure(
	const unsigned char *a,
	int width_a,
	const unsigned char *b,
	int width_b,
	int width,
	int height,
	double *mean,
	double *fraction)
{
	int x;
	int y;
	int channel;
	int difference;
	int largest;
	double total;
	long over;

	/* Adds up the differences of the shared area. */
	total = 0.0;
	over = 0;
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			largest = 0;
			for (channel = 0; channel < 3; channel++) {
				difference = abs((int)a[((size_t)y * width_a + x) * 3 + channel] - (int)b[((size_t)y * width_b + x) * 3 + channel]);
				total += difference;
				if (difference > largest)
					largest = difference;
			}
			if (largest > COMPARE_THRESHOLD)
				over++;
		}
	}
	*mean = total / ((double)width * height * 3.0);
	*fraction = (double)over / ((double)width * height);
}

/* Blurs a picture with the 5x5 binomial kernel (1 4 6 4 1 each way, a Gaussian of sigma 1), the edges repeated. */
static unsigned char *
blur(
	const unsigned char *pixels,
	int width,
	int height)
{
	static const int weights[5] = { 1, 4, 6, 4, 1 };
	unsigned char *blurred;
	int x;
	int y;
	int dx;
	int dy;
	int sx;
	int sy;
	int channel;
	int sum;

	/* Allocates the result. */
	blurred = malloc((size_t)width * height * 3);
	if (blurred == NULL)
		return NULL;

	/* Each pixel is the weighted mean of its neighbourhood. */
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			for (channel = 0; channel < 3; channel++) {
				sum = 0;
				for (dy = -2; dy <= 2; dy++) {
					for (dx = -2; dx <= 2; dx++) {
						sx = x + dx;
						sy = y + dy;
						if (sx < 0)
							sx = 0;
						if (sx >= width)
							sx = width - 1;
						if (sy < 0)
							sy = 0;
						if (sy >= height)
							sy = height - 1;
						sum += weights[dx + 2] * weights[dy + 2] * pixels[((size_t)sy * width + sx) * 3 + channel];
					}
				}
				blurred[((size_t)y * width + x) * 3 + channel] = (unsigned char)((sum + 128) / 256);
			}
		}
	}
	return blurred;
}

/*
 * Renders mutated copies of any document: bytes changed inside its
 * unfiltered streams only (the lengths and the xref stay valid, so the
 * damage reaches the content interpreter, the fonts and the images), and
 * bytes changed anywhere.
 */
static int
fuzz_document(
	const char *path,
	long iterations)
{
	static const char alphabet[] = " \n0123456789.-+/[]<>()qQcmlhfSWn*BbreEIgGkKrRdjJwMsvyDoT'\"%";
	static const char *const tokens[] = { "q ", "Q ", "cm ", "BT ", "ET ", "Tj ", "TJ ", "Tf ", "Tm ", "Td ", "Tr ", "Tz ", "'",
		"/F1 ", "/F2 99 Tf ", "[(a) -9e9 (b)] TJ ", "<ffff> Tj ", "sh ", "/Sh1 sh ", "BI /W 4 /H 4 /CS /RGB /BPC 8 ID xx EI ",
		"BI /W 99999 /H 99999 /BPC 16 /CS /G ID ", "1e308 ", "-99999999999 ", "[ ", "] ", "<< ", ">> ", "( ", "99999 w ", "W n " };
	struct pdf_document *document;
	unsigned char *original;
	unsigned char *mutated;
	size_t *starts;
	size_t *ends;
	size_t size;
	size_t streams;
	size_t position;
	size_t start;
	size_t end;
	size_t length;
	unsigned long random;
	long iteration;
	int changes;
	int change;
	int opened;
	const char *token;
	int error;

	/* Reads the file to mutate. */
	error = read_file(path, &original, &size);
	if (error != 0)
		return error;
	mutated = malloc(size);
	starts = malloc(sizeof(*starts) * (size / 16 + 1));
	ends = malloc(sizeof(*ends) * (size / 16 + 1));
	if (mutated == NULL || starts == NULL || ends == NULL)
		return ENOMEM;

	/* Finds the streams' data: from after "stream" and its end of line to "endstream". */
	streams = 0;
	for (position = 0; position + 16 < size; position++) {
		if (memcmp(original + position, "stream", 6) != 0)
			continue;
		if (position > 2 && memcmp(original + position - 3, "end", 3) == 0)
			continue;
		start = position + 6;
		if (original[start] == '\r')
			start++;
		if (original[start] == '\n')
			start++;
		for (end = start; end + 9 <= size; end++) {
			if (memcmp(original + end, "endstream", 9) == 0)
				break;
		}
		if (end + 9 > size)
			break;
		if (end > start + 8) {
			starts[streams] = start;
			ends[streams] = end;
			streams++;
		}
		position = end + 9;
	}
	printf("fuzzdoc %s: %lu bytes, %lu streams\n", path, (unsigned long)size, (unsigned long)streams);

	/* Mutates bytes inside the streams, keeping every length. */
	random = 0x2545F4914F6CDD1DUL;
	opened = 0;
	for (iteration = 0; iteration < iterations && streams > 0; iteration++) {
		memcpy(mutated, original, size);
		changes = 1 + (int)(next_random(&random) % 8);
		for (change = 0; change < changes; change++) {
			start = starts[next_random(&random) % streams];
			end = ends[next_random(&random) % streams];
			if (end <= start)
				continue;
			position = start + next_random(&random) % (end - start);
			if (next_random(&random) % 2 == 0) {
				mutated[position] = (unsigned char)alphabet[next_random(&random) % (sizeof(alphabet) - 1)];
				continue;
			}
			token = tokens[next_random(&random) % (sizeof(tokens) / sizeof(tokens[0]))];
			length = strlen(token);
			if (length > end - position)
				length = end - position;
			memcpy(mutated + position, token, length);
		}
		error = pdf_document_open_memory(mutated, size, &document);
		if (error == 0) {
			opened++;
			render_document(document, NULL, 24.0, 1);
			pdf_document_close(document);
		}
	}
	printf("fuzzdoc streams: %ld iterations, %d opened\n", iteration, opened);

	/* Mutates the file's bytes anywhere. */
	opened = 0;
	for (iteration = 0; iteration < iterations; iteration++) {
		memcpy(mutated, original, size);
		changes = 1 + (int)(next_random(&random) % 6);
		for (change = 0; change < changes; change++) {
			position = next_random(&random) % size;
			mutated[position] = (unsigned char)alphabet[next_random(&random) % (sizeof(alphabet) - 1)];
		}
		error = pdf_document_open_memory(mutated, size, &document);
		if (error == 0) {
			opened++;
			render_document(document, NULL, 24.0, 1);
			pdf_document_close(document);
		}
	}
	printf("fuzzdoc file: %ld iterations, %d opened\n", iterations, opened);
	free(original);
	free(mutated);
	free(starts);
	free(ends);
	return 0;
}

/* Reads a whole file into a new buffer. */
static int
read_file(
	const char *path,
	unsigned char **data,
	size_t *size)
{
	FILE *file;
	long length;

	/* Opens the file and measures it. */
	*data = NULL;
	*size = 0;
	file = fopen(path, "rb");
	if (file == NULL)
		return EIO;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (length <= 0) {
		fclose(file);
		return EINVAL;
	}

	/* Reads its bytes. */
	*size = (size_t)length;
	*data = malloc(*size);
	if (*data == NULL || fread(*data, 1, *size, file) != *size) {
		fclose(file);
		free(*data);
		return EIO;
	}
	fclose(file);
	return 0;
}

/* Appends bytes to a buffer, growing it. */
static void
append(
	struct buffer *buffer,
	const void *data,
	size_t size)
{
	unsigned char *grown;
	size_t capacity;

	/* Grows by doubling. */
	if (buffer->size + size > buffer->capacity) {
		capacity = buffer->capacity * 2 + size + 1024;
		grown = realloc(buffer->data, capacity);
		if (grown == NULL) {
			fprintf(stderr, "host-pdf-render: out of memory\n");
			exit(1);
		}
		buffer->data = grown;
		buffer->capacity = capacity;
	}
	memcpy(buffer->data + buffer->size, data, size);
	buffer->size += size;
}

/* Appends a string. */
static void
append_text(
	struct buffer *buffer,
	const char *text)
{
	append(buffer, text, strlen(text));
}

/* Appends bytes as a zlib stream of stored deflate blocks, with its Adler-32. */
static void
append_stored_deflate(
	struct buffer *buffer,
	const unsigned char *data,
	size_t size)
{
	unsigned char header[5];
	unsigned long a;
	unsigned long b;
	size_t done;
	size_t block;
	size_t index;

	/* The zlib header: deflate, 32K window, no dictionary, check bits. */
	header[0] = 0x78;
	header[1] = 0x01;
	append(buffer, header, 2);

	/* Stored blocks of at most 65535 bytes. */
	done = 0;
	do {
		block = size - done;
		if (block > 65535)
			block = 65535;
		header[0] = (unsigned char)((done + block == size) ? 1 : 0);
		header[1] = (unsigned char)(block & 0xff);
		header[2] = (unsigned char)(block >> 8);
		header[3] = (unsigned char)(~block & 0xff);
		header[4] = (unsigned char)((~block >> 8) & 0xff);
		append(buffer, header, 5);
		append(buffer, data + done, block);
		done += block;
	} while (done < size);

	/* The Adler-32 of the data, big-endian. */
	a = 1;
	b = 0;
	for (index = 0; index < size; index++) {
		a = (a + data[index]) % 65521UL;
		b = (b + a) % 65521UL;
	}
	header[0] = (unsigned char)(b >> 8);
	header[1] = (unsigned char)b;
	header[2] = (unsigned char)(a >> 8);
	header[3] = (unsigned char)a;
	append(buffer, header, 4);
}

/* The next value of a xorshift generator. */
static unsigned long
next_random(
	unsigned long *state)
{
	*state ^= *state << 13;
	*state ^= *state >> 7;
	*state ^= *state << 17;
	return *state & 0xffffffffUL;
}
