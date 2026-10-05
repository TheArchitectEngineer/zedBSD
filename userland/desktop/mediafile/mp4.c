/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The MP4 and MOV reader (ISO base media file format, WS122 p003).  The
 * movie box (moov) is read whole; each track's sample table (stsd, stts,
 * ctts, stsc, stsz or stz2, stco or co64, stss) becomes one list of its
 * samples with their offsets, sizes, times and sync flags, and packets are
 * handed out in the order of their offsets in the file.  An edit list's
 * first edit shifts the presentation times (the delay B-frames bring, or
 * an empty edit at the start).  Fragmented files (moof) are not read.
 */

#include "mediafile-private.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* The largest movie box read into memory. */
#define MP4_MOOV_MAX		(64U * 1024U * 1024U)

/* The most samples a track may have (a day of 120 fps video is about 10 million). */
#define MP4_SAMPLES_MAX		(16U * 1024U * 1024U)

/* An esds's descriptor tags. */
#define MP4_TAG_ES		3U
#define MP4_TAG_CONFIG		4U
#define MP4_TAG_SPECIFIC	5U

/* One box inside a buffer: its type and its payload (after the header). */
struct mp4_box {
	char type[4];
	const unsigned char *payload;
	size_t size;
};

/*
 * One sample of a track: where its bytes are, how many, its decoding time
 * and the offset of its presentation time (both in the track's timescale),
 * and whether a decoder can start at it.
 */
struct mp4_sample {
	uint64_t offset;
	int64_t dts;
	uint32_t size;
	int32_t cts;
	unsigned key;
};

/*
 * One track's samples, the next one to hand out, its timescale and how far
 * its presentation times are moved (the edit list: subtracted from each).
 */
struct mp4_track {
	struct mp4_sample *samples;
	uint64_t count;
	uint64_t next;
	uint32_t timescale;
	int64_t shift;
};

/* The reader's state: the movie box and each track's samples. */
struct mp4_state {
	unsigned char *moov;
	size_t moov_size;
	uint32_t movie_timescale;
	struct mp4_track tracks[MF_TRACK_MAX];
};

/* The tables of one track's sample table box, found before they are combined. */
struct mp4_tables {
	struct mp4_box stts;
	struct mp4_box ctts;
	struct mp4_box stsc;
	struct mp4_box stsz;
	struct mp4_box stco;
	struct mp4_box stss;
	unsigned has_ctts;
	unsigned has_stss;
	unsigned compact_sizes;
	unsigned wide_offsets;
};

static int mp4_open(struct mf_file *file);
static int mp4_read(struct mf_file *file, struct mf_packet *packet);
static int mp4_seek(struct mf_file *file, int64_t time_us);
static void mp4_close(struct mf_file *file);
static int find_moov(struct mf_file *file, uint64_t *offset, uint64_t *size);
static int box_next(const unsigned char *data, size_t size, size_t *offset, struct mp4_box *box);
static int box_find(const unsigned char *data, size_t size, const char *type, struct mp4_box *box);
static int read_movie_header(struct mp4_state *state, struct mf_file *file);
static int read_track(struct mf_file *file, struct mp4_state *state, const struct mp4_box *trak);
static int read_media_header(const struct mp4_box *mdia, struct mp4_track *track, struct mf_track *info);
static int read_sample_entry(const struct mp4_box *stbl, struct mf_track *info);
static int read_visual_entry(const struct mp4_box *entry, struct mf_track *info);
static int read_audio_entry(const struct mp4_box *entry, struct mf_track *info);
static int read_esds(const struct mp4_box *esds, struct mf_track *info);
static int descriptor_next(const unsigned char *data, size_t size, size_t *offset, unsigned *tag, size_t *length);
static void codec_of(struct mf_track *info, const char *type);
static int is_configuration(const char *type);
static void read_edit_list(const struct mp4_box *trak, uint32_t movie_timescale, struct mp4_track *track);
static int find_tables(const struct mp4_box *stbl, struct mp4_tables *tables);
static int build_samples(const struct mp4_tables *tables, struct mp4_track *track);
static int fill_sizes(const struct mp4_tables *tables, struct mp4_track *track);
static int fill_offsets(const struct mp4_tables *tables, struct mp4_track *track);
static int fill_times(const struct mp4_tables *tables, struct mp4_track *track);
static int fill_sync(const struct mp4_tables *tables, struct mp4_track *track);
static int64_t sample_pts_us(const struct mp4_track *track, const struct mp4_sample *sample);
static int64_t sample_dts_us(const struct mp4_track *track, const struct mp4_sample *sample);

/* A sample entry's type and the codec it carries. */
struct mp4_entry_codec {
	const char *type;
	unsigned codec;
};

/* The sample entry types this reader knows, by codec. */
static const struct mp4_entry_codec entry_codecs[] = {
	{ "avc1", MF_CODEC_H264 },
	{ "avc3", MF_CODEC_H264 },
	{ "hvc1", MF_CODEC_HEVC },
	{ "hev1", MF_CODEC_HEVC },
	{ "av01", MF_CODEC_AV1 },
	{ "vp09", MF_CODEC_VP9 },
	{ "vp08", MF_CODEC_VP8 },
	{ "mp4v", MF_CODEC_MPEG4 },
	{ "mp4a", MF_CODEC_AAC },
	{ "Opus", MF_CODEC_OPUS },
	{ ".mp3", MF_CODEC_MP3 },
};

