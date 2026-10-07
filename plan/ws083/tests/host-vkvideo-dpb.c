/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of vkvideo-probe's DPB and output order (ws083-p006a).
 *
 * A stream's pictures go through the probe's reader and DPB as the probe
 * decodes them, without a decoder: each decode's plan must be one the
 * executor takes (references distinct, active, at most max_num_ref_frames,
 * the written slot not among them, a P or B picture with a reference) and
 * the marking must follow.  The pictures put in order of their order counts
 * (between IDR pictures, as the probe prints them) must be ffmpeg's display
 * order: the access units' positions in the stream, compared with the
 * positions ffprobe lists for the frames in display order.
 *
 *   host-vkvideo-dpb STREAM.h264 POSITIONS.txt
 */

#include "../../../userland/tests/vkvideo-probe/h264.h"
#include "../../../userland/tests/vkvideo-probe/dpb.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The most pictures a test stream has. */
#define FIXTURE_PICTURES	256U

/* One decoded picture waiting for its place in the output: its order count and its access unit. */
struct fixture_output {
	int32_t poc;
	size_t access_unit;
};

static uint8_t *fixture_read(const char *path, size_t *size);
static int fixture_compare(const void *left, const void *right);
static void fixture_flush(struct fixture_output *pending, unsigned *pending_count, size_t *order, unsigned *ordered);

/* Runs the checks of one stream. */
int
main(
	int argc,
	char **argv)
{
	static struct h264_stream stream;
	static struct h264_picture picture;
	static struct dpb dpb;
	static struct fixture_output pending[FIXTURE_PICTURES];
	static size_t order[FIXTURE_PICTURES];
	struct dpb_plan plan;
	const StdVideoH264SequenceParameterSet *sps;
	const char *reason;
	uint8_t *data;
	size_t size;
	unsigned pending_count;
	unsigned ordered;
	unsigned pictures;
	unsigned references_seen;
	unsigned index;
	unsigned other;
	unsigned long position;
	FILE *positions;
	int result;
	int error;

	/* The stream and the display-order positions. */
	assert(argc == 3);
	data = fixture_read(argv[1], &size);
	error = h264_open(&stream, data, size);
	assert(error == 0);
	sps = &stream.sps[0];
	assert(stream.has_sps[0]);
	dpb_init(&dpb, sps->max_num_ref_frames);

	/* Every picture planned and marked as the probe does it. */
	pending_count = 0U;
	ordered = 0U;
	pictures = 0U;
	references_seen = 0U;
	for (;;) {
		result = h264_next_picture(&stream, &picture, &reason);
		if (result < 0)
			fprintf(stderr, "picture %u: %s\n", pictures, reason);
		assert(result >= 0);
		if (result == 0)
			break;

		/* An IDR picture outputs every picture before it. */
		if (picture.info.flags.IdrPicFlag)
			fixture_flush(pending, &pending_count, order, &ordered);

		/* The plan. */
		reason = dpb_plan(&dpb, sps, &picture, &plan);
		if (reason != NULL)
			fprintf(stderr, "picture %u plan: %s\n", pictures, reason);
		assert(reason == NULL);
		assert(plan.reset == (pictures == 0U));
		assert(plan.setup >= 0 && (uint32_t)plan.setup < dpb.slots);
		assert(plan.reference_count <= sps->max_num_ref_frames);
		if (picture.info.flags.IdrPicFlag)
			assert(plan.reference_count == 0U);
		if (!picture.intra)
			assert(plan.reference_count >= 1U);
		for (index = 0U; index < plan.reference_count; index++) {
			assert(plan.references[index] != plan.setup);
			assert(dpb.entry[plan.references[index]].reference != DPB_UNUSED);
			assert(dpb.entry[plan.references[index]].device_active);
			for (other = 0U; other < index; other++)
				assert(plan.references[other] != plan.references[index]);
		}
		for (index = 0U; index < plan.deactivate_count; index++) {
			assert(dpb.entry[plan.deactivate[index]].device_active);
			assert(dpb.entry[plan.deactivate[index]].reference == DPB_UNUSED);
		}
		references_seen += plan.reference_count;

		/* The marking after the decode. */
		reason = dpb_mark(&dpb, sps, &picture, &plan);
		if (reason != NULL)
			fprintf(stderr, "picture %u mark: %s\n", pictures, reason);
		assert(reason == NULL);

		/* The picture waits for its place in the output. */
		assert(pending_count < FIXTURE_PICTURES);
		pending[pending_count].poc = picture.info.PicOrderCnt[0];
		pending[pending_count].access_unit = picture.access_unit;
		pending_count++;
		pictures++;
	}
	fixture_flush(pending, &pending_count, order, &ordered);

	/* The output order is ffmpeg's display order. */
	positions = fopen(argv[2], "r");
	assert(positions != NULL);
	for (index = 0U; index < ordered; index++) {
		assert(fscanf(positions, "%lu", &position) == 1);
		if ((size_t)position != order[index])
			fprintf(stderr, "frame %u: access unit at %zu, ffmpeg's at %lu\n", index, order[index], position);
		assert((size_t)position == order[index]);
	}
	assert(fscanf(positions, "%lu", &position) != 1);
	fclose(positions);
	printf("%s: %u pictures, %u references handed over, display order matches ffmpeg's\n", argv[1], pictures, references_seen);
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

/* Orders two waiting pictures by their order counts. */
static int
fixture_compare(
	const void *left,
	const void *right)
{
	const struct fixture_output *first;
	const struct fixture_output *second;

	/* The smaller order count first. */
	first = left;
	second = right;
	if (first->poc < second->poc)
		return -1;
	if (first->poc > second->poc)
		return 1;
	return 0;
}

/* Outputs the waiting pictures in order of their order counts. */
static void
fixture_flush(
	struct fixture_output *pending,
	unsigned *pending_count,
	size_t *order,
	unsigned *ordered)
{
	unsigned index;

	/* Sorted, then appended. */
	qsort(pending, *pending_count, sizeof(pending[0]), fixture_compare);
	for (index = 0U; index < *pending_count; index++) {
		order[*ordered] = pending[index].access_unit;
		*ordered = *ordered + 1U;
	}
	*pending_count = 0U;
}
