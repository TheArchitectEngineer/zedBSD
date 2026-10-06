/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws079-p014: host test of libpdf's update, the revision a writer adds to
 * another program's PDF.
 *
 *   host-pdf-update base OUT             writes the hand-made "foreign" PDF (a nested page tree,
 *                                        inherited resources, a rotated and cropped page with two
 *                                        content streams that leave the graphics state changed, a
 *                                        page without content, an attached file in a name tree
 *                                        with kids, an information dictionary, an identifier)
 *   host-pdf-update update IN OUT        adds a revision to IN: page 1 drawn over (a translucent
 *                                        pen stroke and a blue box), a page added after it, page 2
 *                                        (rotated) drawn over, page 3 (no content) drawn over, a page
 *                                        added at the end, the attached edit data; then reads OUT
 *                                        back and checks it (prefix, pages, boxes, attachments,
 *                                        revision link, hashes of the added pages, dates)
 *   host-pdf-update keep IN OUT          adds a revision that keeps every page and replaces the
 *                                        attached edit data (a second revision on an updated file)
 *   host-pdf-update refusals DIR         the refusals: a signed document, an encrypted one, pages
 *                                        listed out of order or not all, a name the page already
 *                                        uses, a page written into its parent's kids
 *
 * Each command prints what it checked and exits 0 when every check passed.
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pdf.h>

/* The edit data the updates attach, and its name and media type (Notes' own). */
#define UPDATE_NAME "kei-notes.bin"
#define UPDATE_TYPE "application/x-kei-notes"

/*
 * A growable run of bytes the test builds files in.
 */
struct buffer {
	unsigned char *data;
	size_t size;
	size_t capacity;
};

/*
 * The variants of the hand-made document: plain, signed, encrypted, with
 * a name the update uses, with a page written into its parent's kids.
 */
enum variant {
	VARIANT_PLAIN = 0,
	VARIANT_SIGNED,
	VARIANT_ENCRYPTED,
	VARIANT_TAKEN_NAME,
	VARIANT_DIRECT_PAGE
};

/* The number of checks that failed. */
static int failures;

static int make_base(const char *path, enum variant variant);
static int update(const char *in, const char *out);
static int keep(const char *in, const char *out);
static int refusals(const char *folder);
static int refuse_encrypted(const char *path);
static int draw_pen(struct pdf_writer *writer, double x, double y, double red, double green, double blue, double alpha);
static int read_file(const char *path, unsigned char **data, size_t *size);
static void check(int condition, const char *what);
static void append(struct buffer *buffer, const void *data, size_t size);
static void append_text(struct buffer *buffer, const char *text);
static int write_file(const char *path, const struct buffer *buffer);

int
main(
	int argc,
	char **argv)
{
	int error;

	/* Runs the command. */
	error = EINVAL;
	if (argc == 3 && strcmp(argv[1], "base") == 0)
		error = make_base(argv[2], VARIANT_PLAIN);
	else if (argc == 4 && strcmp(argv[1], "update") == 0)
		error = update(argv[2], argv[3]);
	else if (argc == 4 && strcmp(argv[1], "keep") == 0)
		error = keep(argv[2], argv[3]);
	else if (argc == 3 && strcmp(argv[1], "refusals") == 0)
		error = refusals(argv[2]);
	else if (argc == 3 && strcmp(argv[1], "encrypted") == 0)
		error = refuse_encrypted(argv[2]);
	else
		fprintf(stderr, "usage: host-pdf-update base OUT | update IN OUT | keep IN OUT | refusals DIR | encrypted IN\n");
	if (error != 0) {
		fprintf(stderr, "host-pdf-update %s: error %d\n", argc > 1 ? argv[1] : "", error);
		return 1;
	}
	if (failures != 0) {
		fprintf(stderr, "host-pdf-update %s: %d checks failed\n", argv[1], failures);
		return 1;
	}
	printf("host-pdf-update %s: ok\n", argv[1]);
	return 0;
}