/* The reader as mediafile.c calls it. */
const struct mf_format mf_mp4_format = {
	"mp4",
	mp4_open,
	mp4_read,
	mp4_seek,
	mp4_close,
};

/*
 * Reads the movie box and every track's sample table.
 */
static int
mp4_open(
	struct mf_file *file)
{
	struct mp4_state *state;
	struct mp4_box trak;
	uint64_t offset;
	uint64_t size;
	size_t cursor;
	int compared;
	int error;

	/* The state. */
	state = calloc(1, sizeof(*state));
	if (state == NULL)
		return ENOMEM;

	/* Kept in the file, so that mf_close frees it on any failure. */
	file->state = state;

	/* Where the movie box is. */
	error = find_moov(file, &offset, &size);
	if (error != 0)
		return error;

	/* Refuses a movie box too large to be real. */
	if (size > MP4_MOOV_MAX)
		return EINVAL;

	/* The movie box, whole. */
	state->moov_size = (size_t)size;
	state->moov = malloc(state->moov_size);
	if (state->moov == NULL)
		return ENOMEM;

	/* Its bytes. */
	error = mf_read_at(file, offset, state->moov, state->moov_size);
	if (error != 0)
		return error;

	/* The movie's timescale and length. */
	error = read_movie_header(state, file);
	if (error != 0)
		return error;

	/* Each track. */
	cursor = 0;
	for (;;) {
		/* The next box of the movie. */
		error = box_next(state->moov, state->moov_size, &cursor, &trak);
		if (error == ENODATA)
			break;

		/* Refuses a damaged movie box. */
		if (error != 0)
			return error;

		/* Only the tracks, and no more than there is room for. */
		compared = memcmp(trak.type, "trak", 4);
		if (compared != 0 || file->track_count == MF_TRACK_MAX)
			continue;

		/* The track's media and samples. */
		error = read_track(file, state, &trak);
		if (error != 0)
			return error;
	}

	/* Refuses a movie without a track. */
	if (file->track_count == 0)
		return EINVAL;

	/* Succeeded: the tracks are read. */
	return 0;
}

/*
 * Hands out the sample whose bytes come first in the file among the next
 * sample of each track.
 */
static int
mp4_read(
	struct mf_file *file,
	struct mf_packet *packet)
{
	struct mp4_state *state;
	struct mp4_track *track;
	struct mp4_sample *sample;
	struct mp4_sample *best;
	unsigned best_track;
	unsigned i;
	int error;

	/* The earliest next sample in the file. */
	state = file->state;
	best = NULL;
	best_track = 0;
	for (i = 0; i < file->track_count; i++) {
		/* A track with samples left. */
		track = &state->tracks[i];
		if (track->next >= track->count)
			continue;

		/* Kept when it comes before the best so far. */
		sample = &track->samples[track->next];
		if (best == NULL || sample->offset < best->offset) {
			best = sample;
			best_track = i;
		}
	}

	/* Every track is at its end. */
	if (best == NULL)
		return ENODATA;

	/* Its bytes. */
	error = mf_packet_room(file, best->size);
	if (error != 0)
		return error;

	/* Read from the file. */
	error = mf_read_at(file, best->offset, file->buffer, best->size);
	if (error != 0)
		return error;

	/* The packet, and the track moves on. */
	track = &state->tracks[best_track];
	packet->track = best_track;
	packet->pts_us = sample_pts_us(track, best);
	packet->dts_us = sample_dts_us(track, best);
	packet->keyframe = (int)best->key;
	packet->data = file->buffer;
	packet->size = best->size;
	track->next++;

	/* Succeeded: one packet. */
	return 0;
}

/*
 * Moves the first video track to its last sync sample at or before the
 * time, and every other track to its last sample that decodes no later
 * than that sync sample.
 */
static int
mp4_seek(
	struct mf_file *file,
	int64_t time_us)
{
	struct mp4_state *state;
	struct mp4_track *track;
	struct mp4_track *lead;
	unsigned lead_index;
	uint64_t i;
	uint64_t chosen;
	int64_t start_us;
	int64_t pts_us;
	int64_t dts_us;
	unsigned t;

	/* The track that decides: the first video track, else the first. */
	state = file->state;
	lead_index = 0;
	for (t = 0; t < file->track_count; t++) {
		/* The first video track with samples. */
		if (file->tracks[t].kind == MF_TRACK_VIDEO && state->tracks[t].count != 0) {
			lead_index = t;
			break;
		}
	}

	/* Its last sync sample presented at or before the time (the first one when none is). */
	lead = &state->tracks[lead_index];
	chosen = 0;
	for (i = 0; i < lead->count; i++) {
		/* A sync sample not after the time. */
		if (!lead->samples[i].key)
			continue;

		/* Kept while it is not after the time. */
		pts_us = sample_pts_us(lead, &lead->samples[i]);
		if (pts_us <= time_us)
			chosen = i;
	}

	/* The lead starts there; the others decode from the same time. */
	lead->next = chosen;
	start_us = 0;
	if (lead->count != 0)
		start_us = sample_dts_us(lead, &lead->samples[chosen]);

	/* Each other track: its last sample decoding at or before that time. */
	for (t = 0; t < file->track_count; t++) {
		/* Not the lead. */
		if (t == lead_index)
			continue;

		/* The last sample not after the start (the first when none is). */
		track = &state->tracks[t];
		track->next = 0;
		for (i = 0; i < track->count; i++) {
			/* Stops at the first sample past the start. */
			dts_us = sample_dts_us(track, &track->samples[i]);
			if (dts_us > start_us)
				break;

			/* Kept while it is not past the start. */
			track->next = i;
		}
	}

	/* Succeeded: the tracks are moved. */
	return 0;
}

