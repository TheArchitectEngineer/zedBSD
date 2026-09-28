/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of libpdf's reader (ws079-p004).
 *
 * 1. Round trip: writes a three-page document with libpdf's writer (pages of
 *    three sizes, pressure strokes, an image, an attached edit-data file
 *    whose bytes contain "endstream" and every string escape), reads it back
 *    from the file and from memory, and compares the page count, the boxes,
 *    the attachment's bytes, the page content hashes with the writer's,
 *    the identifier and the dates.  The hashes are printed so that
 *    run-pdf-reader.sh can compare them with qpdf's stream data.
 * 2. Incremental update: appends a revision that replaces page 1 (a
 *    rotated, cropped page whose content is an array of two streams) and
 *    the catalog (the attachment only in the EmbeddedFiles name tree, one
 *    level down), linked with /Prev, and checks what the reader sees.
 * 3. Hand-made files for each refusal and limit: cycles in the page tree,
 *    the stream length and the name tree, nesting past the limit, a /Prev
 *    loop, a cross-reference stream, encryption, a filter, inheritance,
 *    rotations, escapes in names and strings, dates.
 * 4. Robustness: every truncation of a small document, and a fixed number
 *    of deterministic random corruptions (byte changes, digit changes,
 *    insertions and deletions) of it and of the round-trip file; each
 *    result is opened and every call is made on it.  Under ASan and UBSan
 *    any read out of bounds ends the test.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sha2.h>

#include <pdf.h>

/* The size of a hand-made document's buffer. */
#define TEST_PDF_MAX 65536

/* The most objects a hand-made document has. */
#define TEST_OBJECTS_MAX 64

/* The size of the round trip's attached file. */
#define TEST_ATTACHMENT_SIZE 3000

/* The number of random corruptions of each fuzzed document. */
#define TEST_FUZZ_ROUNDS 30000

/*
 * A hand-made PDF being built: its bytes and each object's offset.
 */
struct test_pdf {
	char data[TEST_PDF_MAX];
	size_t length;
	size_t offsets[TEST_OBJECTS_MAX];
	int present[TEST_OBJECTS_MAX];
};

/* The number of checks that failed, which main reports. */
static int test_failures;

/* The state of the fuzzer's xorshift generator, fixed so every run is the same. */
static unsigned long test_random_state = 0x2545f491UL;

static void expect(int condition, const char *what);
static void expect_error(int error, int expected, const char *what);
static int round_trip(const char *path, unsigned char **file, size_t *file_size);
static int draw_page(struct pdf_writer *writer, double width, double height, int seed);
static void check_document(struct pdf_document *document, const unsigned char hashes[3][32], const unsigned char *attachment, const char *how);
static void check_incremental(const unsigned char *file, size_t size, const unsigned char *attachment);
static void check_hand_made(void);
static void pdf_begin(struct test_pdf *pdf);
static void pdf_append(struct test_pdf *pdf, const char *text);
static void pdf_object(struct test_pdf *pdf, int number, const char *body);
static void pdf_stream(struct test_pdf *pdf, int number, const char *dictionary, const char *data);
static void pdf_finish(struct test_pdf *pdf, int size, const char *trailer);
static int open_hand_made(const struct test_pdf *pdf, struct pdf_document **document);
static void exercise(struct pdf_document *document);
static void fuzz(const unsigned char *file, size_t size, const char *name);
static unsigned long next_random(void);
static void print_hash(const char *label, const unsigned char digest[32]);
static const unsigned char *find_last(const unsigned char *file, size_t size, const char *text);

/*
 * Runs every part of the test; the first argument is the round trip's output path.
 */
int
main(
	int argc,
	char **argv)
{
	struct test_pdf small;
	unsigned char *file;
	size_t file_size;
	unsigned char attachment[TEST_ATTACHMENT_SIZE];
	size_t index;
	int error;

	/* Needs the output path. */
	if (argc != 2) {
		fprintf(stderr, "usage: host-pdf-reader out.pdf\n");
		return 2;
	}

	/* Writes and reads back the round-trip document. */
	file = NULL;
	file_size = 0;
	error = round_trip(argv[1], &file, &file_size);
	if (error != 0) {
		fprintf(stderr, "host-pdf-reader: the round trip failed: %s\n", strerror(error));
		return 1;
	}

	/* Rebuilds the attachment's bytes, which the round trip made the same way. */
	for (index = 0; index < TEST_ATTACHMENT_SIZE; index++)
		attachment[index] = (unsigned char)((index * 7 + index / 256) & 0xff);
	memcpy(attachment + 100, "\nendstream\nendobj\n", 18);

	/* Checks a revision appended to the file. */
	check_incremental(file, file_size, attachment);

	/* Checks the hand-made files. */
	check_hand_made();

	/* Fuzzes a small hand-made document and the round-trip file. */
	pdf_begin(&small);
	pdf_object(&small, 1, "<< /Type /Catalog /Pages 2 0 R /Names << /EmbeddedFiles << /Names [(a.bin) 5 0 R] >> >> >>");
	pdf_object(&small, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 100 200] >>");
	pdf_object(&small, 3, "<< /Type /Page /Parent 2 0 R /Contents 4 0 R /Rotate 90 >>");
	pdf_stream(&small, 4, "", "0 0 m 10 10 l f");
	pdf_object(&small, 5, "<< /Type /Filespec /F (a.bin) /EF << /F 6 0 R >> >>");
	pdf_stream(&small, 6, "/Type /EmbeddedFile /Subtype /application#2Fx-test", "abc");
	pdf_finish(&small, 7, "/ID [<000102030405060708090A0B0C0D0E0F> <00>]");
	fuzz((const unsigned char *)small.data, small.length, "small");
	fuzz(file, file_size, "round-trip");
	free(file);

	/* Reports the checks that failed. */
	if (test_failures != 0) {
		fprintf(stderr, "host-pdf-reader: %d check(s) failed\n", test_failures);
		return 1;
	}

	/* Succeeded: every check passed. */
	printf("host-pdf-reader: ok\n");
	return 0;
}

