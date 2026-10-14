/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws236-p011: the UIF layout of one-, two- and four-byte texels and the R8
 * and RG8 texture records (native-texture.c), on the host.
 *
 * Checks that the generic texel offset gives the four-byte layout the
 * existing copy writes, that the generic copy puts every texel at its
 * offset with zero padding, and that the R8, RG8 and RGBA8 records carry
 * their native type, size and swizzles.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/gpu/bcm2711/native-state.h"

/* The shapes the copies are checked with: partial columns and block rows, padded pitches. */
struct shape {
	unsigned width;
	unsigned height;
	unsigned pitch_extra;
};

static int failures;

static void check(int condition, const char *what);
static unsigned read_bits(const unsigned char *bytes, unsigned start, unsigned count);
static void check_rgba_offsets(void);
static void check_copy(unsigned texel_bytes, const struct shape *shape);
static void check_record(enum bcm2711_native_texel texel, unsigned width, unsigned height, unsigned type, unsigned green, unsigned blue, unsigned alpha);

/* Runs every check and reports one PASS or FAIL line. */
int
main(
	void)
{
	static const struct shape shapes[] = {
		{1, 1, 0},
		{33, 9, 4},
		{96, 40, 0},
		{130, 70, 60},
		{960, 540, 64}
	};
	unsigned index;
	unsigned texel;

	/* The four-byte layout is the one the existing copy writes. */
	check_rgba_offsets();

	/* The generic copy for every texel size and shape. */
	for (texel = 1; texel <= 4; texel *= 2) {
		for (index = 0; index < sizeof(shapes) / sizeof(shapes[0]); index++)
			check_copy(texel, &shapes[index]);
	}

	/* The records: R8 reads (r, 0, 0, 1), RG8 (r, g, 0, 1), RGBA8 all four. */
	check_record(BCM2711_NATIVE_TEXEL_R8, 1920, 1080, 0, 0, 0, 1);
	check_record(BCM2711_NATIVE_TEXEL_RG8, 960, 540, 2, 3, 0, 1);
	check_record(BCM2711_NATIVE_TEXEL_RGBA8, 37, 131, 4, 3, 4, 5);

	/* One line for the runner. */
	if (failures != 0) {
		printf("ws236 p011 texture planes: FAIL (%d)\n", failures);
		return 1;
	}
	printf("ws236 p011 texture planes: PASS\n");
	return 0;
}

/* Counts and names a failed check. */
static void
check(
	int condition,
	const char *what)
{
	/* A failed check is named once. */
	if (!condition) {
		failures++;
		printf("FAIL: %s\n", what);
	}
}

/* Reads one little-endian bit field of a record. */
static unsigned
read_bits(
	const unsigned char *bytes,
	unsigned start,
	unsigned count)
{
	unsigned value;
	unsigned bit;

	/* Bit by bit, as the encoder writes them. */
	value = 0;
	for (bit = 0; bit < count; bit++) {
		if ((bytes[(start + bit) / 8U] >> ((start + bit) % 8U)) & 1U)
			value |= 1U << bit;
	}
	return value;
}

/* Compares the generic four-byte offsets with the existing whole-image copy. */
static void
check_rgba_offsets(
	void)
{
	unsigned width;
	unsigned height;
	unsigned x;
	unsigned y;
	uint32_t bytes;
	uint32_t offset;
	uint32_t *raster;
	unsigned char *tiled;
	uint32_t word;
	int error;

	/* A raster whose every pixel names itself. */
	width = 70;
	height = 37;
	raster = calloc((size_t)width * height, 4);
	error = bcm2711_native_texture_size(width, height, &bytes);
	check(error == 0, "rgba size");
	tiled = calloc(bytes, 1);
	if (raster == NULL || tiled == NULL) {
		check(0, "rgba allocation");
		free(raster);
		free(tiled);
		return;
	}
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++)
			raster[y * width + x] = 0x80000000U | (x << 12) | y;
	}

	/* The existing copy, then every pixel at its generic offset. */
	error = bcm2711_native_texture_copy(width, height, raster, width * 4U, (size_t)width * height * 4U, tiled, bytes);
	check(error == 0, "rgba copy");
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			error = bcm2711_native_texture_offset(x, y, height, 4, &offset);
			check(error == 0, "rgba offset");
			memcpy(&word, tiled + offset, 4);
			if (word != raster[y * width + x]) {
				check(0, "rgba offset matches the existing copy");
				y = height;
				break;
			}
		}
	}
	free(raster);
	free(tiled);
}