/*
 * Lets the reader's state go.
 */
static void
mp4_close(
	struct mf_file *file)
{
	struct mp4_state *state;
	unsigned i;

	/* Each track's samples, the movie box and the state. */
	state = file->state;
	for (i = 0; i < MF_TRACK_MAX; i++)
		free(state->tracks[i].samples);

	/* The movie box and the state. */
	free(state->moov);
	free(state);
	file->state = NULL;
}

/*
 * Finds the movie box among the file's top-level boxes.  Returns 0 with its
 * payload's offset and size, or EINVAL when there is none.
 */
static int
find_moov(
	struct mf_file *file,
	uint64_t *offset,
	uint64_t *size)
{
	unsigned char header[16];
	uint64_t position;
	uint64_t box_size;
	size_t header_size;
	int compared;
	int error;

	/* Each top-level box in turn. */
	position = 0;
	while (position + 8U <= file->size) {
		/* Its header (16 bytes when the file has them, for a 64-bit size). */
		header_size = 8U;
		error = mf_read_at(file, position, header, 8U);
		if (error != 0)
			return error;

		/* The size: 32-bit, 64-bit after the type (1), or to the end of the file (0). */
		box_size = mf_be32(header);
		if (box_size == 1U) {
			error = mf_read_at(file, position + 8U, header + 8, 8U);
			if (error != 0)
				return error;

			/* The 64-bit size. */
			box_size = mf_be64(header + 8);
			header_size = 16U;
		} else if (box_size == 0U) {
			box_size = file->size - position;
		}

		/* Refuses a box smaller than its header or past the end. */
		if (box_size < header_size || box_size > file->size - position)
			return EINVAL;

		/* The movie box. */
		compared = memcmp(header + 4, "moov", 4);
		if (compared == 0) {
			*offset = position + header_size;
			*size = box_size - header_size;
			return 0;
		}

		/* The next box. */
		position += box_size;
	}

	/* No movie box: not a playable MP4 (or a fragmented one without it). */
	return EINVAL;
}

/*
 * Reads the box at *offset of a buffer and moves *offset past it.  Returns
 * 0, ENODATA at the end of the buffer, or EINVAL for a box that does not
 * fit in it.
 */
static int
box_next(
	const unsigned char *data,
	size_t size,
	size_t *offset,
	struct mp4_box *box)
{
	uint64_t box_size;
	size_t header_size;
	size_t left;

	/* The end of the buffer (trailing bytes too few for a header are ignored). */
	left = size - *offset;
	if (left < 8U)
		return ENODATA;

	/* The size: 32-bit, 64-bit (1) or to the end of the buffer (0). */
	header_size = 8U;
	box_size = mf_be32(data + *offset);
	if (box_size == 1U) {
		/* A 64-bit size needs its 8 bytes. */
		if (left < 16U)
			return EINVAL;

		/* The 64-bit size. */
		box_size = mf_be64(data + *offset + 8);
		header_size = 16U;
	} else if (box_size == 0U) {
		box_size = left;
	}

	/* Refuses a box smaller than its header or larger than what is left. */
	if (box_size < header_size || box_size > left)
		return EINVAL;

	/* The box. */
	memcpy(box->type, data + *offset + 4, 4);
	box->payload = data + *offset + header_size;
	box->size = (size_t)box_size - header_size;
	*offset += (size_t)box_size;

	/* Succeeded: one box. */
	return 0;
}

/*
 * Finds the first child box of a type in a payload.  Returns 0, ENODATA
 * when there is none, or EINVAL for a damaged payload.
 */
static int
box_find(
	const unsigned char *data,
	size_t size,
	const char *type,
	struct mp4_box *box)
{
	size_t offset;
	int compared;
	int error;

	/* Each child in turn. */
	offset = 0;
	for (;;) {
		/* The next child. */
		error = box_next(data, size, &offset, box);
		if (error != 0)
			return error;

		/* The type wanted. */
		compared = memcmp(box->type, type, 4);
		if (compared == 0)
			return 0;
	}
}

/*
 * Reads the movie header: the movie's timescale (for the edit lists) and
 * the presentation's length.
 */
static int
read_movie_header(
	struct mp4_state *state,
	struct mf_file *file)
{
	struct mp4_box mvhd;
	uint64_t duration;
	int error;

	/* The movie header. */
	error = box_find(state->moov, state->moov_size, "mvhd", &mvhd);
	if (error != 0)
		return EINVAL;

	/* Version 1 has 64-bit times; version 0 32-bit ones. */
	if (mvhd.size >= 32U && mvhd.payload[0] == 1U) {
		state->movie_timescale = mf_be32(mvhd.payload + 20);
		duration = mf_be64(mvhd.payload + 24);
	} else if (mvhd.size >= 20U) {
		state->movie_timescale = mf_be32(mvhd.payload + 12);
		duration = mf_be32(mvhd.payload + 16);
	} else {
		return EINVAL;
	}

	/* The length in microseconds (an unknown length is all ones). */
	if (duration != UINT64_MAX && duration != UINT32_MAX && duration <= (uint64_t)INT64_MAX)
		file->duration_us = mf_scale_us((int64_t)duration, state->movie_timescale);

	/* Succeeded: the header is read. */
	return 0;
}