/* Counts a failed check and names it. */
static void
expect(
	int condition,
	const char *what)
{
	/* A check that holds needs nothing. */
	if (condition)
		return;

	/* Names the failed check. */
	fprintf(stderr, "FAILED: %s\n", what);
	test_failures++;
}

/* Checks that a call reported the expected error. */
static void
expect_error(
	int error,
	int expected,
	const char *what)
{
	/* A call that reported what was expected needs nothing. */
	if (error == expected)
		return;

	/* Names the failed check with both values. */
	fprintf(stderr, "FAILED: %s: got %d (%s), expected %d (%s)\n", what, error, strerror(error), expected, strerror(expected));
	test_failures++;
}

/*
 * Writes the three-page document, reads it back from the file and from
 * memory, and returns the file's bytes for the later parts.
 */
static int
round_trip(
	const char *path,
	unsigned char **file,
	size_t *file_size)
{
	static const unsigned char document_id[16] = {
		0x7a, 0x65, 0x64, 0x42, 0x53, 0x44, 0x2d, 0x72, 0x65, 0x61, 0x64, 0x65, 0x72, 0x2d, 0x70, 0x34
	};
	unsigned char attachment[TEST_ATTACHMENT_SIZE];
	unsigned char hashes[3][32];
	struct pdf_writer *writer;
	struct pdf_document *document;
	unsigned char *bytes;
	char label[32];
	FILE *stream;
	size_t size;
	int index;
	int error;

	/* Makes the attachment's bytes: every byte value, and a false end of stream at byte 100. */
	for (index = 0; index < TEST_ATTACHMENT_SIZE; index++)
		attachment[index] = (unsigned char)((index * 7 + index / 256) & 0xff);
	memcpy(attachment + 100, "\nendstream\nendobj\n", 18);

	/* Writes three pages of different sizes with a fixed identifier and dates. */
	error = pdf_writer_create(&writer);
	if (error != 0)
		return error;
	error = pdf_writer_set_document_id(writer, document_id);
	if (error == 0)
		error = pdf_writer_set_dates(writer, 1790564400, 1790596800);
	if (error == 0)
		error = draw_page(writer, 595.276, 841.89, 1);
	if (error == 0)
		error = draw_page(writer, 792.0, 612.0, 2);
	if (error == 0)
		error = draw_page(writer, 100.5, 50.25, 3);
	if (error == 0)
		error = pdf_writer_attach_file(writer, "zedbsd-notes.bin", "application/x-zedbsd-notes", attachment, sizeof(attachment));

	/* Takes the writer's content hashes of the three pages. */
	for (index = 0; index < 3 && error == 0; index++)
		error = pdf_writer_get_page_content_hash(writer, (size_t)index, hashes[index]);
	expect_error(pdf_writer_get_page_content_hash(writer, 3, hashes[0]), EINVAL, "the writer's hash of a page it does not have");
	if (error == 0)
		error = pdf_writer_save(writer, path);
	pdf_writer_destroy(writer);
	if (error != 0)
		return error;

	/* Prints the hashes for the comparison with qpdf. */
	for (index = 0; index < 3; index++) {
		sprintf(label, "page %d", index + 1);
		print_hash(label, hashes[index]);
	}

	/* Reads the file back and checks it. */
	error = pdf_document_open(path, &document);
	if (error != 0)
		return error;
	check_document(document, (const unsigned char (*)[32])hashes, attachment, "file");
	pdf_document_close(document);

	/* Loads the file's bytes. */
	stream = fopen(path, "rb");
	if (stream == NULL)
		return errno;
	bytes = malloc(1 << 20);
	if (bytes == NULL) {
		fclose(stream);
		return ENOMEM;
	}
	size = fread(bytes, 1, (1 << 20) - 1, stream);
	fclose(stream);
	bytes[size] = '\0';

	/* Reads the bytes from memory and checks them the same way. */
	error = pdf_document_open_memory(bytes, size, &document);
	if (error != 0) {
		free(bytes);
		return error;
	}
	check_document(document, (const unsigned char (*)[32])hashes, attachment, "memory");
	pdf_document_close(document);

	/* A file that does not exist is reported as such. */
	expect_error(pdf_document_open("/nonexistent/x.pdf", &document), ENOENT, "opening a missing file");

	/* Succeeded: the caller owns the file's bytes. */
	*file = bytes;
	*file_size = size;
	return 0;
}