/* Writes the hand-made document of a variant. */
static int
make_base(
	const char *path,
	enum variant variant)
{
	static const char page1[] =
	    "0.9 0.9 0.85 rg 0 0 400 300 re f\n"
	    "q 120 0 0 90 250 180 cm /Im0 Do Q\n"
	    "0 0.5 0 RG 6 w 30 30 m 370 270 l S\n"
	    "/GS0 gs 1 0.6 0 rg 200 150 m 260 150 l 260 210 l 200 210 l h f\n"
	    "2 0 0 2 0 0 cm 0 1 0 rg\n";
	static const char page2a[] = "1 0 0 rg 40 40 200 100 re f\n";
	static const char page2b[] = "/GS0 gs 0 0 1 rg 100 100 150 150 re f 3 0 0 3 0 0 cm\n";
	struct buffer pdf;
	struct buffer image;
	size_t offsets[20];
	char text[1024];
	unsigned char pixel[3];
	size_t xref;
	int x;
	int y;
	int object;
	int error;

	/* A 16x16 RGB image: a colour ramp. */
	memset(&pdf, 0, sizeof(pdf));
	memset(&image, 0, sizeof(image));
	for (y = 0; y < 16; y++) {
		for (x = 0; x < 16; x++) {
			pixel[0] = (unsigned char)(x * 16);
			pixel[1] = (unsigned char)(y * 16);
			pixel[2] = (unsigned char)(255 - x * 8);
			append(&image, pixel, 3);
		}
	}

	/* The header. */
	memset(offsets, 0, sizeof(offsets));
	append_text(&pdf, "%PDF-1.6\n%\xE2\xE3\xCF\xD3\n");

