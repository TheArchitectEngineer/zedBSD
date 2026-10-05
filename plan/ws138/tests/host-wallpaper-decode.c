/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the wallpaper decoding (ws138-p001, ws138-p002,
 * userland/desktop/picture/wallpaper.c).
 *
 *   host-wallpaper-decode FILE          prints "ok WIDTHxHEIGHT r,g,b r,g,b" (the first two pixels) or "error E"
 *   host-wallpaper-decode same A B      prints "same" when both decode to the same pixels, else "differ"
 *   host-wallpaper-decode near A B N    prints "near" when the mean absolute difference is at most N, else "far M"
 *   host-wallpaper-decode grey FILE     prints "grey" when every pixel has equal red, green and blue
 *
 * A file whose name ends in ".rgb" is a reference, not decoded: a line
 * "WIDTH HEIGHT" and then the RGB pixels (the script writes them from
 * Python's own PNG reading, ppm-to-png.py's read_png).
 *
 * run-host-wallpaper-decode.sh makes the files and checks the lines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wallpaper.h"

static int decode_file(const char *path, struct kl_wallpaper_image *image);
static int reference_take(unsigned char *data, size_t size, struct kl_wallpaper_image *image);
static int show(const char *path);
static int same(const char *left, const char *right);
static int near(const char *left, const char *right, double limit);
static int grey(const char *path);

/*
 * Runs the mode the arguments name.
 */
int
main(
	int argc,
	char **argv)
{
	int compared;

	/* One file: its size and first pixels. */
	if (argc == 2)
		return show(argv[1]);

	/* Two files compared exactly. */
	compared = 1;
	if (argc == 4)
		compared = strcmp(argv[1], "same");
	if (compared == 0)
		return same(argv[2], argv[3]);

	/* Two files compared within a mean difference. */
	compared = 1;
	if (argc == 5)
		compared = strcmp(argv[1], "near");
	if (compared == 0)
		return near(argv[2], argv[3], atof(argv[4]));

	/* One file checked for grey. */
	compared = 1;
	if (argc == 3)
		compared = strcmp(argv[1], "grey");
	if (compared == 0)
		return grey(argv[2]);

	/* Anything else. */
	fprintf(stderr, "usage: host-wallpaper-decode FILE | same A B | near A B N | grey FILE\n");
	return 2;
}

/* Reads a file and decodes it; returns the decoding's error, or the reading's. */
static int
decode_file(
	const char *path,
	struct kl_wallpaper_image *image)
{
	unsigned char *data;
	FILE *stream;
	size_t count;
	size_t length;
	long size;
	int reference;
	int error;

	/* Reads the whole file. */
	memset(image, 0, sizeof(*image));
	stream = fopen(path, "rb");
	if (stream == NULL)
		return 2;
	(void)fseek(stream, 0, SEEK_END);
	size = ftell(stream);
	(void)fseek(stream, 0, SEEK_SET);
	data = malloc((size_t)size + 1U);
	if (data == NULL) {
		fclose(stream);
		return 12;
	}

	/* Its bytes. */
	count = fread(data, 1U, (size_t)size, stream);
	fclose(stream);

	/* A short read is a failure. */
	if (count != (size_t)size) {
		free(data);
		return 5;
	}

	/* A reference keeps its bytes as the pixels. */
	length = strlen(path);
	reference = 1;
	if (length > 4U)
		reference = strcmp(path + length - 4U, ".rgb");
	if (reference == 0)
		return reference_take(data, (size_t)size, image);

	/* Decodes it; the bytes stay ours. */
	error = kl_wallpaper_decode(data, (size_t)size, image);
	free(data);
	return error;
}

/* Takes a reference's pixels after its "WIDTH HEIGHT" line; frees the bytes on a failure. */
static int
reference_take(
	unsigned char *data,
	size_t size,
	struct kl_wallpaper_image *image)
{
	unsigned width;
	unsigned height;
	size_t at;
	int fields;