/* Draws one page: a pressure stroke, a translucent one and a small RGBA image. */
static int
draw_page(
	struct pdf_writer *writer,
	double width,
	double height,
	int seed)
{
	struct pdf_stroke_point points[40];
	struct pdf_point *outline;
	unsigned char pixels[4 * 4 * 4];
	size_t count;
	int index;
	int error;

	/* Opens the page. */
	error = pdf_writer_begin_page(writer, width, height);
	if (error != 0)
		return error;

	/* Draws a zigzag stroke whose pressure rises. */
	for (index = 0; index < 40; index++) {
		points[index].x = width * (0.1 + 0.8 * index / 39.0);
		points[index].y = height * (0.3 + 0.1 * (double)((index + seed) % 3));
		points[index].pressure = index / 39.0;
	}
	error = pdf_outline_stroke(points, 40, 4.0 + seed, &outline, &count);
	if (error != 0)
		return error;
	error = pdf_writer_set_fill_color(writer, 0.2, 0.1 * seed, 0.5, 1.0);
	if (error == 0)
		error = pdf_writer_fill_outline(writer, outline, count);
	if (error == 0)
		error = pdf_writer_set_fill_color(writer, 0.9, 0.6, 0.0, 0.5);
	if (error == 0)
		error = pdf_writer_fill_outline(writer, outline, count);
	pdf_outline_free(outline);

	/* Draws a small image in the corner. */
	for (index = 0; index < 64; index++)
		pixels[index] = (unsigned char)(index * 4 + seed);
	if (error == 0)
		error = pdf_writer_draw_rgba_image(writer, pixels, 4, 4, 1.0, 1.0, 10.0, 10.0);
	if (error == 0)
		error = pdf_writer_end_page(writer);
	if (error != 0)
		return error;

	/* Succeeded: the page is drawn. */
	return 0;
}

/* Checks a read-back round-trip document. */
static void
check_document(
	struct pdf_document *document,
	const unsigned char hashes[3][32],
	const unsigned char *attachment,
	const char *how)
{
	static const double sizes[3][2] = { { 595.276, 841.89 }, { 792.0, 612.0 }, { 100.5, 50.25 } };
	static const unsigned char document_id[16] = {
		0x7a, 0x65, 0x64, 0x42, 0x53, 0x44, 0x2d, 0x72, 0x65, 0x61, 0x64, 0x65, 0x72, 0x2d, 0x70, 0x34
	};
	struct pdf_page_box box;
	unsigned char digest[32];
	unsigned char id[16];
	const void *data;
	size_t size;
	size_t index;
	time_t creation;
	time_t modification;
	int error;

	/* The pages and their boxes. */
	printf("%s: %lu pages\n", how, (unsigned long)pdf_document_page_count(document));
	expect(pdf_document_page_count(document) == 3, "three pages");
	for (index = 0; index < 3; index++) {
		error = pdf_document_page_box(document, index, &box);
		expect_error(error, 0, "the page box");
		expect(box.media_left == 0.0 && box.media_bottom == 0.0, "the media box's origin");
		expect(box.media_right == sizes[index][0] && box.media_top == sizes[index][1], "the media box's size");
		expect(box.crop_right == sizes[index][0] && box.crop_top == sizes[index][1], "the crop box is the media box");
		expect(box.rotation == 0, "no rotation");
		expect(box.width == sizes[index][0] && box.height == sizes[index][1], "the page's size as shown");

		/* The content hash is the writer's. */
		error = pdf_document_page_content_hash(document, index, digest);
		expect_error(error, 0, "the page content hash");
		expect(memcmp(digest, hashes[index], 32) == 0, "the page content hash is the writer's");
	}
	expect_error(pdf_document_page_box(document, 3, &box), EINVAL, "the box of a page the document does not have");
	expect_error(pdf_document_page_content_hash(document, 3, digest), EINVAL, "the hash of a page the document does not have");

	/* The attachment, by name and by name and type. */
	error = pdf_document_find_attachment(document, "zedbsd-notes.bin", &data, &size);
	expect_error(error, 0, "finding the attachment");
	expect(size == TEST_ATTACHMENT_SIZE && memcmp(data, attachment, TEST_ATTACHMENT_SIZE) == 0, "the attachment's bytes");
	error = pdf_document_find_attachment_type(document, "zedbsd-notes.bin", "application/x-zedbsd-notes", &data, &size);
	expect_error(error, 0, "finding the attachment by type");
	expect_error(pdf_document_find_attachment_type(document, "zedbsd-notes.bin", "text/plain", &data, &size), ENOENT, "an attachment of another type");
	expect_error(pdf_document_find_attachment(document, "other.bin", &data, &size), ENOENT, "an attachment of another name");

	/* The identifier and the dates. */
	error = pdf_document_get_id(document, id);
	expect_error(error, 0, "the identifier");
	expect(memcmp(id, document_id, 16) == 0, "the identifier is the writer's");
	pdf_document_get_dates(document, &creation, &modification);
	expect(creation == 1790564400 && modification == 1790596800, "the dates are the writer's");
}

