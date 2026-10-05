/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The first fields of FFmpeg's AVPacket and AVFrame the decoding add-in
 * (codec.c, WS122 p004) touches, as the order of FFmpeg's public headers
 * gives them for the major versions in codec.c's add_versions.  The layout
 * is a fact of FFmpeg's public interface; no FFmpeg code is copied.
 * plan/ws122/tests/host-layout.c compiles a check of these against each
 * version's headers.
 */

#ifndef VIDEOPLAYER_CODEC_LAYOUT_H
#define VIDEOPLAYER_CODEC_LAYOUT_H

#include <stdint.h>

/* The data pointers of an AVFrame (AV_NUM_DATA_POINTERS). */
#define CODEC_DATA_POINTERS	8U

/*
 * The first fields of AVPacket, in the order of FFmpeg's public header for
 * every major version in add_versions (checked by host-layout.c).
 */
struct codec_packet {
	void *buffer;
	int64_t pts;
	int64_t dts;
	uint8_t *data;
	int size;
	int stream_index;
	int flags;
};

/*
 * The first fields of AVFrame, in the order of FFmpeg's public header for
 * every major version in add_versions (checked by host-layout.c).
 */
struct codec_frame {
	uint8_t *data[CODEC_DATA_POINTERS];
	int linesize[CODEC_DATA_POINTERS];
	uint8_t **extended_data;
	int width;
	int height;
	int nb_samples;
	int format;
};

#endif
