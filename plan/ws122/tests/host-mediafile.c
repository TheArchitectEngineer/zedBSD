/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws122-p003: dumps what the media file reader (userland/desktop/mediafile)
 * finds in a file, for run-host-mediafile.sh to compare with what the file
 * was made with (make-media.py).
 *
 *     host-mediafile FILE [SEEK_US]
 *
 * Prints the format, each track, and each packet (track, times, keyframe,
 * size, the sum of its bytes); with SEEK_US, then seeks there and prints
 * the next three packets.  A file the reader refuses prints "OPEN error=N"
 * and exits with 1.
 */

#include "userland/desktop/mediafile/mediafile.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv);
static void print_packet(const char *tag, const struct mf_packet *packet);

/*
 * Opens the file, prints what is in it, and seeks when asked.
 */
int
main(
	int argc,
	char **argv)
{
	const struct mf_track *track;
	struct mf_file *file;
	struct mf_packet packet;
	long long seek_us;
	unsigned i;
	unsigned count;
	unsigned tracks;
	int error;

	/* The file, and the time to seek to. */
	if (argc < 2) {
		fprintf(stderr, "usage: host-mediafile FILE [SEEK_US]\n");
		return 2;
	}

	/* Opens it; a refusal is printed. */
	error = mf_open(argv[1], &file);
	if (error != 0) {
		printf("OPEN error=%d\n", error);
		return 1;
	}

	/* The format, the length and each track. */
	tracks = mf_track_count(file);
	printf("FORMAT %s duration_us=%lld tracks=%u\n",
	       mf_format_name(file),
	       (long long)mf_duration_us(file),
	       tracks);
	for (i = 0; i < tracks; i++) {
		/* One track. */
		track = mf_track(file, i);
		printf("TRACK %u kind=%u codec=%u name=%s width=%u height=%u rate=%u channels=%u private=%zu packets=%llu\n",
		       i,
		       track->kind,
		       track->codec,
		       track->codec_name,
		       track->width,
		       track->height,
		       track->sample_rate,
		       track->channels,
		       track->private_size,
		       (unsigned long long)track->packet_count);
	}

	/* Each packet. */
	count = 0;
	for (;;) {
		/* The next one, or the end. */
		error = mf_read(file, &packet);
		if (error != 0)
			break;

		/* Printed and counted. */
		print_packet("PACKET", &packet);
		count++;
	}

	/* How reading ended (ENODATA at the end). */
	printf("END error=%d packets=%u\n", error, count);

	/* The seek, and the three packets after it. */
	if (argc >= 3) {
		seek_us = strtoll(argv[2], NULL, 10);
		error = mf_seek(file, (int64_t)seek_us);
		printf("SEEK %lld error=%d\n", seek_us, error);
		for (i = 0; i < 3U; i++) {
			/* One packet after the seek. */
			error = mf_read(file, &packet);
			if (error != 0)
				break;

			/* Printed. */
			print_packet("AFTER", &packet);
		}
	}

	/* Succeeded: everything is printed. */
	mf_close(file);
	return 0;
}

/*
 * Prints one packet: its track, times, keyframe flag, size and the sum of
 * its bytes.
 */
static void
print_packet(
	const char *tag,
	const struct mf_packet *packet)
{
	unsigned long sum;
	size_t i;

	/* The sum of the bytes, which tells the right ones were read. */
	sum = 0;
	for (i = 0; i < packet->size; i++)
		sum += packet->data[i];

	/* The line. */
	printf("%s track=%u pts=%lld dts=%lld key=%d size=%zu sum=%lu\n",
	       tag,
	       packet->track,
	       (long long)packet->pts_us,
	       (long long)packet->dts_us,
	       packet->keyframe,
	       packet->size,
	       sum);
}