/*
 * Appends a revision to the round-trip file and checks what the reader
 * sees through the /Prev chain.
 *
 * The writer numbers the catalog 1, page 1 as 4 with its content 5, and
 * the attachment's file specification last, one below the trailer's /Size.
 */
static void
check_incremental(
	const unsigned char *file,
	size_t size,
	const unsigned char *attachment)
{
	struct pdf_document *document;
	struct pdf_page_box box;
	unsigned char *updated;
	unsigned char digest[32];
	unsigned char content_digest[32];
	const void *data;
	const char *text;
	char addition[4096];
	size_t attachment_size;
	size_t length;
	size_t table;
	size_t old_startxref;
	size_t offsets[3];
	unsigned long object_count;
	SHA2_CTX context;
	int error;

	/* Finds the old /Size and startxref in the last trailer. */
	text = (const char *)find_last(file, size, "trailer");
	expect(text != NULL, "the trailer");
	if (text == NULL)
		return;
	object_count = strtoul(strstr(text, "/Size ") + 6, NULL, 10);
	old_startxref = strtoul(strstr(text, "startxref\n") + 10, NULL, 10);

	/* Appends a new page 1, a new catalog and a name tree node. */
	updated = malloc(size + sizeof(addition));
	memcpy(updated, file, size);
	length = size;
	offsets[0] = length;
	length += (size_t)sprintf((char *)updated + length,
				  "4 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] /CropBox [-10 20 290 500] /Rotate -90 /Contents [5 0 R 5 0 R] >>\nendobj\n");
	offsets[1] = length;
	length += (size_t)sprintf((char *)updated + length,
				  "1 0 obj\n<< /Type /Catalog /Pages 2 0 R /Names << /EmbeddedFiles %lu 0 R >> >>\nendobj\n",
				  object_count);
	offsets[2] = length;
	length += (size_t)sprintf((char *)updated + length,
				  "%lu 0 obj\n<< /Kids [<< /Names [(aaa) 1 0 R (zedbsd-notes.bin) %lu 0 R] >>] >>\nendobj\n",
				  object_count,
				  object_count - 1);

	/* Appends the section that names them, linked to the old one. */
	table = length;
	length += (size_t)sprintf((char *)updated + length,
				  "xref\n0 2\n0000000000 65535 f \n%010lu 00000 n \n4 1\n%010lu 00000 n \n%lu 1\n%010lu 00000 n \n"
				  "trailer\n<< /Size %lu /Root 1 0 R /Prev %lu >>\nstartxref\n%lu\n%%%%EOF\n",
				  (unsigned long)offsets[1],
				  (unsigned long)offsets[0],
				  object_count,
				  (unsigned long)offsets[2],
				  object_count + 1,
				  (unsigned long)old_startxref,
				  (unsigned long)table);

	/* Opens the updated file. */
	error = pdf_document_open_memory(updated, length, &document);
	expect_error(error, 0, "opening the updated file");
	if (error != 0) {
		free(updated);
		return;
	}

	/* Page 1 is the new one: the crop box clipped to the media box, turned a quarter. */
	error = pdf_document_page_box(document, 0, &box);
	expect_error(error, 0, "the updated page's box");
	expect(box.media_right == 300.0 && box.media_top == 400.0, "the updated media box");
	expect(box.crop_left == 0.0 && box.crop_bottom == 20.0 && box.crop_right == 290.0 && box.crop_top == 400.0, "the updated crop box");
	expect(box.rotation == 270, "a rotation of -90 is 270");
	expect(box.width == 380.0 && box.height == 290.0, "the rotated page's size as shown");

	/* Its content is the old page 1's stream twice. */
	error = pdf_document_page_content_hash(document, 0, digest);
	expect_error(error, 0, "the updated page's hash");
	text = strstr((const char *)file, "5 0 obj\n<< /Length ");
	length = strtoul(text + 19, NULL, 10);
	text = strstr(text, "stream\n") + 7;
	SHA256Init(&context);
	SHA256Update(&context, (const unsigned char *)text, length);
	SHA256Update(&context, (const unsigned char *)text, length);
	SHA256Final(content_digest, &context);
	expect(memcmp(digest, content_digest, 32) == 0, "the hash of a content array is that of its streams joined");

	/* The attachment is found through the name tree alone, and the identifier is gone. */
	error = pdf_document_find_attachment_type(document, "zedbsd-notes.bin", "application/x-zedbsd-notes", &data, &attachment_size);
	expect_error(error, 0, "the attachment through the name tree");
	expect(attachment_size == TEST_ATTACHMENT_SIZE && memcmp(data, attachment, TEST_ATTACHMENT_SIZE) == 0, "the attachment's bytes through the name tree");
	expect_error(pdf_document_find_attachment(document, "aaa", &data, &attachment_size), ENOENT, "a name tree value that is no file specification");
	expect_error(pdf_document_get_id(document, content_digest), ENOENT, "the new trailer has no identifier");
	expect(pdf_document_page_count(document) == 3, "the update keeps three pages");
	pdf_document_close(document);
	free(updated);
}

