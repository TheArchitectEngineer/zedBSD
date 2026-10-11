/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host tool of libmedia's AAC-LC encoder (ws205-p003), driven by
 * run-aac-host-test.sh, which decodes with FFmpeg (the independent side):
 *
 *   aac-tool encode RATE CHANNELS BITRATE IN.raw OUT.aac   an ADTS file; prints the bit rate reached
 */

#include "userland/desktop/libmedia/media-aac-encoder.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv);

/*
 * Encodes a raw 16-bit interleaved file into ADTS.
 */
int
main(
	int argc,
	char **argv)
{
	struct media_aac_encoder *encoder;
	uint8_t header[MEDIA_AAC_ADTS_BYTES];
	uint8_t unit[MEDIA_AAC_UNIT_MAX];
	int16_t pcm[MEDIA_AAC_FRAME_SAMPLES * 2U];
	unsigned rate;
	unsigned channels;
	unsigned bitrate;
	unsigned long frames;
	unsigned long long bytes;
	size_t length;
	size_t got;
	FILE *input;
	FILE *output;
	int error;
	int pass;
	int same;

	/* The arguments. */
	same = 1;
	if (argc == 7)
		same = strcmp(argv[1], "encode");
	if (same != 0) {
		fprintf(stderr, "usage: aac-tool encode RATE CHANNELS BITRATE IN.raw OUT.aac\n");
		return 2;
	}

	/* The settings and the encoder. */
	rate = (unsigned)atoi(argv[2]);
	channels = (unsigned)atoi(argv[3]);
	bitrate = (unsigned)atoi(argv[4]);
	error = media_aac_encoder_create(rate, channels, bitrate, &encoder);
	if (error != 0) {
		printf("FAIL create: %s\n", strerror(error));
		return 1;
	}

	/* The files. */
	input = fopen(argv[5], "rb");
	output = fopen(argv[6], "wb");
	if (input == NULL || output == NULL) {
		printf("FAIL files\n");
		return 1;
	}

	/* Each frame, the last one padded with silence, then one more to flush the window. */
	frames = 0UL;
	bytes = 0ULL;
	for (pass = 0; pass < 2;) {
		memset(pcm, 0, sizeof(pcm));
		got = fread(pcm, sizeof(int16_t) * channels, MEDIA_AAC_FRAME_SAMPLES, input);
		if (got == 0U)
			pass++;
		error = media_aac_encode(encoder, pcm, unit, sizeof(unit), &length);
		if (error == 0)
			error = media_aac_adts_header(encoder, length, header);
		if (error != 0) {
			printf("FAIL encode: %s\n", strerror(error));
			return 1;
		}

		/* Written as ADTS. */
		fwrite(header, 1U, sizeof(header), output);
		fwrite(unit, 1U, length, output);
		frames++;
		bytes += length;
	}

	/* The files and the encoder are no longer needed. */
	fclose(input);
	fclose(output);
	media_aac_encoder_destroy(encoder);

	/* The bit rate reached. */
	printf("encode frames=%lu kbps=%.1f\n", frames, (double)bytes * 8.0 * (double)rate / ((double)frames * 1024.0) / 1000.0);
	return 0;
}
