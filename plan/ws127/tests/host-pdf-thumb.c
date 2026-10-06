/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws127-p004: the PDF thumbnails and the trim of the thumbnail cache (files/thumb-cache.c) on the host, with a
 * host-built libpdf.so found by dlopen (host-pdf-thumb.sh):
 *  1. a real one-page PDF (made by plan/ws127/tests/make-pdf.py) gives a thumbnail whose longest side is 512;
 *  2. a PDF cut short, a PDF of only its signature, an empty file and random bytes behind the signature fail
 *     with EINVAL and do not crash;
 *  3. the cache trimmed to 3 of 5 records keeps the 3 written last.
 *   host-pdf-thumb GOOD.pdf TEMPORARY-FOLDER
 */

#include "files.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static int failures;

static void check(int condition, const char *what);
static unsigned char *read_file(const char *path, size_t *size);

int
main(
	int argc,
	char **argv)
{
	static const char signature_only[] = "%PDF-1.4\n";
	struct kl_image image;
	struct timeval times[2];
	unsigned char *data;
	unsigned char noise[4096];
	char path[1200];
	char cache[1024];
	size_t size = 0;
	size_t index;
	int error;
	int removed;
	int kept;
	int written;

	if (argc != 3) {
		fprintf(stderr, "usage: host-pdf-thumb GOOD.pdf TEMPORARY-FOLDER\n");
		return 2;
	}

	/* 1. The real PDF. */
	data = read_file(argv[1], &size);
	check(data != NULL, "the PDF is read");
	memset(&image, 0, sizeof(image));
	error = fm_thumb_pdf(data, size, &image);
	printf("good: error=%d width=%d height=%d\n", error, image.width, image.height);
	check(error == 0 && (image.width == 512 || image.height == 512), "a PDF's first page is drawn, its longest side 512");
	kl_image_release(&image);

	/* 2. Damaged documents. */
	error = fm_thumb_pdf(data, size / 3U, &image);
	check(error == EINVAL && image.pixels == NULL, "a PDF cut to a third fails without a crash");
	kl_image_release(&image);
	error = fm_thumb_pdf((const unsigned char *)signature_only, sizeof(signature_only) - 1U, &image);
	check(error == EINVAL, "a PDF of only its signature fails");
	kl_image_release(&image);
	error = fm_thumb_pdf((const unsigned char *)"", 0U, &image);
	check(error == EINVAL, "an empty file fails");
	kl_image_release(&image);
	srand(127);
	memcpy(noise, "%PDF-1.7\n", 9U);
	for (index = 9U; index < sizeof(noise); index++)
		noise[index] = (unsigned char)(rand() & 0xff);
	error = fm_thumb_pdf(noise, sizeof(noise), &image);
	check(error == EINVAL, "random bytes behind the signature fail");
	kl_image_release(&image);
	free(data);

	/* 3. The trim: five records of different ages, three kept. */
	snprintf(cache, sizeof(cache), "%s/cache", argv[2]);
	setenv("XDG_CACHE_HOME", cache, 1);
	(void)fm_thumb_cache_trim(1000000U, 1000000U);
	for (index = 0; index < 5U; index++) {
		snprintf(path, sizeof(path), "%s/keiland/thumbnails/record-%lu", cache, (unsigned long)index);
		{
			FILE *file = fopen(path, "w");
			if (file != NULL) {
				fputs("x", file);
				fclose(file);
			}
		}
		times[0].tv_sec = 1000000 + (long)index * 100;
		times[0].tv_usec = 0;
		times[1] = times[0];
		utimes(path, times);
	}
	removed = fm_thumb_cache_trim(4U, 3U);
	kept = 0;
	for (index = 0; index < 5U; index++) {
		snprintf(path, sizeof(path), "%s/keiland/thumbnails/record-%lu", cache, (unsigned long)index);
		written = access(path, F_OK) == 0;
		if (written && index >= 2U)
			kept++;
		if (written && index < 2U)
			kept = -100;
	}
	check(removed == 2 && kept == 3, "the trim removes the two oldest of five, keeping three");
	removed = fm_thumb_cache_trim(4U, 3U);
	check(removed == 0, "a cache within its bound is not trimmed");

	printf("host-pdf-thumb: %s\n", failures == 0 ? "PASS" : "FAIL");
	return failures == 0 ? 0 : 1;
}

static void
check(
	int condition,
	const char *what)
{
	printf("%s: %s\n", condition ? "ok" : "FAIL", what);
	if (!condition)
		failures++;
}

static unsigned char *
read_file(
	const char *path,
	size_t *size)
{
	unsigned char *data;
	FILE *file;
	long length;

	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)length + 1U);
	if (data != NULL)
		*size = fread(data, 1U, (size_t)length, file);
	fclose(file);
	return data;
}