/* Checks the hand-made files, one refusal or limit each. */
static void
check_hand_made(void)
{
	struct test_pdf pdf;
	struct pdf_document *document;
	struct pdf_page_box box;
	unsigned char digest[32];
	unsigned char expected[32];
	unsigned char id[16];
	const void *data;
	char body[8192];
	SHA2_CTX context;
	size_t size;
	size_t index;
	time_t creation;
	time_t modification;
	int error;

	/* Not a PDF, and nothing at all. */
	expect_error(pdf_document_open_memory("hello", 5, &document), PDF_EFORMAT, "a file that is not a PDF");
	expect_error(pdf_document_open_memory(NULL, 0, &document), PDF_EFORMAT, "an empty file");
	expect_error(pdf_document_open_memory(NULL, 1, &document), EINVAL, "missing bytes");

	/* A minimal document whose stream length is indirect and whose data holds endstream. */
	pdf_begin(&pdf);
	pdf_object(&pdf, 1, "<< /Type /Catalog /Pages 2 0 R >>");
	pdf_object(&pdf, 2, "<< /Type /Pages /Kids [3 0 R 3 0 R 2 0 R] /Count 1 >>");
	pdf_object(&pdf, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 10 20] /Contents 4 0 R >>");
	pdf_object(&pdf, 4, "<< /Length 5 0 R >>\nstream\nendstream endobj\nendstream");
	pdf_object(&pdf, 5, "17");
	pdf_finish(&pdf, 6, "/Info 6 0 R");
	error = open_hand_made(&pdf, &document);
	expect_error(error, 0, "the minimal document");
	if (error == 0) {
		expect(pdf_document_page_count(document) == 1, "a page listed twice and a looping kid count once");
		error = pdf_document_page_content_hash(document, 0, digest);
		expect_error(error, 0, "the hash of a stream with an indirect length");
		SHA256Init(&context);
		SHA256Update(&context, (const unsigned char *)"endstream endobj\n", 17);
		SHA256Final(expected, &context);
		expect(memcmp(digest, expected, 32) == 0, "the hash covers the length's bytes, past a false endstream");
		pdf_document_get_dates(document, &creation, &modification);
		expect(creation == 0 && modification == 0, "a missing information dictionary gives no dates");
		pdf_document_close(document);
	}

	/* A stream whose length is itself. */
	pdf_begin(&pdf);
	pdf_object(&pdf, 1, "<< /Type /Catalog /Pages 2 0 R >>");
	pdf_object(&pdf, 2, "<< /Type /Pages /Kids [3 0 R] /MediaBox [0 0 10 20] >>");
	pdf_object(&pdf, 3, "<< /Type /Page /Contents 4 0 R >>");
	pdf_object(&pdf, 4, "<< /Length 4 0 R >>\nstream\nabc\nendstream");
	pdf_finish(&pdf, 5, "");
	error = open_hand_made(&pdf, &document);
	expect_error(error, 0, "a document whose content's length is itself opens");
	if (error == 0) {
		expect_error(pdf_document_page_content_hash(document, 0, digest), PDF_EFORMAT, "a stream whose length is itself");
		expect_error(pdf_document_page_content_hash(document, 0, digest), PDF_EFORMAT, "the same stream a second time");
		error = pdf_document_page_box(document, 0, &box);
		expect_error(error, 0, "an inherited media box");
		expect(box.width == 10.0 && box.height == 20.0, "the inherited media box's size");
		pdf_document_close(document);
	}

	/* A page tree node that lists itself a thousand times. */
	pdf_begin(&pdf);
	pdf_object(&pdf, 1, "<< /Type /Catalog /Pages 2 0 R >>");
	strcpy(body, "<< /Type /Pages /Kids [");
	for (index = 0; index < 1000; index++)
		strcat(body, "2 0 R ");
	strcat(body, "3 0 R] /MediaBox [0 0 1 1] >>");
	pdf_object(&pdf, 2, body);
	pdf_object(&pdf, 3, "<< /Type /Page >>");
	pdf_finish(&pdf, 4, "");
	error = open_hand_made(&pdf, &document);
	expect_error(error, 0, "a node that lists itself");
	if (error == 0) {
		expect(pdf_document_page_count(document) == 1, "the node's one page");
		expect_error(pdf_document_page_content_hash(document, 0, digest), 0, "a page without content hashes nothing");
		SHA256Init(&context);
		SHA256Final(expected, &context);
		expect(memcmp(digest, expected, 32) == 0, "the hash of no content");
		pdf_document_close(document);
	}

	/* Arrays nested past the limit. */
	pdf_begin(&pdf);
	pdf_object(&pdf, 1, "<< /Type /Catalog /Pages 2 0 R >>");
	strcpy(body, "<< /Type /Pages /Kids [] /Deep ");
	for (index = 0; index < 40; index++)
		strcat(body, "[");
	for (index = 0; index < 40; index++)
		strcat(body, "]");
	strcat(body, " >>");
	pdf_object(&pdf, 2, body);
	pdf_finish(&pdf, 3, "");
	expect_error(open_hand_made(&pdf, &document), PDF_EFORMAT, "nesting past the limit");

	/* A page tree deeper than the limit. */
	pdf_begin(&pdf);
	pdf_object(&pdf, 1, "<< /Type /Catalog /Pages 2 0 R >>");
	for (index = 2; index < 40; index++) {
		sprintf(body, "<< /Type /Pages /Kids [%lu 0 R] >>", (unsigned long)index + 1);
		pdf_object(&pdf, (int)index, body);
	}
	pdf_object(&pdf, 40, "<< /Type /Page /MediaBox [0 0 1 1] >>");
	pdf_finish(&pdf, 41, "");
	expect_error(open_hand_made(&pdf, &document), PDF_EFORMAT, "a page tree past the limit");

	/* No catalog. */
	pdf_begin(&pdf);
	pdf_object(&pdf, 1, "<< /Type /Catalog /Pages 2 0 R >>");
	pdf_finish(&pdf, 2, "/Root 9 0 R");
	expect_error(open_hand_made(&pdf, &document), PDF_EFORMAT, "a missing catalog");

	/* Encryption. */
	pdf_begin(&pdf);
	pdf_object(&pdf, 1, "<< /Type /Catalog /Pages 2 0 R >>");
	pdf_object(&pdf, 2, "<< /Type /Pages /Kids [] >>");
	pdf_finish(&pdf, 3, "/Encrypt << /Filter /Standard >>");
	expect_error(open_hand_made(&pdf, &document), ENOTSUP, "an encrypted document");

	/* A cross-reference stream. */
	pdf_begin(&pdf);
	pdf_object(&pdf, 1, "<< /Type /XRef /Size 2 /W [1 2 1] /Length 0 >>\nstream\n\nendstream");
	sprintf(body, "startxref\n%lu\n%%%%EOF\n", (unsigned long)pdf.offsets[1]);
	pdf_append(&pdf, body);
	expect_error(pdf_document_open_memory(pdf.data, pdf.length, &document), ENOTSUP, "a cross-reference stream");

	/* A /Prev chain that comes back to itself, and a subsection too large for the file. */
	pdf_begin(&pdf);
	pdf_object(&pdf, 1, "<< /Type /Catalog /Pages 2 0 R >>");
	pdf_object(&pdf, 2, "<< /Type /Pages /Kids [] >>");
	sprintf(body, "xref\n0 3\n0000000000 65535 f \n%010lu 00000 n \n%010lu 00000 n \ntrailer\n<< /Size 3 /Root 1 0 R /Prev %lu >>\nstartxref\n%lu\n%%%%EOF\n",
		(unsigned long)pdf.offsets[1], (unsigned long)pdf.offsets[2], (unsigned long)pdf.length, (unsigned long)pdf.length);
	pdf_append(&pdf, body);
	expect_error(pdf_document_open_memory(pdf.data, pdf.length, &document), PDF_EFORMAT, "a /Prev loop");
	pdf_begin(&pdf);
	sprintf(body, "xref\n0 8000000\n0000000000 65535 f \ntrailer\n<< /Size 3 >>\nstartxref\n9\n%%%%EOF\n");
	pdf_append(&pdf, body);
	expect_error(pdf_document_open_memory(pdf.data, pdf.length, &document), PDF_EFORMAT, "a subsection too large for the file");

	/* A filtered stream, inherited rotation and crop box, escapes and dates. */
	pdf_begin(&pdf);
	pdf_object(&pdf, 1, "<< /Type /Catalog /Pages 2 0 R /AF [7 0 R 5 0 R] >>");
	pdf_object(&pdf, 2, "<< /Type /Pages /Kids [3 0 R 8 0 R] /MediaBox [200 100 0 0] /Rotate 450 /CropBox [10 10 300 90] >>");
	pdf_object(&pdf, 3, "<< /Type /Page /Contents 4 0 R >>");
	pdf_stream(&pdf, 4, "/Filter /FlateDecode", "xx");
	pdf_object(&pdf, 5, "<< /Type /Filespec /UF (a\\(b\\)\\\\\\143\\\r\nd) /EF << /F 6 0 R >> >>");
	pdf_stream(&pdf, 6, "/Subtype /application#2fx-t#65st /Params << /Size 3 >>", "xyz");
	pdf_object(&pdf, 7, "<< /Type /Filespec /F (b.bin) /EF << /F 9 0 R >> >>");
	pdf_object(&pdf, 8, "<< /Type /Page /Rotate 45 /CropBox [500 500 600 600] >>");
	pdf_stream(&pdf, 9, "/Params << /Size 4 >>", "xyz");
	pdf_object(&pdf, 10, "<< /CreationDate (D:20260928120000+09'00') /ModDate (D:2026) >>");
	pdf_finish(&pdf, 11, "/Info 10 0 R /ID [<7A65644253442D703030342D74657374> <00>]");
	error = open_hand_made(&pdf, &document);
	expect_error(error, 0, "the document of escapes and inheritance");
	if (error == 0) {
		error = pdf_document_page_box(document, 0, &box);
		expect_error(error, 0, "the inherited boxes");
		expect(box.media_left == 0.0 && box.media_right == 200.0 && box.media_top == 100.0, "a media box given backwards");
		expect(box.crop_left == 10.0 && box.crop_right == 200.0 && box.crop_top == 90.0, "the crop box clipped");
		expect(box.rotation == 90 && box.width == 80.0 && box.height == 190.0, "a rotation of 450 is 90");
		error = pdf_document_page_box(document, 1, &box);
		expect(box.rotation == 0 && box.crop_right == 200.0, "a rotation of 45 and a crop box outside the media box");
		expect_error(pdf_document_page_content_hash(document, 0, digest), ENOTSUP, "a filtered content stream");
		error = pdf_document_find_attachment_type(document, "a(b)\\cd", "application/x-test", &data, &size);
		expect_error(error, 0, "escapes in a string and in a name");
		expect(size == 3 && memcmp(data, "xyz", 3) == 0, "the escaped attachment's bytes");
		expect_error(pdf_document_find_attachment(document, "b.bin", &data, &size), PDF_EFORMAT, "an attachment whose size disagrees");
		error = pdf_document_get_id(document, id);
		expect(error == 0 && memcmp(id, "zedBSD-p004-test", 16) == 0, "a hexadecimal identifier");
		pdf_document_get_dates(document, &creation, &modification);
		expect(creation == 1790564400, "a date nine hours ahead of universal time");
		expect(modification == 1767225600, "a date of only a year");
		pdf_document_close(document);
	}

	/* A name tree that loops. */
	pdf_begin(&pdf);
	pdf_object(&pdf, 1, "<< /Type /Catalog /Pages 2 0 R /Names << /EmbeddedFiles 3 0 R >> >>");
	pdf_object(&pdf, 2, "<< /Type /Pages /Kids [] >>");
	pdf_object(&pdf, 3, "<< /Kids [3 0 R 3 0 R 4 0 R] >>");
	pdf_object(&pdf, 4, "<< /Kids [3 0 R] /Names [(x) 5 0 R] >>");
	pdf_finish(&pdf, 5, "");
	error = open_hand_made(&pdf, &document);
	expect_error(error, 0, "a document without pages");
	if (error == 0) {
		expect(pdf_document_page_count(document) == 0, "no pages");
		expect_error(pdf_document_find_attachment(document, "y", &data, &size), ENOENT, "a name tree that loops");
		expect_error(pdf_document_find_attachment(document, "x", &data, &size), ENOENT, "a name whose value is missing");
		pdf_document_close(document);
	}
}