/*
 * Reads one track: its media header, handler, sample description, edit
 * list and sample table.  A track this reader cannot use (no sample table,
 * no samples) is left out.
 */
static int
read_track(
	struct mf_file *file,
	struct mp4_state *state,
	const struct mp4_box *trak)
{
	struct mp4_track *track;
	struct mf_track *info;
	struct mp4_tables tables;
	struct mp4_box mdia;
	struct mp4_box minf;
	struct mp4_box stbl;
	int error;

	/* The slot of the next track, emptied of a track left out before. */
	track = &state->tracks[file->track_count];
	info = &file->tracks[file->track_count];
	free((void *)info->private_data);
	free(track->samples);
	memset(track, 0, sizeof(*track));
	memset(info, 0, sizeof(*info));

	/* The media box; a track without one is left out. */
	error = box_find(trak->payload, trak->size, "mdia", &mdia);
	if (error == ENODATA)
		return 0;

	/* Refuses a damaged media box. */
	if (error != 0)
		return error;

	/* Its timescale, length and kind. */
	error = read_media_header(&mdia, track, info);
	if (error != 0)
		return error;

	/* The media information and its sample table. */
	error = box_find(mdia.payload, mdia.size, "minf", &minf);
	if (error == 0)
		error = box_find(minf.payload, minf.size, "stbl", &stbl);

	/* A track without a sample table is left out. */
	if (error == ENODATA)
		return 0;

	/* Refuses a damaged media information box. */
	if (error != 0)
		return error;

	/* The codec and its private data. */
	error = read_sample_entry(&stbl, info);
	if (error != 0)
		return error;

	/* The tables, then the samples they describe. */
	error = find_tables(&stbl, &tables);
	if (error == ENODATA)
		return 0;

	/* Refuses a damaged sample table. */
	if (error != 0)
		return error;

	/* The samples. */
	error = build_samples(&tables, track);
	if (error != 0)
		return error;

	/* The edit list's shift, and the track is counted. */
	read_edit_list(trak, state->movie_timescale, track);
	info->packet_count = track->count;
	file->track_count++;

	/* Succeeded: the track is read. */
	return 0;
}

/*
 * Reads a media box's header (timescale, length) and handler (the kind of
 * track).
 */
static int
read_media_header(
	const struct mp4_box *mdia,
	struct mp4_track *track,
	struct mf_track *info)
{
	struct mp4_box mdhd;
	struct mp4_box hdlr;
	uint64_t duration;
	int compared;
	int error;

	/* The media header. */
	error = box_find(mdia->payload, mdia->size, "mdhd", &mdhd);
	if (error != 0)
		return EINVAL;

	/* Version 1 has 64-bit times; version 0 32-bit ones. */
	if (mdhd.size >= 32U && mdhd.payload[0] == 1U) {
		track->timescale = mf_be32(mdhd.payload + 20);
		duration = mf_be64(mdhd.payload + 24);
	} else if (mdhd.size >= 20U) {
		track->timescale = mf_be32(mdhd.payload + 12);
		duration = mf_be32(mdhd.payload + 16);
	} else {
		return EINVAL;
	}

	/* Refuses a track without a timescale. */
	if (track->timescale == 0)
		return EINVAL;

	/* The length, when it is known. */
	if (duration != UINT64_MAX && duration != UINT32_MAX && duration <= (uint64_t)INT64_MAX)
		info->duration_us = mf_scale_us((int64_t)duration, track->timescale);

	/* The handler: video, sound or something else. */
	error = box_find(mdia->payload, mdia->size, "hdlr", &hdlr);
	if (error != 0 || hdlr.size < 12U)
		return 0;

	/* A video track. */
	compared = memcmp(hdlr.payload + 8, "vide", 4);
	if (compared == 0)
		info->kind = MF_TRACK_VIDEO;

	/* A sound track. */
	compared = memcmp(hdlr.payload + 8, "soun", 4);
	if (compared == 0)
		info->kind = MF_TRACK_AUDIO;

	/* Succeeded: the header is read. */
	return 0;
}

/*
 * Reads the first entry of the sample description: the codec, the picture's
 * size or the sound's format, and the codec's private data.
 */
static int
read_sample_entry(
	const struct mp4_box *stbl,
	struct mf_track *info)
{
	struct mp4_box stsd;
	struct mp4_box entry;
	size_t offset;
	int error;

	/* The sample description, version and count first. */
	error = box_find(stbl->payload, stbl->size, "stsd", &stsd);
	if (error != 0 || stsd.size < 8U)
		return 0;

	/* Its first entry. */
	offset = 8U;
	error = box_next(stsd.payload, stsd.size, &offset, &entry);
	if (error != 0)
		return 0;

	/* The codec from the entry's type. */
	mf_set_codec_name(info, entry.type, 4);
	codec_of(info, entry.type);

	/* A video entry's size and configuration. */
	if (info->kind == MF_TRACK_VIDEO) {
		error = read_visual_entry(&entry, info);
		return error;
	}

	/* A sound entry's format and configuration. */
	if (info->kind == MF_TRACK_AUDIO) {
		error = read_audio_entry(&entry, info);
		return error;
	}

	/* Succeeded: another kind of track keeps only its codec's name. */
	return 0;
}

/*
 * Reads a visual sample entry: the picture's size, then its child boxes
 * (avcC, hvcC, av1C, vpcC or esds) for the codec's private data.
 */