	/* The line with the size. */
	data[size] = '\0';
	fields = sscanf((const char *)data, "%u %u", &width, &height);
	at = 0;
	while (at < size && data[at] != '\n')
		at++;
	at++;

	/* The pixels must be all there. */
	if (fields != 2 || at > size || size - at != (size_t)width * height * 3U) {
		free(data);
		return 22;
	}

	/* Moves the pixels to the start of the buffer, which becomes the image's. */
	memmove(data, data + at, size - at);
	image->rgb = data;
	image->width = width;
	image->height = height;
	return 0;
}

/* Prints a file's size and first two pixels, or its error. */
static int
show(
	const char *path)
{
	struct kl_wallpaper_image image;
	const unsigned char *p;
	int error;

	/* Decodes it. */
	error = decode_file(path, &image);
	if (error != 0) {
		printf("error %d\n", error);
		return 0;
	}

	/* The size and the first pixels. */
	p = image.rgb;
	if ((unsigned long)image.width * image.height >= 2UL)
		printf("ok %ux%u %u,%u,%u %u,%u,%u\n", image.width, image.height, p[0], p[1], p[2], p[3], p[4], p[5]);
	else
		printf("ok %ux%u %u,%u,%u\n", image.width, image.height, p[0], p[1], p[2]);
	free(image.rgb);
	return 0;
}

/* Prints whether two files decode to the same pixels. */
static int
same(
	const char *left,
	const char *right)
{
	struct kl_wallpaper_image a;
	struct kl_wallpaper_image b;
	int differs;
	int error;

	/* Decodes both. */
	error = decode_file(left, &a);
	if (error == 0)
		error = decode_file(right, &b);
	if (error != 0) {
		printf("error %d\n", error);
		return 0;
	}

	/* The sizes and the pixels. */
	differs = 1;
	if (a.width == b.width && a.height == b.height)
		differs = memcmp(a.rgb, b.rgb, (size_t)a.width * a.height * 3U);
	if (differs == 0)
		printf("same\n");
	else
		printf("differ\n");
	free(a.rgb);
	free(b.rgb);
	return 0;
}

/* Prints whether two files decode to pixels within a mean absolute difference. */
static int
near(
	const char *left,
	const char *right,
	double limit)
{
	struct kl_wallpaper_image a;
	struct kl_wallpaper_image b;
	unsigned long long total;
	size_t count;
	size_t index;
	double mean;
	int error;

	/* Decodes both. */
	error = decode_file(left, &a);
	if (error == 0)
		error = decode_file(right, &b);
	if (error != 0) {
		printf("error %d\n", error);
		return 0;
	}

	/* Different sizes are far. */
	if (a.width != b.width || a.height != b.height) {
		printf("far size\n");
		return 0;
	}

	/* The mean absolute difference of every sample. */
	count = (size_t)a.width * a.height * 3U;
	total = 0;
	for (index = 0; index < count; index++)
		total += (unsigned long long)abs((int)a.rgb[index] - (int)b.rgb[index]);
	mean = (double)total / (double)count;
	if (mean <= limit)
		printf("near\n");
	else
		printf("far %.2f\n", mean);
	free(a.rgb);
	free(b.rgb);
	return 0;
}

/* Prints whether every pixel of a file is grey. */
static int
grey(
	const char *path)
{
	struct kl_wallpaper_image image;
	size_t count;
	size_t index;
	int error;

	/* Decodes it. */
	error = decode_file(path, &image);
	if (error != 0) {
		printf("error %d\n", error);
		return 0;
	}

	/* Each pixel's three samples. */
	count = (size_t)image.width * image.height;
	for (index = 0; index < count; index++) {
		if (image.rgb[index * 3U] != image.rgb[index * 3U + 1U] || image.rgb[index * 3U] != image.rgb[index * 3U + 2U]) {
			printf("not grey\n");
			free(image.rgb);
			return 0;
		}
	}

	/* Every pixel is grey. */
	printf("grey\n");
	free(image.rgb);
	return 0;
}