/* Starts a hand-made PDF with its header. */
static void
pdf_begin(
	struct test_pdf *pdf)
{
	/* Empties the document and writes the header. */
	memset(pdf, 0, sizeof(*pdf));
	pdf_append(pdf, "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
}

/* Appends text to a hand-made PDF. */
static void
pdf_append(
	struct test_pdf *pdf,
	const char *text)
{
	size_t length;

	/* Copies the text when it fits; the buffer is far larger than any test needs. */
	length = strlen(text);
	if (pdf->length + length >= TEST_PDF_MAX) {
		fprintf(stderr, "host-pdf-reader: a hand-made document is too large\n");
		exit(1);
	}
	memcpy(pdf->data + pdf->length, text, length);
	pdf->length += length;
}

/* Appends an indirect object and records its offset. */
static void
pdf_object(
	struct test_pdf *pdf,
	int number,
	const char *body)
{
	char header[32];

	/* Records the offset and writes the object. */
	pdf->offsets[number] = pdf->length;
	pdf->present[number] = 1;
	sprintf(header, "%d 0 obj\n", number);
	pdf_append(pdf, header);
	pdf_append(pdf, body);
	pdf_append(pdf, "\nendobj\n");
}

/* Appends a stream object with an exact direct length. */
static void
pdf_stream(
	struct test_pdf *pdf,
	int number,
	const char *dictionary,
	const char *data)
{
	char body[1024];

	/* Writes the dictionary, the length and the data. */
	sprintf(body, "<< %s /Length %lu >>\nstream\n%s\nendstream", dictionary, (unsigned long)strlen(data), data);
	pdf_object(pdf, number, body);
}

/* Appends the cross-reference table of the objects written, the trailer and startxref. */
static void
pdf_finish(
	struct test_pdf *pdf,
	int size,
	const char *trailer)
{
	char line[256];
	size_t table;
	int number;

	/* Writes one entry per object number below size; a missing object is free. */
	table = pdf->length;
	sprintf(line, "xref\n0 %d\n0000000000 65535 f \n", size);
	pdf_append(pdf, line);
	for (number = 1; number < size; number++) {
		if (pdf->present[number]) {
			sprintf(line, "%010lu 00000 n \n", (unsigned long)pdf->offsets[number]);
		} else {
			sprintf(line, "0000000000 00000 f \n");
		}
		pdf_append(pdf, line);
	}

	/* Writes the trailer; the caller's keys come first, so a /Root of its own wins. */
	sprintf(line, "trailer\n<< %s /Size %d /Root 1 0 R >>\nstartxref\n%lu\n%%%%EOF\n", trailer, size, (unsigned long)table);
	pdf_append(pdf, line);
}

/* Opens a hand-made PDF from memory. */
static int
open_hand_made(
	const struct test_pdf *pdf,
	struct pdf_document **document)
{
	int error;