	/* 1: the catalog, with a name tree of attached files (13), and the form of the signed variant. */
	offsets[1] = pdf.size;
	append_text(&pdf, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R /PageMode /UseNone /Names << /EmbeddedFiles 13 0 R >>");
	if (variant == VARIANT_SIGNED)
		append_text(&pdf, " /AcroForm << /Fields [] /SigFlags 3 >>");
	append_text(&pdf, " >>\nendobj\n");

	/* 2: the root of the page tree, which passes down the media box and the resources (7). */
	offsets[2] = pdf.size;
	append_text(&pdf, "2 0 obj\n<< /Type /Pages /Kids [3 0 R 6 0 R] /Count 3 /MediaBox [0 0 400 300] /Resources 7 0 R >>\nendobj\n");

	/* 3: a node with the first two pages (the direct-page variant writes the second into the kids). */
	offsets[3] = pdf.size;
	if (variant == VARIANT_DIRECT_PAGE)
		append_text(&pdf, "3 0 obj\n<< /Type /Pages /Parent 2 0 R /Kids [4 0 R << /Type /Page /Contents 9 0 R >>] /Count 2 >>\nendobj\n");
	else
		append_text(&pdf, "3 0 obj\n<< /Type /Pages /Parent 2 0 R /Kids [4 0 R 5 0 R] /Count 2 >>\nendobj\n");

	/* 4: page 1, which inherits everything. */
	offsets[4] = pdf.size;
	append_text(&pdf, "4 0 obj\n<< /Type /Page /Parent 3 0 R /Contents 8 0 R >>\nendobj\n");

	/* 5: page 2, turned a quarter and cropped, with two content streams and its own resources. */
	offsets[5] = pdf.size;
	append_text(&pdf, "5 0 obj\n<< /Type /Page /Parent 3 0 R /Contents [9 0 R 10 0 R] /Rotate 90 /CropBox [20 10 380 290]"
	    " /Resources << /ExtGState << /GS0 11 0 R >> >> >>\nendobj\n");

	/* 6: page 3, portrait, without content. */
	offsets[6] = pdf.size;
	append_text(&pdf, "6 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] >>\nendobj\n");

	/* 7: the shared resources (the taken-name variant already uses the update's first name). */
	offsets[7] = pdf.size;
	if (variant == VARIANT_TAKEN_NAME)
		append_text(&pdf, "7 0 obj\n<< /ExtGState << /GS0 11 0 R /KeiGS0 11 0 R >> /XObject << /Im0 12 0 R >> >>\nendobj\n");
	else
		append_text(&pdf, "7 0 obj\n<< /ExtGState << /GS0 11 0 R >> /XObject << /Im0 12 0 R >> >>\nendobj\n");

	/* 8 to 10: the content streams; each leaves the graphics state changed at its end. */
	offsets[8] = pdf.size;
	sprintf(text, "8 0 obj\n<< /Length %lu >>\nstream\n%s\nendstream\nendobj\n", (unsigned long)strlen(page1), page1);
	append_text(&pdf, text);
	offsets[9] = pdf.size;
	sprintf(text, "9 0 obj\n<< /Length %lu >>\nstream\n%s\nendstream\nendobj\n", (unsigned long)strlen(page2a), page2a);
	append_text(&pdf, text);
	offsets[10] = pdf.size;
	sprintf(text, "10 0 obj\n<< /Length %lu >>\nstream\n%s\nendstream\nendobj\n", (unsigned long)strlen(page2b), page2b);
	append_text(&pdf, text);

	/* 11: a half-opaque graphics state; 12: the image. */
	offsets[11] = pdf.size;
	append_text(&pdf, "11 0 obj\n<< /Type /ExtGState /ca 0.5 >>\nendobj\n");
	offsets[12] = pdf.size;
	sprintf(text, "12 0 obj\n<< /Type /XObject /Subtype /Image /Width 16 /Height 16 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Length %lu >>\nstream\n",
	    (unsigned long)image.size);
	append_text(&pdf, text);
	append(&pdf, image.data, image.size);
	append_text(&pdf, "\nendstream\nendobj\n");

	/* 13 and 14: the name tree of attached files, a root with one kid; 15 and 16: the file. */
	offsets[13] = pdf.size;
	append_text(&pdf, "13 0 obj\n<< /Kids [14 0 R] >>\nendobj\n");
	offsets[14] = pdf.size;
	append_text(&pdf, "14 0 obj\n<< /Names [(readme.txt) 15 0 R] /Limits [(readme.txt) (readme.txt)] >>\nendobj\n");
	offsets[15] = pdf.size;
	append_text(&pdf, "15 0 obj\n<< /Type /Filespec /F (readme.txt) /UF (readme.txt) /EF << /F 16 0 R >> >>\nendobj\n");
	offsets[16] = pdf.size;
	append_text(&pdf, "16 0 obj\n<< /Type /EmbeddedFile /Subtype /text#2Fplain /Length 6 >>\nstream\nhello\n\nendstream\nendobj\n");

	/* 17: the information dictionary. */
	offsets[17] = pdf.size;
	append_text(&pdf, "17 0 obj\n<< /Title (A foreign test \\(p014\\)) /Producer (hand) /ModDate (D:20200101000000Z) >>\nendobj\n");

	/* The cross-reference table and the trailer (the encrypted variant names an encryption dictionary). */
	xref = pdf.size;
	append_text(&pdf, "xref\n0 18\n0000000000 65535 f \n");
	for (object = 1; object <= 17; object++) {
		sprintf(text, "%010lu 00000 n \n", (unsigned long)offsets[object]);
		append_text(&pdf, text);
	}
	append_text(&pdf, "trailer\n<< /Size 18 /Root 1 0 R /Info 17 0 R /ID [<000102030405060708090A0B0C0D0E0F> <0F0E0D0C0B0A09080706050403020100>]");
	if (variant == VARIANT_ENCRYPTED)
		append_text(&pdf, " /Encrypt << /Filter /Standard /V 1 /R 2 /O <00> /U <00> /P -4 >>");
	sprintf(text, " >>\nstartxref\n%lu\n%%%%EOF", (unsigned long)xref);
	append_text(&pdf, text);

	/* Writes the file; the plain one ends without a line end, as some writers leave it. */
	error = write_file(path, &pdf);
	free(pdf.data);
	free(image.data);
	return error;
}

/* Adds the revision of the update command and checks the result. */
static int
update(
	const char *in,
	const char *out)
{
	static const char edit_data[] = "ZNOT edit data of the test";
	struct pdf_document *base;
	struct pdf_document *result;
	struct pdf_writer *writer;
	struct pdf_page_box box;
	unsigned char *before;
	unsigned char *after;
	unsigned char added_hash[2][32];
	unsigned char digest[32];
	const void *data;
	size_t before_size;
	size_t after_size;
	size_t base_xref;
	size_t previous;
	size_t newest;
	size_t size;
	time_t creation;
	time_t modification;
	int difference;
	int error;

	/* The document to add to. */
	error = pdf_document_open(in, &base);
	if (error != 0)
		return error;
	(void)pdf_document_get_revision(base, &base_xref, &previous);
	error = pdf_writer_create_update(base, &writer);
	if (error != 0)
		return error;

	/* Page 1 drawn over: a translucent red pen stroke and a blue box in the top left, as shown. */
	error = pdf_writer_begin_page_over(writer, 0, PDF_PAGE_OVERLAY);
	check(error == 0, "page 1 begun over");
	error = draw_pen(writer, 40.0, 60.0, 0.9, 0.1, 0.1, 0.5);
	check(error == 0, "page 1 pen stroke");
	error = pdf_writer_set_fill_color(writer, 0.0, 0.0, 1.0, 1.0);
	check(error == 0, "page 1 colour");
	(void)pdf_writer_move_to(writer, 10.0, 10.0);
	(void)pdf_writer_line_to(writer, 60.0, 10.0);
	(void)pdf_writer_line_to(writer, 60.0, 30.0);
	(void)pdf_writer_line_to(writer, 10.0, 30.0);
	(void)pdf_writer_close_path(writer);
	error = pdf_writer_fill(writer, PDF_FILL_NONZERO);
	check(error == 0, "page 1 box");
	(void)pdf_writer_end_page(writer);

	/* A page added after page 1, with a green box. */
	error = pdf_writer_begin_page(writer, 200.0, 200.0);
	check(error == 0, "added page begun");
	(void)pdf_writer_set_fill_color(writer, 0.0, 0.7, 0.2, 1.0);
	(void)pdf_writer_move_to(writer, 20.0, 20.0);
	(void)pdf_writer_line_to(writer, 180.0, 20.0);
	(void)pdf_writer_line_to(writer, 180.0, 100.0);
	(void)pdf_writer_line_to(writer, 20.0, 100.0);
	(void)pdf_writer_close_path(writer);
	(void)pdf_writer_fill(writer, PDF_FILL_NONZERO);
	(void)pdf_writer_end_page(writer);

	/* Page 2, turned a quarter: a black box in the top left as shown, and a stroke. */
	error = pdf_writer_begin_page_over(writer, 1, PDF_PAGE_OVERLAY);
	check(error == 0, "page 2 begun over");
	(void)pdf_writer_set_fill_color(writer, 0.0, 0.0, 0.0, 1.0);
	(void)pdf_writer_move_to(writer, 10.0, 10.0);
	(void)pdf_writer_line_to(writer, 60.0, 10.0);
	(void)pdf_writer_line_to(writer, 60.0, 30.0);
	(void)pdf_writer_line_to(writer, 10.0, 30.0);
	(void)pdf_writer_close_path(writer);
	(void)pdf_writer_fill(writer, PDF_FILL_NONZERO);
	error = draw_pen(writer, 80.0, 150.0, 0.1, 0.2, 0.8, 1.0);
	check(error == 0, "page 2 pen stroke");
	(void)pdf_writer_end_page(writer);

	/* Page 3, which has no content: drawn over all the same. */
	error = pdf_writer_begin_page_over(writer, 2, PDF_PAGE_OVERLAY);
	check(error == 0, "page 3 begun over");
	error = draw_pen(writer, 50.0, 300.0, 0.0, 0.0, 0.0, 1.0);
	check(error == 0, "page 3 pen stroke");
	(void)pdf_writer_end_page(writer);

	/* A page added at the end. */
	error = pdf_writer_begin_page(writer, 300.0, 200.0);
	check(error == 0, "last page begun");
	error = draw_pen(writer, 30.0, 100.0, 0.5, 0.0, 0.5, 0.4);
	check(error == 0, "last page pen stroke");
	(void)pdf_writer_end_page(writer);

	/* The edit data. */
	error = pdf_writer_attach_file(writer, UPDATE_NAME, UPDATE_TYPE, edit_data, sizeof(edit_data) - 1);
	check(error == 0, "attached");

	/* The hashes of the added pages are known; those of the pages drawn over are not. */
	error = pdf_writer_get_page_content_hash(writer, 1, added_hash[0]);
	check(error == 0, "added page hash");
	error = pdf_writer_get_page_content_hash(writer, 4, added_hash[1]);
	check(error == 0, "last page hash");
	error = pdf_writer_get_page_content_hash(writer, 0, digest);
	check(error == ENOENT, "drawn-over page hash unknown");

	/* Saves. */
	error = pdf_writer_save(writer, out);
	check(error == 0, "saved");
	pdf_writer_destroy(writer);
	pdf_document_close(base);
	if (error != 0)
		return error;

	/* The file starts with the document's bytes, unchanged. */
	error = read_file(in, &before, &before_size);
	if (error != 0)
		return error;
	error = read_file(out, &after, &after_size);
	if (error != 0)
		return error;
	check(after_size > before_size, "file grew");
	difference = memcmp(before, after, before_size);
	check(difference == 0, "original bytes untouched");
	printf("original %lu bytes, updated %lu bytes (revision %lu bytes)\n", (unsigned long)before_size, (unsigned long)after_size,
	    (unsigned long)(after_size - before_size));
	free(before);
	free(after);

	/* Reads the result back. */
	error = pdf_document_open(out, &result);
	check(error == 0, "result opens");
	if (error != 0)
		return error;
	check(pdf_document_page_count(result) == 5, "five pages");

	/* The revision links the document's section. */
	(void)pdf_document_get_revision(result, &newest, &previous);
	check(previous == base_xref, "revision links the original section");
	check(newest > before_size, "newest section is in the revision");

	/* The pages' boxes: the turned page keeps its rotation and crop box; the added ones are upright. */
	error = pdf_document_page_box(result, 2, &box);
	check(error == 0 && box.rotation == 90 && box.crop_left == 20.0, "page 2 turned and cropped");
	error = pdf_document_page_box(result, 1, &box);
	check(error == 0 && box.rotation == 0 && box.width == 200.0 && box.height == 200.0, "added page upright 200x200");
	error = pdf_document_page_box(result, 3, &box);
	check(error == 0 && box.width == 300.0 && box.height == 400.0, "page 3 keeps its box");
	error = pdf_document_page_box(result, 4, &box);
	check(error == 0 && box.rotation == 0 && box.width == 300.0 && box.height == 200.0, "last page upright 300x200");

	/* The added pages hash as the writer said. */
	error = pdf_document_page_content_hash(result, 1, digest);
	check(error == 0 && memcmp(digest, added_hash[0], 32) == 0, "added page hash matches");
	error = pdf_document_page_content_hash(result, 4, digest);
	check(error == 0 && memcmp(digest, added_hash[1], 32) == 0, "last page hash matches");

	/*
	 * The edit data, and the file the document had (which qpdf's rewrite
	 * compresses, and the reader does not decode an attachment yet: found
	 * all the same, as ENOTSUP rather than ENOENT).
	 */
	error = pdf_document_find_attachment_type(result, UPDATE_NAME, UPDATE_TYPE, &data, &size);
	check(error == 0 && size == sizeof(edit_data) - 1 && memcmp(data, edit_data, size) == 0, "edit data attached");
	error = pdf_document_find_attachment(result, "readme.txt", &data, &size);
	check((error == 0 && size == 6) || error == ENOTSUP, "the document's own attachment kept");

	/* The identifier's permanent element and the dates. */
	error = pdf_document_get_id(result, digest);
	check(error == 0 && digest[0] == 0x00 && digest[15] == 0x0f, "permanent identifier kept");
	(void)pdf_document_get_dates(result, &creation, &modification);
	check(modification > (time_t)1700000000, "modification date of the save");
	pdf_document_close(result);
	return 0;
}

/* Adds a revision that keeps every page and replaces the edit data. */
static int
keep(
	const char *in,
	const char *out)
{
	static const char edit_data[] = "ZNOT the second edit data";
	struct pdf_document *base;
	struct pdf_document *result;
	struct pdf_writer *writer;
	const void *data;
	size_t pages;
	size_t page;
	size_t size;
	int error;

	/* The document to add to, every page kept. */
	error = pdf_document_open(in, &base);
	if (error != 0)
		return error;
	error = pdf_writer_create_update(base, &writer);
	if (error != 0)
		return error;
	pages = pdf_document_page_count(base);
	for (page = 0; page < pages; page++) {
		error = pdf_writer_keep_page(writer, page);
		check(error == 0, "page kept");
	}
	error = pdf_writer_attach_file(writer, UPDATE_NAME, UPDATE_TYPE, edit_data, sizeof(edit_data) - 1);
	check(error == 0, "attached");
	error = pdf_writer_save(writer, out);
	check(error == 0, "saved");
	pdf_writer_destroy(writer);
	pdf_document_close(base);

	/* The new edit data replaces the old one. */
	error = pdf_document_open(out, &result);
	check(error == 0, "result opens");
	if (error != 0)
		return error;
	check(pdf_document_page_count(result) == pages, "same pages");
	error = pdf_document_find_attachment_type(result, UPDATE_NAME, UPDATE_TYPE, &data, &size);
	check(error == 0 && size == sizeof(edit_data) - 1 && memcmp(data, edit_data, size) == 0, "second edit data found");
	error = pdf_document_find_attachment(result, "readme.txt", &data, &size);
	check(error == 0 || error == ENOTSUP, "the document's own attachment kept");
	pdf_document_close(result);
	return 0;
}

/*
 * ws079-p008: an encrypted document the reader opens (its user password is
 * empty) is refused by the update, which would have to encrypt its revision.
 */
static int
refuse_encrypted(
	const char *path)
{
	struct pdf_document *document;
	struct pdf_writer *writer;
	int encrypted;
	int error;

	/* The reader opens it and tells it is encrypted. */
	error = pdf_document_open(path, &document);
	check(error == 0, "encrypted document with an empty password opens");
	if (error != 0)
		return 0;
	encrypted = 0;
	error = pdf_document_encrypted(path, &encrypted);
	check(error == 0 && encrypted == 1, "encrypted document told");

	/* The update refuses it. */
	error = pdf_writer_create_update(document, &writer);
	check(error == EACCES, "encrypted document refused by the update");
	if (error == 0)
		pdf_writer_destroy(writer);
	pdf_document_close(document);
	return 0;
}

/* Checks each refusal. */
static int
refusals(
	const char *folder)
{
	struct pdf_document *document;
	struct pdf_writer *writer;
	char path[1024];
	int encrypted;
	int error;

	/* A signed document. */
	sprintf(path, "%s/signed.pdf", folder);
	(void)make_base(path, VARIANT_SIGNED);
	error = pdf_document_open(path, &document);
	check(error == 0, "signed document opens");
	if (error == 0) {
		error = pdf_writer_create_update(document, &writer);
		check(error == PDF_ESIGNED, "signed document refused");
		pdf_document_close(document);
	}

	/* An encrypted document: not opened, and told apart. */
	sprintf(path, "%s/encrypted.pdf", folder);
	(void)make_base(path, VARIANT_ENCRYPTED);
	error = pdf_document_open(path, &document);
	check(error == ENOTSUP, "encrypted document not opened");
	encrypted = 0;
	error = pdf_document_encrypted(path, &encrypted);
	check(error == 0 && encrypted == 1, "encrypted document told");
	sprintf(path, "%s/signed.pdf", folder);
	encrypted = 1;
	error = pdf_document_encrypted(path, &encrypted);
	check(error == 0 && encrypted == 0, "plain document not encrypted");

	/* Pages listed out of order, twice, or not all. */
	sprintf(path, "%s/plain.pdf", folder);
	(void)make_base(path, VARIANT_PLAIN);
	error = pdf_document_open(path, &document);
	if (error != 0)
		return error;
	error = pdf_writer_create_update(document, &writer);
	if (error != 0)
		return error;
	error = pdf_writer_keep_page(writer, 1);
	check(error == EINVAL, "page 2 before page 1 refused");
	error = pdf_writer_keep_page(writer, 0);
	check(error == 0, "page 1 kept");
	error = pdf_writer_keep_page(writer, 0);
	check(error == EINVAL, "page 1 twice refused");
	error = pdf_writer_begin_page_over(writer, 7, PDF_PAGE_OVERLAY);
	check(error == EINVAL, "missing page refused");
	sprintf(path, "%s/partial.pdf", folder);
	error = pdf_writer_save(writer, path);
	check(error == EINVAL, "save before every page is listed refused");
	pdf_writer_destroy(writer);

	/* A writer of a new document cannot list pages of one. */
	error = pdf_writer_create(&writer);
	if (error != 0)
		return error;
	error = pdf_writer_keep_page(writer, 0);
	check(error == EINVAL, "keep on a new document refused");
	pdf_writer_destroy(writer);
	pdf_document_close(document);

	/* A name the page's resources already use: the update takes another prefix (ws175-p003, design.md [M4]) and saves. */
	sprintf(path, "%s/taken.pdf", folder);
	(void)make_base(path, VARIANT_TAKEN_NAME);
	error = pdf_document_open(path, &document);
	if (error != 0)
		return error;
	error = pdf_writer_create_update(document, &writer);
	if (error != 0)
		return error;
	(void)pdf_writer_begin_page_over(writer, 0, PDF_PAGE_OVERLAY);
	(void)draw_pen(writer, 40.0, 60.0, 1.0, 0.0, 0.0, 0.5);
	(void)pdf_writer_end_page(writer);
	(void)pdf_writer_keep_page(writer, 1);
	(void)pdf_writer_keep_page(writer, 2);
	sprintf(path, "%s/taken-out.pdf", folder);
	error = pdf_writer_save(writer, path);
	check(error == 0, "a name the page uses: another prefix, saved");
	pdf_writer_destroy(writer);
	pdf_document_close(document);

	/* A page written into its parent's kids cannot be replaced. */
	sprintf(path, "%s/direct.pdf", folder);
	(void)make_base(path, VARIANT_DIRECT_PAGE);
	error = pdf_document_open(path, &document);
	if (error != 0)
		return error;
	error = pdf_writer_create_update(document, &writer);
	if (error != 0)
		return error;
	(void)pdf_writer_keep_page(writer, 0);
	(void)pdf_writer_begin_page_over(writer, 1, PDF_PAGE_OVERLAY);
	(void)draw_pen(writer, 40.0, 60.0, 1.0, 0.0, 0.0, 1.0);
	(void)pdf_writer_end_page(writer);
	(void)pdf_writer_keep_page(writer, 2);
	sprintf(path, "%s/direct-out.pdf", folder);
	error = pdf_writer_save(writer, path);
	check(error == ENOTSUP, "a page without a reference refused");
	pdf_writer_destroy(writer);
	pdf_document_close(document);
	return 0;
}

/* Draws a wavy pen stroke with rising pressure from a point, as its outline. */
static int
draw_pen(
	struct pdf_writer *writer,
	double x,
	double y,
	double red,
	double green,
	double blue,
	double alpha)
{
	struct pdf_stroke_point points[24];
	struct pdf_point *outline;
	size_t count;
	size_t index;
	int error;

	/* The samples. */
	for (index = 0; index < 24; index++) {
		points[index].x = x + (double)index * 8.0;
		points[index].y = y + sin((double)index * 0.5) * 14.0;
		points[index].pressure = 0.2 + 0.8 * (double)index / 23.0;
	}

	/* The outline, filled in the colour. */
	error = pdf_outline_stroke(points, 24, 8.0, &outline, &count);
	if (error != 0)
		return error;
	error = pdf_writer_set_fill_color(writer, red, green, blue, alpha);
	if (error == 0)
		error = pdf_writer_fill_outline(writer, outline, count);
	pdf_outline_free(outline);
	return error;
}

/* Reads a whole file. */
static int
read_file(
	const char *path,
	unsigned char **data,
	size_t *size)
{
	FILE *file;
	long length;

	/* The file's size, then its bytes. */
	file = fopen(path, "rb");
	if (file == NULL)
		return errno;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	*data = malloc((size_t)length + 1);
	if (*data == NULL) {
		fclose(file);
		return ENOMEM;
	}
	*size = fread(*data, 1, (size_t)length, file);
	fclose(file);
	return 0;
}

/* Records one check. */
static void
check(
	int condition,
	const char *what)
{
	/* Prints the check and counts a failure. */
	printf("%s: %s\n", what, condition ? "ok" : "FAILED");
	if (!condition)
		failures++;
}

/* Appends bytes to a buffer. */
static void
append(
	struct buffer *buffer,
	const void *data,
	size_t size)
{
	unsigned char *grown;
	size_t capacity;

	/* Grows the buffer, then copies. */
	if (buffer->size + size > buffer->capacity) {
		capacity = (buffer->size + size) * 2 + 256;
		grown = realloc(buffer->data, capacity);
		if (grown == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
		buffer->data = grown;
		buffer->capacity = capacity;
	}
	memcpy(buffer->data + buffer->size, data, size);
	buffer->size += size;
}

/* Appends text to a buffer. */
static void
append_text(
	struct buffer *buffer,
	const char *text)
{
	/* The text without its terminator. */
	append(buffer, text, strlen(text));
}

/* Writes a buffer to a file. */
static int
write_file(
	const char *path,
	const struct buffer *buffer)
{
	FILE *file;
	size_t written;

	/* The whole buffer in one write. */
	file = fopen(path, "wb");
	if (file == NULL)
		return errno;
	written = fwrite(buffer->data, 1, buffer->size, file);
	fclose(file);
	if (written != buffer->size)
		return EIO;
	return 0;
}