static int
read_visual_entry(
	const struct mp4_box *entry,
	struct mf_track *info)
{
	struct mp4_box child;
	size_t offset;
	int configuration;
	int compared;
	int error;

	/* The fixed part: 8 bytes of the sample entry and 70 of the visual one. */
	if (entry->size < 78U)
		return 0;

	/* The picture's size, in the fixed part. */
	info->width = mf_be16(entry->payload + 24);
	info->height = mf_be16(entry->payload + 26);

	/* The child boxes after the fixed part. */
	offset = 78U;
	for (;;) {
		/* The next child; the end or a damaged one ends the search. */
		error = box_next(entry->payload, entry->size, &offset, &child);
		if (error != 0)
			return 0;

		/* An MPEG-4 Part 2 stream keeps its configuration in an esds. */
		compared = memcmp(child.type, "esds", 4);
		if (compared == 0) {
			error = read_esds(&child, info);
			return error;
		}

		/* The configuration boxes of H.264, H.265, AV1 and VP9 are kept whole. */
		configuration = is_configuration(child.type);
		if (configuration) {
			error = mf_keep_private(info, child.payload, child.size);
			return error;
		}
	}
}

/*
 * Reads a sound sample entry (versions 0, 1 and 2 of QuickTime's layout):
 * the channels and rate, then its child boxes (esds, dOps) for the codec's
 * private data.
 */
static int
read_audio_entry(
	const struct mp4_box *entry,
	struct mf_track *info)
{
	struct mp4_box child;
	uint64_t bits;
	double rate;
	size_t offset;
	unsigned version;
	int compared;
	int error;

	/* The fixed part of version 0: 8 bytes of the sample entry and 20 of the sound one. */
	if (entry->size < 28U)
		return 0;

	/* The channels and the rate (16.16), and where the child boxes start. */
	version = mf_be16(entry->payload + 8);
	info->channels = mf_be16(entry->payload + 16);
	info->sample_rate = mf_be32(entry->payload + 24) >> 16;
	offset = 28U;
	if (version == 1U)
		offset = 44U;

	/* Version 2 holds the rate as a double and the channels after it. */
	if (version == 2U && entry->size >= 64U) {
		bits = mf_be64(entry->payload + 32);
		memcpy(&rate, &bits, sizeof(rate));
		info->sample_rate = 0;
		if (rate > 0.0 && rate < 1000000.0)
			info->sample_rate = (uint32_t)rate;

		/* The channels, after the rate. */
		info->channels = mf_be32(entry->payload + 40);
		offset = 64U;
	}

	/* The child boxes. */
	for (;;) {
		/* The next child; the end or a damaged one ends the search. */
		error = box_next(entry->payload, entry->size, &offset, &child);
		if (error != 0)
			return 0;

		/* AAC and MP3 keep their configuration in an esds. */
		compared = memcmp(child.type, "esds", 4);
		if (compared == 0) {
			error = read_esds(&child, info);
			return error;
		}

		/* Opus keeps its header in dOps, kept whole. */
		compared = memcmp(child.type, "dOps", 4);
		if (compared == 0) {
			error = mf_keep_private(info, child.payload, child.size);
			return error;
		}
	}
}

/*
 * Reads an esds box: the object type in the decoder configuration (which
 * tells AAC, MP3 and MPEG-4 video apart) and the decoder-specific
 * information, kept as the private data.
 */
static int
read_esds(
	const struct mp4_box *esds,
	struct mf_track *info)
{
	const unsigned char *data;
	size_t size;
	size_t offset;
	size_t length;
	unsigned tag;
	unsigned flags;
	unsigned object;
	int error;

	/* After the version and flags, the ES descriptor. */
	if (esds->size < 4U)
		return 0;

	/* The descriptors after the version and flags. */
	data = esds->payload + 4;
	size = esds->size - 4U;
	offset = 0;
	error = descriptor_next(data, size, &offset, &tag, &length);
	if (error != 0 || tag != MP4_TAG_ES || length < 3U)
		return 0;

	/* Its fixed part (ES_ID and flags) and the optional fields the flags announce. */
	flags = data[offset + 2];
	offset += 3U;
	if ((flags & 0x80U) != 0U)
		offset += 2U;

	/* A URL, its length first. */
	if ((flags & 0x40U) != 0U && offset < size)
		offset += 1U + data[offset];

	/* An OCR stream's ID. */
	if ((flags & 0x20U) != 0U)
		offset += 2U;

	/* The decoder configuration descriptor. */
	if (offset > size)
		return 0;

	/* The decoder configuration descriptor. */
	error = descriptor_next(data, size, &offset, &tag, &length);
	if (error != 0 || tag != MP4_TAG_CONFIG || length < 13U)
		return 0;

	/* The object type: AAC, MP3 or MPEG-4 video. */
	object = data[offset];
	if (object == 0x40U || object == 0x66U || object == 0x67U || object == 0x68U)
		info->codec = MF_CODEC_AAC;
	else if (object == 0x69U || object == 0x6bU)
		info->codec = MF_CODEC_MP3;
	else if (object == 0x20U)
		info->codec = MF_CODEC_MPEG4;

	/* The decoder-specific information after the configuration's 13 bytes. */
	offset += 13U;
	error = descriptor_next(data, size, &offset, &tag, &length);
	if (error != 0 || tag != MP4_TAG_SPECIFIC)
		return 0;

	/* Kept as the track's private data. */
	error = mf_keep_private(info, data + offset, length);
	return error;
}