	/* Opens the bytes. */
	error = pdf_document_open_memory(pdf->data, pdf->length, document);
	if (error != 0)
		return error;

	/* Succeeded: the caller owns the document. */
	return 0;
}

/* Makes every call on an opened document, which must not crash whatever the file held. */
static void
exercise(
	struct pdf_document *document)
{
	struct pdf_page_box box;
	unsigned char digest[32];
	unsigned char id[16];
	const void *data;
	size_t size;
	size_t index;
	size_t count;
	time_t creation;
	time_t modification;

	/* Asks about every page. */
	count = pdf_document_page_count(document);
	for (index = 0; index < count && index < 64; index++) {
		pdf_document_page_box(document, index, &box);
		pdf_document_page_content_hash(document, index, digest);
	}

	/* Asks for the attachments and the metadata. */
	pdf_document_find_attachment(document, "zedbsd-notes.bin", &data, &size);
	pdf_document_find_attachment_type(document, "a.bin", "application/x-test", &data, &size);
	pdf_document_get_id(document, id);
	pdf_document_get_dates(document, &creation, &modification);
}

/*
 * Opens every truncation of a document and a fixed number of random
 * corruptions of it, making every call on each one that opens.
 */
static void
fuzz(
	const unsigned char *file,
	size_t size,
	const char *name)
{
	struct pdf_document *document;
	unsigned char *copy;
	size_t length;
	size_t round;
	size_t changes;
	size_t change;
	size_t position;
	size_t stride;
	unsigned long kind;
	unsigned long opened;
	int error;

	/* Allocates room for the file with a few inserted bytes. */
	copy = malloc(size + 64);
	if (copy == NULL) {
		expect(0, "the fuzzer's buffer");
		return;
	}

	/* Opens truncations: every length of a small file, of the end of a large one, and a stride of the rest. */
	opened = 0;
	stride = 1 + size / 4096;
	for (length = 0; length <= size; length++) {
		if (length + 2048 < size && length % stride != 0)
			continue;
		error = pdf_document_open_memory(file, length, &document);
		if (error == 0) {
			opened++;
			exercise(document);
			pdf_document_close(document);
		}
	}
	printf("%s: truncations of %lu bytes, %lu opened\n", name, (unsigned long)size, opened);

	/* Opens random corruptions: from one to eight changes each. */
	opened = 0;
	for (round = 0; round < TEST_FUZZ_ROUNDS; round++) {
		memcpy(copy, file, size);
		length = size;
		changes = 1 + next_random() % 8;
		for (change = 0; change < changes; change++) {
			kind = next_random() % 5;
			position = next_random() % length;

			/* Aims half of the changes at the end of the file, where the cross-reference table is. */
			if (next_random() % 2 == 0 && length > 600)
				position = length - 1 - next_random() % 600;
			if (kind == 0) {
				/* Any byte. */
				copy[position] = (unsigned char)next_random();
			} else if (kind == 1 && copy[position] >= '0' && copy[position] <= '9') {
				/* Another digit, which moves an offset or a length. */
				copy[position] = (unsigned char)('0' + next_random() % 10);
			} else if (kind == 2 && length + 1 < size + 64) {
				/* An inserted byte, which shifts everything after it. */
				memmove(copy + position + 1, copy + position, length - position);
				copy[position] = (unsigned char)" 0()<>[]/\n"[next_random() % 10];
				length++;
			} else if (kind == 3 && length > 1) {
				/* A removed byte. */
				memmove(copy + position, copy + position + 1, length - position - 1);
				length--;
			} else {
				/* A delimiter, which changes how the tokens fall. */
				copy[position] = (unsigned char)"()<>[]{}/%R"[next_random() % 11];
			}
		}
		error = pdf_document_open_memory(copy, length, &document);
		if (error == 0) {
			opened++;
			exercise(document);
			pdf_document_close(document);
		}
	}
	printf("%s: %d corruptions, %lu opened\n", name, TEST_FUZZ_ROUNDS, opened);
	free(copy);
}

/* Steps the fuzzer's generator. */
static unsigned long
next_random(void)
{
	/* A 32-bit xorshift step. */
	test_random_state ^= (test_random_state << 13) & 0xffffffffUL;
	test_random_state ^= test_random_state >> 17;
	test_random_state ^= (test_random_state << 5) & 0xffffffffUL;

	/* Reports the new state. */
	return test_random_state;
}

/* Finds the last occurrence of a text in a file that may hold NULs, or NULL. */
static const unsigned char *
find_last(
	const unsigned char *file,
	size_t size,
	const char *text)
{
	size_t length;
	size_t position;

	/* Compares at each position from the end. */
	length = strlen(text);
	for (position = size - length + 1; position > 0; position--) {
		if (memcmp(file + position - 1, text, length) == 0)
			return file + position - 1;
	}

	/* The file does not hold the text. */
	return NULL;
}

/* Prints a digest in hexadecimal after a label. */
static void
print_hash(
	const char *label,
	const unsigned char digest[32])
{
	size_t index;

	/* Writes the label and the digest's bytes. */
	printf("%s sha256 ", label);
	for (index = 0; index < 32; index++)
		printf("%02x", digest[index]);
	printf("\n");
}
