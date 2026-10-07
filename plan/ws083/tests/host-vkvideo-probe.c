/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of vkvideo-probe's stream reader and frame hash (ws083-p004).
 *
 * For each test stream: the reader must find the parameter sets and the
 * pictures the stream was made with (intra IDR pictures, the slices per
 * picture, the extent); and each frame of ffmpeg's own decode (raw NV12),
 * laid into Y tiles here as the decoder's image is laid out, must hash
 * through the probe's de-tiling to the stream's reference hash.
 *
 *   host-vkvideo-probe STREAM.h264 STREAM.nv12 STREAM.sha256 SLICES
 */

#include "../../../userland/tests/vkvideo-probe/h264.h"
#include "../../../userland/tests/vkvideo-probe/frame.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *fixture_read(const char *path, size_t *size);
static void fixture_tile(uint8_t *tiled, size_t pitch, const uint8_t *linear, uint32_t width, uint32_t rows);

/* Runs the checks of one stream. */
int
main(
	int argc,
	char **argv)
{
	static struct h264_stream stream;
	static struct h264_picture picture;
	struct frame_planes planes;
	struct frame_window window;
	const StdVideoH264SequenceParameterSet *sps;
	const char *reason;
	uint8_t *data;
	uint8_t *raw;
	uint8_t *tiled;
	size_t size;
	size_t raw_size;
	size_t frame_bytes;
	size_t pitch;
	size_t rows;
	uint32_t width;
	uint32_t height;
	uint32_t slices;
	unsigned frames;
	char expected[80];
	char text[65];
	FILE *hashes;
	int result;
	int error;

	/* The stream, ffmpeg's frames, the reference hashes, the slices a picture has. */
	assert(argc == 5);
	data = fixture_read(argv[1], &size);
	raw = fixture_read(argv[2], &raw_size);
	hashes = fopen(argv[3], "r");
	assert(hashes != NULL);
	slices = (uint32_t)strtoul(argv[4], NULL, 10);

	/* The reader finds the sets. */
	error = h264_open(&stream, data, size);
	assert(error == 0);
	assert(stream.has_sps[0] && stream.has_pps[0]);
	sps = &stream.sps[0];
	width = (sps->pic_width_in_mbs_minus1 + 1U) * 16U;
	height = (sps->pic_height_in_map_units_minus1 + 1U) * 16U;
	assert(sps->flags.frame_mbs_only_flag == 1U);
	assert(sps->flags.frame_cropping_flag == 0U);
	frame_bytes = (size_t)width * height * 3U / 2U;

	/* x264's matrix of the cqm stream is in the picture set: lists 2 and 5 left to fall back (make-streams.sh). */
	if (stream.pps[0].flags.pic_scaling_matrix_present_flag) {
		assert(stream.pps[0].flags.transform_8x8_mode_flag == 1U);
		assert(stream.pps_scaling[0].scaling_list_present_mask == 0xdbU);
		assert(stream.pps_scaling[0].use_default_scaling_matrix_mask == 0U);
		assert(sps->flags.seq_scaling_matrix_present_flag == 0U);
		printf("%s: picture scaling matrix, lists 0x%x\n", argv[1], stream.pps_scaling[0].scaling_list_present_mask);
	}
	assert(raw_size % frame_bytes == 0U);

	/* The decoder's layout: whole tiles across, whole tile rows down for each plane. */
	pitch = (width + 127U) & ~127U;
	rows = (height + 31U) & ~31U;
	tiled = calloc(1U, pitch * (rows + ((height / 2U + 31U) & ~31U)));
	assert(tiled != NULL);

	/* Every picture: intra, IDR, the slices it was made with, its frame hashing to the reference. */
	frames = 0U;
	for (;;) {
		result = h264_next_picture(&stream, &picture, &reason);
		if (result < 0)
			fprintf(stderr, "picture %u: %s\n", frames, reason);
		assert(result >= 0);
		if (result == 0)
			break;
		assert(picture.intra == 1 && picture.info.flags.is_intra == 1U);
		assert(picture.info.flags.IdrPicFlag == 1U && picture.info.flags.is_reference == 1U);
		assert(picture.info.frame_num == 0U);
		assert(picture.info.PicOrderCnt[0] == 0 && picture.info.PicOrderCnt[1] == 0);
		assert(picture.slice_count == slices);
		assert((size_t)(frames + 1U) * frame_bytes <= raw_size);

		/* ffmpeg's frame laid into Y tiles: the Y plane, then the CbCr plane below it. */
		fixture_tile(tiled, pitch, raw + frames * frame_bytes, width, height);
		fixture_tile(tiled + pitch * rows, pitch, raw + frames * frame_bytes + (size_t)width * height, width, height / 2U);
		planes.luma = tiled;
		planes.luma_pitch = pitch;
		planes.chroma = tiled + pitch * rows;
		planes.chroma_pitch = pitch;
		window.x = 0U;
		window.y = 0U;
		window.width = width;
		window.height = height;
		frame_hash(&planes, &window, text);

		/* The reference hash of the frame. */
		assert(fgets(expected, sizeof(expected), hashes) != NULL);
		expected[strcspn(expected, "\n")] = '\0';
		if (strcmp(expected, text) != 0)
			fprintf(stderr, "frame %u: %s, expected %s\n", frames, text, expected);
		assert(strcmp(expected, text) == 0);
		frames++;
	}

	/* Every frame was a picture, and the other way round. */
	assert((size_t)frames * frame_bytes == raw_size);
	assert(fgets(expected, sizeof(expected), hashes) == NULL);
	printf("%s: %u pictures of %ux%u, %u slices each, hashes match\n", argv[1], frames, width, height, slices);
	fclose(hashes);
	free(tiled);
	free(raw);
	free(data);
	return 0;
}

/* Reads a whole file. */
static uint8_t *
fixture_read(
	const char *path,
	size_t *size)
{
	FILE *file;
	uint8_t *data;
	long length;

	/* Its length, then its bytes. */
	file = fopen(path, "rb");
	assert(file != NULL);
	fseek(file, 0L, SEEK_END);
	length = ftell(file);
	assert(length > 0);
	fseek(file, 0L, SEEK_SET);
	data = malloc((size_t)length);
	assert(data != NULL);
	assert(fread(data, 1U, (size_t)length, file) == (size_t)length);
	fclose(file);
	*size = (size_t)length;
	return data;
}

/*
 * Lays linear rows into Y tiles, written out tile by tile here (not by the
 * probe's offset function): for each tile, its eight 16-byte columns, for
 * each column its 32 rows.
 */
static void
fixture_tile(
	uint8_t *tiled,
	size_t pitch,
	const uint8_t *linear,
	uint32_t width,
	uint32_t rows)
{
	uint32_t tile_row;
	uint32_t tile_column;
	uint32_t column;
	uint32_t row;
	uint32_t byte;
	uint32_t x;
	uint32_t y;
	uint8_t *out;

	/* The tiles in order, the bytes of each in order. */
	out = tiled;
	for (tile_row = 0U; tile_row < (rows + 31U) / 32U; tile_row++) {
		for (tile_column = 0U; tile_column < pitch / 128U; tile_column++) {
			for (column = 0U; column < 8U; column++) {
				for (row = 0U; row < 32U; row++) {
					for (byte = 0U; byte < 16U; byte++) {
						x = tile_column * 128U + column * 16U + byte;
						y = tile_row * 32U + row;
						*out = 0xeeU;
						if (x < width && y < rows)
							*out = linear[(size_t)y * width + x];
						out++;
					}
				}
			}
		}
	}
}