/*
 * Reads a descriptor's tag and length (up to four 7-bit groups) at
 * *offset, and moves *offset to its contents.  Returns 0, or EINVAL when
 * it does not fit.
 */
static int
descriptor_next(
	const unsigned char *data,
	size_t size,
	size_t *offset,
	unsigned *tag,
	size_t *length)
{
	unsigned i;
	unsigned byte;

	/* The tag. */
	if (*offset >= size)
		return EINVAL;

	/* The tag, and the length follows it. */
	*tag = data[*offset];
	(*offset)++;

	/* The length, seven bits a byte while the top bit is set. */
	*length = 0;
	for (i = 0; i < 4U; i++) {
		/* One more byte of the length. */
		if (*offset >= size)
			return EINVAL;

		/* The byte, and the length grows by its seven bits. */
		byte = data[*offset];
		(*offset)++;
		*length = (*length << 7) | (byte & 0x7fU);
		if ((byte & 0x80U) == 0U)
			break;
	}

	/* Refuses contents past the end. */
	if (*length > size - *offset)
		return EINVAL;

	/* Succeeded: *offset is at the contents. */
	return 0;
}

/*
 * Tells the codec from a sample entry's type (an esds can refine it).
 */
static void
codec_of(
	struct mf_track *info,
	const char *type)
{
	size_t i;
	int compared;

	/* The sample entry types of each codec. */
	for (i = 0; i < sizeof(entry_codecs) / sizeof(entry_codecs[0]); i++) {
		/* The type's codec. */
		compared = memcmp(type, entry_codecs[i].type, 4);
		if (compared == 0) {
			info->codec = entry_codecs[i].codec;
			return;
		}
	}
}

/*
 * Says whether a child box of a visual sample entry is a codec's
 * configuration kept whole (avcC, hvcC, av1C, vpcC).
 */
static int
is_configuration(
	const char *type)
{
	static const char *const boxes[] = { "avcC", "hvcC", "av1C", "vpcC" };
	size_t i;
	int compared;

	/* Each configuration box's type. */
	for (i = 0; i < sizeof(boxes) / sizeof(boxes[0]); i++) {
		/* The type is one of them. */
		compared = memcmp(type, boxes[i], 4);
		if (compared == 0)
			return 1;
	}

	/* Another box. */
	return 0;
}

/*
 * Reads the first edit of a track's edit list: an empty edit delays the
 * track (by its length in the movie's timescale), and the media time of the
 * first real edit is where the presentation starts (B-frames' delay).
 */
static void
read_edit_list(
	const struct mp4_box *trak,
	uint32_t movie_timescale,
	struct mp4_track *track)
{
	struct mp4_box edts;
	struct mp4_box elst;
	const unsigned char *entry;
	uint32_t count;
	uint32_t i;
	uint64_t segment;
	int64_t media_time;
	size_t entry_size;
	int64_t delay_us;
	int error;

	/* The edit box and its list. */
	error = box_find(trak->payload, trak->size, "edts", &edts);
	if (error == 0)
		error = box_find(edts.payload, edts.size, "elst", &elst);

	/* No list (or a damaged one): no shift. */
	if (error != 0 || elst.size < 8U)
		return;

	/* Its entries, 12 bytes each in version 0 and 20 in version 1. */
	entry_size = 12U;
	if (elst.payload[0] == 1U)
		entry_size = 20U;

	/* The count of entries. */
	count = mf_be32(elst.payload + 4);
	delay_us = 0;
	for (i = 0; i < count && 8U + ((size_t)i + 1U) * entry_size <= elst.size; i++) {
		/* The segment's length (movie timescale) and the media time it starts at. */
		entry = elst.payload + 8U + (size_t)i * entry_size;
		if (entry_size == 20U) {
			segment = mf_be64(entry);
			media_time = (int64_t)mf_be64(entry + 8);
		} else {
			segment = mf_be32(entry);
			media_time = (int32_t)mf_be32(entry + 4);
		}

		/* An empty edit delays what follows. */
		if (media_time == -1) {
			delay_us += mf_scale_us((int64_t)(segment & 0x7fffffffffffffffULL), movie_timescale);
			continue;
		}

		/* The first real edit: the presentation starts at its media time, after the delay. */
		track->shift = media_time - (delay_us * (int64_t)track->timescale) / 1000000;
		return;
	}
}

/*
 * Finds the tables of a sample table box.  Returns 0, ENODATA when the
 * required ones (sizes, chunk offsets, sample-to-chunk, times) are not all
 * there, or EINVAL.
 */
static int
find_tables(
	const struct mp4_box *stbl,
	struct mp4_tables *tables)
{
	int error;

	/* The times, the chunks and the sizes are required. */
	memset(tables, 0, sizeof(*tables));
	error = box_find(stbl->payload, stbl->size, "stts", &tables->stts);
	if (error != 0)
		return error;

	/* The sample-to-chunk table. */
	error = box_find(stbl->payload, stbl->size, "stsc", &tables->stsc);
	if (error != 0)
		return error;

	/* The sizes: stsz, or the compact stz2. */
	error = box_find(stbl->payload, stbl->size, "stsz", &tables->stsz);
	if (error == ENODATA) {
		error = box_find(stbl->payload, stbl->size, "stz2", &tables->stsz);
		tables->compact_sizes = 1U;
	}

	/* Neither size table. */
	if (error != 0)
		return error;