/* Copies one raster of texel_bytes texels and checks every texel and the padding. */
static void
check_copy(
	unsigned texel_bytes,
	const struct shape *shape)
{
	unsigned char *raster;
	unsigned char *tiled;
	unsigned char *placed;
	uint32_t bytes;
	uint32_t offset;
	unsigned pitch;
	unsigned x;
	unsigned y;
	unsigned channel;
	unsigned index;
	char what[96];
	int error;

	/* A raster whose bytes depend on texel and channel, with padded rows. */
	pitch = shape->width * texel_bytes + shape->pitch_extra;
	raster = malloc((size_t)pitch * shape->height);
	error = bcm2711_native_texture_size_texel(shape->width, shape->height, texel_bytes, &bytes);
	snprintf(what, sizeof(what), "size %ux%u bpp %u", shape->width, shape->height, texel_bytes);
	check(error == 0, what);
	tiled = malloc(bytes);
	placed = calloc(bytes, 1);
	if (raster == NULL || tiled == NULL || placed == NULL) {
		check(0, "copy allocation");
		free(raster);
		free(tiled);
		free(placed);
		return;
	}
	memset(raster, 0xee, (size_t)pitch * shape->height);
	memset(tiled, 0x5a, bytes);
	for (y = 0; y < shape->height; y++) {
		for (x = 0; x < shape->width * texel_bytes; x++)
			raster[(size_t)y * pitch + x] = (unsigned char)((x / texel_bytes) * 7U + y * 13U + (x % texel_bytes) * 101U + 1U);
	}

	/* The copy, then every visible texel at its offset. */
	error = bcm2711_native_texture_copy_texel(shape->width, shape->height, texel_bytes, raster, pitch, (size_t)pitch * shape->height, tiled, bytes);
	snprintf(what, sizeof(what), "copy %ux%u bpp %u", shape->width, shape->height, texel_bytes);
	check(error == 0, what);
	for (y = 0; y < shape->height; y++) {
		for (x = 0; x < shape->width; x++) {
			error = bcm2711_native_texture_offset(x, y, shape->height, texel_bytes, &offset);
			if (error != 0 || offset + texel_bytes > bytes) {
				check(0, "texel offset inside the image");
				continue;
			}
			for (channel = 0; channel < texel_bytes; channel++) {
				placed[offset + channel] = 1;
				if (tiled[offset + channel] != raster[(size_t)y * pitch + x * texel_bytes + channel]) {
					snprintf(what, sizeof(what), "texel %u,%u of %ux%u bpp %u", x, y, shape->width, shape->height, texel_bytes);
					check(0, what);
				}
			}
		}
	}

	/* Every byte no texel has is zero padding. */
	for (index = 0; index < bytes; index++) {
		if (placed[index] == 0 && tiled[index] != 0) {
			snprintf(what, sizeof(what), "padding of %ux%u bpp %u", shape->width, shape->height, texel_bytes);
			check(0, what);
			break;
		}
	}
	free(raster);
	free(tiled);
	free(placed);
}

/* Encodes one record and checks its type, layer stride, extent and swizzles. */
static void
check_record(
	enum bcm2711_native_texel texel,
	unsigned width,
	unsigned height,
	unsigned type,
	unsigned green,
	unsigned blue,
	unsigned alpha)
{
	struct bcm2711_native_texture texture;
	unsigned char record[BCM2711_NATIVE_TEXTURE_BYTES];
	uint32_t texel_bytes;
	uint32_t native_type;
	uint32_t bytes;
	int error;

	/* The record of the texel's whole UIF image. */
	error = bcm2711_native_texel_layout(texel, &texel_bytes, &native_type);
	check(error == 0, "texel layout");
	error = bcm2711_native_texture_size_texel(width, height, texel_bytes, &bytes);
	check(error == 0, "record size");
	memset(&texture, 0, sizeof(texture));
	texture.address = 0x100000;
	texture.width = width;
	texture.height = height;
	texture.bytes = bytes;
	texture.texel = texel;
	error = bcm2711_native_texture_encode(&texture, record, sizeof(record));
	check(error == 0, "record encode");

	/* The fields the sampler reads. */
	check(read_bits(record, 0, 32) == 0x100000U, "record address");
	check(read_bits(record, 32, 26) == bytes / 64U, "record layer stride");
	check(read_bits(record, 58, 14) == width, "record width");
	check(read_bits(record, 72, 14) == height, "record height");
	check(read_bits(record, 100, 7) == type, "record type");
	check(read_bits(record, 108, 3) == 2U, "record red swizzle");
	check(read_bits(record, 111, 3) == green, "record green swizzle");
	check(read_bits(record, 114, 3) == blue, "record blue swizzle");
	check(read_bits(record, 117, 3) == alpha, "record alpha swizzle");

	/* A swap of red and blue is RGBA8's only. */
	if (texel != BCM2711_NATIVE_TEXEL_RGBA8) {
		texture.swap_red_blue = 1;
		error = bcm2711_native_texture_encode(&texture, record, sizeof(record));
		check(error != 0, "swap refused for a plane");
	}
}
