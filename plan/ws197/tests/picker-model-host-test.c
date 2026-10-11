/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the media picker's model (ws197-p014,
 * userland/desktop/libkeiland/ui/picker-model.c): the snapshot's items and
 * their order, malformed snapshots, the selection with its limit and
 * numbers, the answer and its kinds, cancelling, and the thumbnails' thread
 * with the shared cache (the application's decoder called once, then the
 * cache).  Run by picker-model-host-test.sh with a fresh folder.
 */

#include "userland/desktop/libkeiland/ui/picker.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* The three items' identities. */
#define ID_A "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define ID_B "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define ID_C "cccccccccccccccccccccccccccccccc"

/* How many times the decoder was called (the thread calls it; read after the thread is idle). */
static int decoded;

/* The number of failed checks. */
static int failures;

static void check(int condition, const char *what);
static void write_file(const char *path, const void *bytes, size_t size);
static int snapshot(const char *folder, const char *name, const char *text);
static int fake_decoder(void *data, const char *path, int side, struct kl_image *image);
static int wait_thumbnails(struct keiui_picker *picker);
static int has_file(const char *path);

/*
 * Runs every check in the folder given; prints PASS or FAIL last.
 */
int
main(
	int argc,
	char **argv)
{
	static const unsigned char png[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 13};
	static const unsigned char jpeg[] = {0xff, 0xd8, 0xff, 0xe0, 0, 16, 'J', 'F', 'I', 'F'};
	static const unsigned char mp4[] = {0, 0, 0, 24, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm', 0, 0, 2, 0};
	struct kl_media_picker_options options;
	struct keiui_picker picker;
	char text[4096];
	char path[2048];
	char cache[1024];
	const char *folder;
	int descriptor;
	int error;

	/* The folder, the media files and the cache under it. */
	if (argc != 2)
		return 2;
	folder = argv[1];
	(void)snprintf(cache, sizeof(cache), "%s/cache", folder);
	(void)setenv("XDG_CACHE_HOME", cache, 1);
	(void)snprintf(path, sizeof(path), "%s/a.png", folder);
	write_file(path, png, sizeof(png));
	(void)snprintf(path, sizeof(path), "%s/b.mp4", folder);
	write_file(path, mp4, sizeof(mp4));
	(void)snprintf(path, sizeof(path), "%s/c.jpg", folder);
	write_file(path, jpeg, sizeof(jpeg));

	/* A snapshot of three items (taken at 100, 300 and 200) and an album row. */
	(void)snprintf(text, sizeof(text),
		       "# keiland-media 1\t%s\n"
		       "P\t" ID_A "\t%s/a.png\th\t12\t100\t100\t1\t1\t0\t0\ta.png\n"
		       "P\t" ID_B "\t%s/b.mp4\th\t16\t300\t300\t0\t0\t0\t0\tb.mp4\n"
		       "P\t" ID_C "\t%s/c.jpg\th\t10\t200\t200\t1\t1\t0\t0\tc.jpg\n"
		       "A\tdddddddddddddddddddddddddddddddd\tTrip\n",
		       folder, folder, folder, folder);

	/* The options: two at once, the fake decoder. */
	memset(&options, 0, sizeof(options));
	options.limit = 2U;
	options.thumbnail = fake_decoder;

	/* The items load, the newest first, and the album row is not one. */
	error = keiui_picker_init(&picker, &options);
	check(error == 0, "init");
	descriptor = snapshot(folder, "good", text);
	error = keiui_picker_load(&picker, descriptor);
	check(error == 0, "load");
	check(picker.count == 3U, "three items");
	check(strcmp(picker.items[0].id, ID_B) == 0, "newest first");
	check(strcmp(picker.items[1].id, ID_C) == 0, "then the middle");
	check(strcmp(picker.items[2].id, ID_A) == 0, "oldest last");
	check(strcmp(picker.items[2].name, "a.png") == 0, "the name");

	/* The selection: numbers in order, the limit refused with a message, numbers closing up. */
	keiui_picker_toggle(&picker, 2U);
	keiui_picker_toggle(&picker, 0U);
	check(picker.items[2].order == 1U && picker.items[0].order == 2U, "selection numbers");
	keiui_picker_toggle(&picker, 1U);
	check(picker.items[1].order == 0U && picker.selected == 2U, "limit refuses a third");
	check(picker.message[0] != '\0', "limit message");
	keiui_picker_toggle(&picker, 2U);
	check(picker.items[0].order == 1U && picker.selected == 1U, "numbers close up");
	keiui_picker_toggle(&picker, 1U);
	check(picker.items[1].order == 2U, "a second again");

	/* The answer in the selection's order, with the kinds read from the files. */
	keiui_picker_accept(&picker);
	check(picker.answered && picker.result == KL_MEDIA_PICKER_CHOSEN, "answered");
	check(picker.answer_count == 2U, "two answered");
	check(strcmp(picker.answer[0].id, ID_B) == 0 && picker.answer[0].video == 1, "first is the video");
	check(strcmp(picker.answer[1].id, ID_C) == 0 && picker.answer[1].video == 0, "second is the photo");
	keiui_picker_toggle(&picker, 2U);
	check(picker.items[2].order == 0U, "nothing changes after the answer");

	/* The thread: the photos decoded once each and kept in the cache, the video an icon. */
	error = keiui_picker_start(&picker);
	check(error == 0, "start");
	keiui_picker_want(&picker, 0U, 3U);
	error = wait_thumbnails(&picker);
	check(error == 0, "thumbnails made");
	check(picker.items[0].shown == KEIUI_PICKER_THUMB_ICON && picker.items[0].video == 1, "video icon");
	check(picker.items[1].shown == KEIUI_PICKER_THUMB_READY, "jpeg thumbnail");
	check(picker.items[2].shown == KEIUI_PICKER_THUMB_READY, "png thumbnail");
	check(picker.items[1].image.width == KL_MEDIA_PICKER_THUMBNAIL, "thumbnail size");
	check(decoded == 2, "decoder called for the two photos");
	(void)snprintf(path, sizeof(path), "%s/keiland/photos/" ID_C ".ppm", cache);
	check(has_file(path), "cache written");
	keiui_picker_fini(&picker);

	/* A second picker reads the cache and calls no decoder; one choice moves, a double click answers. */
	options.limit = 0U;
	error = keiui_picker_init(&picker, &options);
	check(error == 0, "second init");
	(void)close(descriptor);
	descriptor = snapshot(folder, "good", text);
	error = keiui_picker_load(&picker, descriptor);
	check(error == 0, "second load");
	error = keiui_picker_start(&picker);
	check(error == 0, "second start");
	keiui_picker_want(&picker, 0U, 3U);
	error = wait_thumbnails(&picker);
	check(error == 0, "second thumbnails");
	check(decoded == 2, "the cache served them");
	if (picker.count != 3U) {
		printf("picker-model-host-test: FAIL (the second load)\n");
		return 1;
	}

	/* The cached square, its colour the decoder's. */
	check(picker.items[1].shown == KEIUI_PICKER_THUMB_READY, "cached thumbnail");
	check((picker.items[1].image.pixels[0] & 0x00ffffffU) == 0x00336699U, "cached colour");
	keiui_picker_toggle(&picker, 0U);
	keiui_picker_toggle(&picker, 1U);
	check(picker.items[0].order == 0U && picker.items[1].order == 1U && picker.selected == 1U, "one choice moves");
	keiui_picker_choose(&picker, 2U);
	check(picker.answered && picker.answer_count == 1U && strcmp(picker.answer[0].id, ID_A) == 0, "double click answers");
	keiui_picker_fini(&picker);
	(void)close(descriptor);

	/* Cancelling answers nothing; Add with nothing selected answers nothing. */
	error = keiui_picker_init(&picker, &options);
	check(error == 0, "third init");
	keiui_picker_accept(&picker);
	check(!picker.answered, "nothing selected, no answer");
	keiui_picker_cancel(&picker);
	check(picker.answered && picker.result == KL_MEDIA_PICKER_CANCELLED && picker.answer_count == 0U, "cancelled");
	keiui_picker_fini(&picker);

	/* A limit past the most is refused. */
	options.limit = KL_MEDIA_PICKER_LIMIT_MAX + 1U;
	error = keiui_picker_init(&picker, &options);
	check(error == EINVAL, "limit too large");
	keiui_picker_fini(&picker);
	options.limit = 0U;

	/* Malformed snapshots leave no items: no header, a bad identity, a cut row. */
	error = keiui_picker_init(&picker, &options);
	check(error == 0, "fourth init");
	descriptor = snapshot(folder, "noheader", "P\t" ID_A "\t/x\th\t1\t1\t1\t0\t0\t0\t0\tx\n");
	error = keiui_picker_load(&picker, descriptor);
	check(error == EPROTO && picker.count == 0U, "no header");
	(void)close(descriptor);
	descriptor = snapshot(folder, "badid", "# keiland-media 1\t/r\nP\tXYZ\t/x\th\t1\t1\t1\t0\t0\t0\t0\tx\n");
	error = keiui_picker_load(&picker, descriptor);
	check(error == EPROTO && picker.count == 0U, "bad identity");
	(void)close(descriptor);
	descriptor = snapshot(folder, "cut", "# keiland-media 1\t/r\nP\t" ID_A "\t/x\th\t1\t1\t1\t0\t0\t0\t0\tx");
	error = keiui_picker_load(&picker, descriptor);
	check(error == EPROTO && picker.count == 0U, "cut row");
	(void)close(descriptor);
	descriptor = snapshot(folder, "relative", "# keiland-media 1\t/r\nP\t" ID_A "\tx\th\t1\t1\t1\t0\t0\t0\t0\tx\n");
	error = keiui_picker_load(&picker, descriptor);
	check(error == EPROTO && picker.count == 0U, "relative path");
	(void)close(descriptor);
	keiui_picker_fini(&picker);

	/* The verdict. */
	if (failures != 0) {
		printf("picker-model-host-test: FAIL (%d)\n", failures);
		return 1;
	}

	/* Succeeded: every check held. */
	printf("picker-model-host-test: PASS\n");
	return 0;
}

/* Counts and reports one check. */
static void
check(
	int condition,
	const char *what)
{
	/* A failed check is told. */
	if (!condition) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* Writes a small file whole. */
static void
write_file(
	const char *path,
	const void *bytes,
	size_t size)
{
	FILE *file;

	/* The file, written and closed. */
	file = fopen(path, "wb");
	if (file == NULL) {
		check(0, path);
		return;
	}

	/* The bytes. */
	(void)fwrite(bytes, 1U, size, file);
	(void)fclose(file);
}

/* Writes a snapshot's text to a file of the folder and opens it for reading. */
static int
snapshot(
	const char *folder,
	const char *name,
	const char *text)
{
	char path[1024];
	int descriptor;

	/* The file, then a descriptor to read it. */
	(void)snprintf(path, sizeof(path), "%s/%s.snapshot", folder, name);
	write_file(path, text, strlen(text));
	descriptor = open(path, O_RDONLY);
	check(descriptor >= 0, "snapshot opens");
	return descriptor;
}

/* The application's decoder: a square of one colour at the side, counted. */
static int
fake_decoder(
	void *data,
	const char *path,
	int side,
	struct kl_image *image)
{
	size_t index;
	int error;

	/* Nothing of the data or the path is needed. */
	(void)data;
	(void)path;

	/* The square. */
	error = kl_image_create(image, side, side);
	if (error != 0)
		return error;
	for (index = 0U; index < (size_t)side * (size_t)side; index++)
		image->pixels[index] = 0xff336699U;

	/* Counted (the thread is the only caller). */
	decoded++;
	return 0;
}

/* Waits up to five seconds for every item in sight to be finished; ETIMEDOUT otherwise. */
static int
wait_thumbnails(
	struct keiui_picker *picker)
{
	struct timespec pause;
	int pending;
	int tries;

	/* Ten milliseconds at a time. */
	pause.tv_sec = 0;
	pause.tv_nsec = 10000000L;
	for (tries = 0; tries < 500; tries++) {
		pending = keiui_picker_collect(picker, 0U, picker->count);
		if (!pending)
			return 0;
		(void)nanosleep(&pause, NULL);
	}

	/* Still coming. */
	return ETIMEDOUT;
}

/* Tells whether a file is there. */
static int
has_file(
	const char *path)
{
	struct stat status;
	int found;

	/* Its status. */
	found = stat(path, &status);
	if (found != 0)
		return 0;
	return 1;
}