	/* The chunk offsets: stco, or the 64-bit co64. */
	error = box_find(stbl->payload, stbl->size, "stco", &tables->stco);
	if (error == ENODATA) {
		error = box_find(stbl->payload, stbl->size, "co64", &tables->stco);
		tables->wide_offsets = 1U;
	}

	/* Neither offset table. */
	if (error != 0)
		return error;

	/* The optional composition offsets and sync samples. */
	error = box_find(stbl->payload, stbl->size, "ctts", &tables->ctts);
	if (error == 0)
		tables->has_ctts = 1U;

	/* No sync table means every sample is one. */
	error = box_find(stbl->payload, stbl->size, "stss", &tables->stss);
	if (error == 0)
		tables->has_stss = 1U;

	/* Succeeded: the tables are found. */
	return 0;
}

/*
 * Turns a track's tables into its list of samples.
 */
static int
build_samples(
	const struct mp4_tables *tables,
	struct mp4_track *track)
{
	int error;

	/* The count and sizes first, which make the list. */
	error = fill_sizes(tables, track);
	if (error != 0)
		return error;

	/* Where each sample is. */
	error = fill_offsets(tables, track);
	if (error != 0)
		return error;

	/* When each is decoded and presented. */
	error = fill_times(tables, track);
	if (error != 0)
		return error;

	/* Where a decoder can start. */
	error = fill_sync(tables, track);
	if (error != 0)
		return error;

	/* Succeeded: the list is complete. */
	return 0;
}

/*
 * Reads the sample count and sizes (stsz, or stz2 with 4, 8 or 16 bits a
 * size) and makes the track's list.
 */
static int
fill_sizes(
	const struct mp4_tables *tables,
	struct mp4_track *track)
{
	const unsigned char *data;
	uint32_t fixed;
	uint32_t count;
	uint32_t i;
	unsigned field;
	uint64_t needed;

	/* The header: version and flags, then the fixed size (or the field width) and the count. */
	data = tables->stsz.payload;
	if (tables->stsz.size < 12U)
		return EINVAL;

	/* The fixed size, else the compact table's field width. */
	fixed = mf_be32(data + 4);
	field = 32U;
	if (tables->compact_sizes) {
		fixed = 0;
		field = data[7];
	}

	/* Refuses a count too large, or a compact width that is not 4, 8 or 16. */
	count = mf_be32(data + 8);
	if (count > MP4_SAMPLES_MAX || (tables->compact_sizes && field != 4U && field != 8U && field != 16U))
		return EINVAL;

	/* A track without samples is left with an empty list. */
	if (count == 0)
		return 0;

	/* The bytes the table needs: the header, and the sizes unless they are all the same. */
	needed = 12U;
	if (fixed == 0)
		needed += ((uint64_t)count * field + 7U) / 8U;

	/* Refuses a table shorter than its count says. */
	if (needed > tables->stsz.size)
		return EINVAL;

	/* The list. */
	track->samples = calloc(count, sizeof(*track->samples));
	if (track->samples == NULL)
		return ENOMEM;

	/* The count of samples listed. */
	track->count = count;

	/* Each size: the fixed one, or its entry of the field's width. */
	for (i = 0; i < count; i++) {
		/* The fixed size. */
		if (fixed != 0) {
			track->samples[i].size = fixed;
			continue;
		}

		/* An entry of 32, 16, 8 or 4 bits. */
		if (field == 32U)
			track->samples[i].size = mf_be32(data + 12 + (size_t)i * 4U);
		else if (field == 16U)
			track->samples[i].size = mf_be16(data + 12 + (size_t)i * 2U);
		else if (field == 8U)
			track->samples[i].size = data[12 + i];
		else if (i % 2U == 0U)
			track->samples[i].size = (uint32_t)(data[12 + i / 2U] >> 4) & 0x0fU;
		else
			track->samples[i].size = (uint32_t)data[12 + i / 2U] & 0x0fU;
	}

	/* Succeeded: the sizes are in. */
	return 0;
}

/*
 * Places the samples in their chunks: the sample-to-chunk table says how
 * many samples each run of chunks holds, and the samples of a chunk follow
 * each other from the chunk's offset.
 */
static int
fill_offsets(
	const struct mp4_tables *tables,
	struct mp4_track *track)
{
	const unsigned char *stsc;
	const unsigned char *stco;
	uint32_t entries;
	uint32_t chunks;
	uint32_t entry;
	uint32_t first;
	uint32_t last;
	uint32_t per_chunk;
	uint32_t chunk;
	uint32_t i;
	uint64_t sample;
	uint64_t offset;
	size_t width;

	/* The tables' counts, and refuses tables shorter than them. */
	stsc = tables->stsc.payload;
	stco = tables->stco.payload;
	if (tables->stsc.size < 8U || tables->stco.size < 8U)
		return EINVAL;

	/* The counts of runs and chunks, and the offsets' width. */
	entries = mf_be32(stsc + 4);
	chunks = mf_be32(stco + 4);
	width = 4U;
	if (tables->wide_offsets)
		width = 8U;

	/* The tables hold what their counts say. */
	if ((uint64_t)entries * 12U > tables->stsc.size - 8U || (uint64_t)chunks * width > tables->stco.size - 8U)
		return EINVAL;

	/* Each run of chunks, each chunk of the run, each sample of the chunk. */
	sample = 0;
	for (entry = 0; entry < entries && sample < track->count; entry++) {
		/* The run's first chunk (1-based), its samples a chunk, and its last chunk. */
		first = mf_be32(stsc + 8 + (size_t)entry * 12U);
		per_chunk = mf_be32(stsc + 12 + (size_t)entry * 12U);
		last = chunks;
		if (entry + 1U < entries)
			last = mf_be32(stsc + 8 + ((size_t)entry + 1U) * 12U) - 1U;

		/* Refuses a run that starts at chunk 0 or past the chunks. */
		if (first == 0 || first > chunks || last > chunks)
			return EINVAL;

		/* Each chunk of the run. */
		for (chunk = first; chunk <= last && sample < track->count; chunk++) {
			/* The chunk's offset. */
			if (width == 8U)
				offset = mf_be64(stco + 8 + ((size_t)chunk - 1U) * 8U);
			else
				offset = mf_be32(stco + 8 + ((size_t)chunk - 1U) * 4U);

			/* Its samples, one after another. */
			for (i = 0; i < per_chunk && sample < track->count; i++) {
				/* Where this sample is, and the next starts after it. */
				track->samples[sample].offset = offset;
				offset += track->samples[sample].size;
				sample++;
			}
		}
	}

	/* Refuses a table that places fewer samples than there are. */
	if (sample != track->count)
		return EINVAL;

	/* Succeeded: every sample has its offset. */
	return 0;
}

/*
 * Gives each sample its decoding time (the running sum of the durations in
 * stts) and its composition offset (ctts, signed).
 */
static int
fill_times(
	const struct mp4_tables *tables,
	struct mp4_track *track)
{
	const unsigned char *data;
	uint32_t entries;
	uint32_t entry;
	uint32_t run;
	uint32_t i;
	uint32_t delta;
	int32_t cts;
	uint64_t sample;
	int64_t time;

	/* The time-to-sample table; refuses one shorter than its count. */
	data = tables->stts.payload;
	if (tables->stts.size < 8U)
		return EINVAL;

	/* The count of runs. */
	entries = mf_be32(data + 4);
	if ((uint64_t)entries * 8U > tables->stts.size - 8U)
		return EINVAL;

	/* Each run of samples with the same duration. */
	sample = 0;
	time = 0;
	for (entry = 0; entry < entries && sample < track->count; entry++) {
		/* The run's length and duration. */
		run = mf_be32(data + 8 + (size_t)entry * 8U);
		delta = mf_be32(data + 12 + (size_t)entry * 8U);
		for (i = 0; i < run && sample < track->count; i++) {
			/* This sample decodes at the running time. */
			track->samples[sample].dts = time;
			time += delta;
			sample++;
		}
	}

	/* No composition offsets: presentation is decoding order. */
	if (!tables->has_ctts)
		return 0;

	/* The composition table; refuses one shorter than its count. */
	data = tables->ctts.payload;
	if (tables->ctts.size < 8U)
		return EINVAL;

	/* The count of runs. */
	entries = mf_be32(data + 4);
	if ((uint64_t)entries * 8U > tables->ctts.size - 8U)
		return EINVAL;

	/* Each run of samples with the same offset (read as signed, as writers use both versions so). */
	sample = 0;
	for (entry = 0; entry < entries && sample < track->count; entry++) {
		/* The run's length and offset. */
		run = mf_be32(data + 8 + (size_t)entry * 8U);
		cts = (int32_t)mf_be32(data + 12 + (size_t)entry * 8U);
		for (i = 0; i < run && sample < track->count; i++) {
			/* This sample's offset. */
			track->samples[sample].cts = cts;
			sample++;
		}
	}

	/* Succeeded: every sample has its times. */
	return 0;
}

/*
 * Marks the sync samples: those stss names (1-based), or every sample when
 * the track has no stss.
 */
static int
fill_sync(
	const struct mp4_tables *tables,
	struct mp4_track *track)
{
	const unsigned char *data;
	uint32_t entries;
	uint32_t entry;
	uint32_t number;
	uint64_t i;

	/* No sync table: every sample is one. */
	if (!tables->has_stss) {
		for (i = 0; i < track->count; i++)
			track->samples[i].key = 1U;

		/* Succeeded: every sample is a sync sample. */
		return 0;
	}

	/* The sync table; refuses one shorter than its count. */
	data = tables->stss.payload;
	if (tables->stss.size < 8U)
		return EINVAL;

	/* The count of sync samples. */
	entries = mf_be32(data + 4);
	if ((uint64_t)entries * 4U > tables->stss.size - 8U)
		return EINVAL;

	/* Each named sample (numbers past the last are ignored). */
	for (entry = 0; entry < entries; entry++) {
		/* The sample's number, from 1. */
		number = mf_be32(data + 8 + (size_t)entry * 4U);
		if (number >= 1U && number <= track->count)
			track->samples[number - 1U].key = 1U;
	}

	/* Succeeded: the sync samples are marked. */
	return 0;
}

/*
 * Reports a sample's presentation time in microseconds (after the edit
 * list's shift).
 */
static int64_t
sample_pts_us(
	const struct mp4_track *track,
	const struct mp4_sample *sample)
{
	/* Decoding time, plus the composition offset, less the shift. */
	return mf_scale_us(sample->dts + sample->cts - track->shift, track->timescale);
}

/*
 * Reports a sample's decoding time in microseconds (after the same shift).
 */
static int64_t
sample_dts_us(
	const struct mp4_track *track,
	const struct mp4_sample *sample)
{
	/* Decoding time less the shift. */
	return mf_scale_us(sample->dts - track->shift, track->timescale);
}
