/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test tool of libmedia's SBC (ws205-p002), driven by
 * run-sbc-host-test.sh, which uses FFmpeg's SBC as the independent side:
 *
 *   sbc-tool signal RATE CHANNELS SECONDS OUT.raw   a test signal (16-bit, interleaved)
 *   sbc-tool encode RATE CHANNELS MODE SUBBANDS BLOCKS BITPOOL IN.raw OUT.sbc
 *   sbc-tool encode-msbc IN.raw OUT.msbc              16 kHz mono
 *   sbc-tool decode IN.sbc OUT.raw                    prints the frames' settings
 *   sbc-tool compare A.raw B.raw CHANNELS MIN_DB      SNR of B against A at the best delay
 *   sbc-tool self                                     checks without FFmpeg
 *
 * MODE is mono, dual, stereo or joint, with -snr for the SNR allocation
 * (else loudness).  Each command prints one line; a
 * failure prints FAIL and exits 1.
 */

#include "userland/desktop/libmedia/media-sbc.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The constant pi. */
#define TOOL_PI			3.14159265358979323846

/* The longest delay looked for between two signals (samples). */
#define TOOL_DELAY_MAX		2000L

int main(int argc, char **argv);
static int tool_signal(unsigned rate, unsigned channels, double seconds, const char *path);
static int tool_encode(const struct media_sbc_config *config, const char *input, const char *output);
static int tool_decode(const char *input, const char *output);
static int tool_compare(const char *first, const char *second, unsigned channels, double least);
static int tool_self(void);
static int tool_read(const char *path, unsigned char **data, size_t *size);
static int tool_write(const char *path, const void *data, size_t size);
static int tool_mode(const char *name, unsigned *mode);

/*
 * Runs one command.
 */
int
main(
	int argc,
	char **argv)
{
	struct media_sbc_config config;
	const char *command;
	unsigned mode;
	unsigned channels;
	unsigned asked;
	char *snr;
	int same;
	int error;
	int status;

	/* A command named. */
	if (argc < 2) {
		fprintf(stderr, "usage: sbc-tool signal|encode|encode-msbc|decode|compare|self ...\n");
		return 2;
	}

	/* Its name. */
	command = argv[1];

	/* A test signal. */
	same = strcmp(command, "signal");
	if (same == 0 && argc == 6) {
		status = tool_signal((unsigned)atoi(argv[2]), (unsigned)atoi(argv[3]), atof(argv[4]), argv[5]);
		return status;
	}

	/* An encoding of given settings; "-snr" after the mode for the SNR allocation. */
	same = strcmp(command, "encode");
	if (same == 0 && argc == 10) {
		memset(&config, 0, sizeof(config));
		config.allocation = MEDIA_SBC_LOUDNESS;
		snr = strstr(argv[4], "-snr");
		if (snr != NULL) {
			config.allocation = MEDIA_SBC_SNR;
			*snr = '\0';
		}

		/* The mode, then the rest of the settings. */
		error = tool_mode(argv[4], &mode);
		if (error != 0)
			return 2;
		config.rate = (unsigned)atoi(argv[2]);
		config.channel_mode = mode;
		config.subbands = (unsigned)atoi(argv[5]);
		config.blocks = (unsigned)atoi(argv[6]);
		config.bitpool = (unsigned)atoi(argv[7]);

		/* The channels the mode has. */
		channels = media_sbc_channels(&config);
		asked = (unsigned)atoi(argv[3]);
		if (asked != channels)
			return 2;
		status = tool_encode(&config, argv[8], argv[9]);
		return status;
	}

	/* An mSBC encoding. */
	same = strcmp(command, "encode-msbc");
	if (same == 0 && argc == 4) {
		media_sbc_msbc_config(&config);
		status = tool_encode(&config, argv[2], argv[3]);
		return status;
	}

	/* A decoding. */
	same = strcmp(command, "decode");
	if (same == 0 && argc == 4) {
		status = tool_decode(argv[2], argv[3]);
		return status;
	}

	/* A comparison. */
	same = strcmp(command, "compare");
	if (same == 0 && argc == 6) {
		status = tool_compare(argv[2], argv[3], (unsigned)atoi(argv[4]), atof(argv[5]));
		return status;
	}

	/* The checks without FFmpeg. */
	same = strcmp(command, "self");
	if (same == 0 && argc == 2) {
		status = tool_self();
		return status;
	}

	/* Anything else. */
	fprintf(stderr, "usage: sbc-tool signal|encode|encode-msbc|decode|compare|self ...\n");
	return 2;
}

/* Writes a test signal: each channel a different chord with a slow sweep, -6 dBFS at its peak. */
static int
tool_signal(
	unsigned rate,
	unsigned channels,
	double seconds,
	const char *path)
{
	int16_t *pcm;
	size_t frames;
	size_t index;
	unsigned channel;
	double time;
	double value;
	double sweep;
	int error;

	/* The samples. */
	frames = (size_t)(seconds * (double)rate);
	pcm = calloc(frames * channels, sizeof(int16_t));
	if (pcm == NULL)
		return 1;
	for (index = 0U; index < frames; index++) {
		time = (double)index / (double)rate;
		sweep = 200.0 + 3000.0 * time / seconds;
		for (channel = 0U; channel < channels; channel++) {
			value = 0.25 * sin(2.0 * TOOL_PI * (440.0 + 110.0 * channel) * time);
			value += 0.15 * sin(2.0 * TOOL_PI * (1250.0 - 300.0 * channel) * time);
			value += 0.1 * sin(2.0 * TOOL_PI * sweep * time * 0.5);
			pcm[index * channels + channel] = (int16_t)lrint(value * 32767.0);
		}
	}

	/* The file. */
	error = tool_write(path, pcm, frames * channels * sizeof(int16_t));
	free(pcm);
	if (error != 0) {
		printf("FAIL signal: %s\n", strerror(error));
		return 1;
	}

	/* Done. */
	printf("signal frames=%zu\n", frames);
	return 0;
}

/* Encodes a raw file frame by frame (the last part frame left out). */
static int
tool_encode(
	const struct media_sbc_config *config,
	const char *input,
	const char *output)
{
	struct media_sbc *codec;
	unsigned char *data;
	unsigned char *out;
	size_t size;
	size_t per_frame;
	size_t frames;
	size_t index;
	size_t length;
	size_t written;
	unsigned channels;
	int error;

	/* The input and the encoder. */
	error = tool_read(input, &data, &size);
	if (error != 0) {
		printf("FAIL encode read: %s\n", strerror(error));
		return 1;
	}

	/* The encoder. */
	error = media_sbc_encoder_create(config, &codec);
	if (error != 0) {
		printf("FAIL encoder: %s\n", strerror(error));
		free(data);
		return 1;
	}

	/* Each whole frame. */
	channels = media_sbc_channels(config);
	per_frame = (size_t)config->blocks * config->subbands * channels;
	frames = size / (per_frame * sizeof(int16_t));
	out = malloc(frames * MEDIA_SBC_FRAME_MAX + 1U);
	written = 0U;
	for (index = 0U; out != NULL && index < frames; index++) {
		error = media_sbc_encode(codec, (const int16_t *)(const void *)data + index * per_frame, out + written, MEDIA_SBC_FRAME_MAX, &length);
		if (error != 0)
			break;
		written += length;
	}

	/* The encoder and the input are no longer needed. */
	media_sbc_destroy(codec);
	free(data);
	if (out == NULL || error != 0) {
		printf("FAIL encode: %s\n", strerror(error));
		free(out);
		return 1;
	}

	/* The file. */
	error = tool_write(output, out, written);
	free(out);
	if (error != 0) {
		printf("FAIL encode write: %s\n", strerror(error));
		return 1;
	}

	/* Done. */
	printf("encode frames=%zu bytes=%zu frame_bytes=%zu\n", frames, written, media_sbc_frame_bytes(config));
	return 0;
}

/* Decodes a file of frames; a bad frame is counted and skipped. */
static int
tool_decode(
	const char *input,
	const char *output)
{
	struct media_sbc_config config;
	struct media_sbc *codec;
	unsigned char *data;
	int16_t *pcm;
	int16_t frame[MEDIA_SBC_SAMPLES_MAX * 2U];
	size_t size;
	size_t offset;
	size_t used;
	size_t count;
	size_t samples;
	size_t room;
	unsigned frames;
	unsigned bad;
	unsigned channels;
	int error;

	/* The input and the decoder. */
	error = tool_read(input, &data, &size);
	if (error != 0) {
		printf("FAIL decode read: %s\n", strerror(error));
		return 1;
	}

	/* The decoder, and room for the whole output. */
	error = media_sbc_decoder_create(&codec);
	room = size * 64U + 1U;
	pcm = malloc(room * sizeof(int16_t));
	if (error != 0 || pcm == NULL) {
		printf("FAIL decoder\n");
		free(data);
		return 1;
	}

	/* Each frame. */
	memset(&config, 0, sizeof(config));
	offset = 0U;
	samples = 0U;
	frames = 0U;
	bad = 0U;
	channels = 1U;
	while (offset < size) {
		error = media_sbc_decode(codec, data + offset, size - offset, &used, frame, sizeof(frame) / sizeof(frame[0]), &count, &config);
		if (error == EAGAIN)
			break;
		if (error == EBADMSG) {
			bad++;
			offset += used;
			continue;
		}

		/* Another failure ends the reading. */
		if (error != 0) {
			printf("FAIL decode: %s\n", strerror(error));
			break;
		}

		/* The frame's samples kept. */
		channels = media_sbc_channels(&config);
		if (samples + count * channels <= room) {
			memcpy(pcm + samples, frame, count * channels * sizeof(int16_t));
			samples += count * channels;
		}

		/* Counted. */
		frames++;
		offset += used;
	}

	/* The decoder and the input are no longer needed. */
	media_sbc_destroy(codec);
	free(data);

	/* The file. */
	error = tool_write(output, pcm, samples * sizeof(int16_t));
	free(pcm);
	if (error != 0 || frames == 0U) {
		printf("FAIL decode: frames=%u bad=%u\n", frames, bad);
		return 1;
	}

	/* Done. */
	printf("decode frames=%u bad=%u rate=%u channels=%u mode=%u subbands=%u blocks=%u bitpool=%u allocation=%u msbc=%u\n",
	    frames, bad, config.rate, channels, config.channel_mode, config.subbands, config.blocks, config.bitpool, config.allocation, config.msbc);
	return bad != 0U;
}

/* The SNR of the second signal against the first, at the delay (either way) that fits best. */
static int
tool_compare(
	const char *first,
	const char *second,
	unsigned channels,
	double least)
{
	unsigned char *a;
	unsigned char *b;
	const int16_t *x;
	const int16_t *y;
	size_t a_size;
	size_t b_size;
	size_t a_frames;
	size_t b_frames;
	size_t start;
	size_t end;
	size_t index;
	unsigned channel;
	long delay;
	long best_delay;
	double signal;
	double noise;
	double error_value;
	double snr;
	double best;
	long other;
	int error;

	/* The files. */
	error = tool_read(first, &a, &a_size);
	if (error == 0)
		error = tool_read(second, &b, &b_size);
	if (error != 0) {
		printf("FAIL compare read\n");
		return 1;
	}

	/* The signals, and their frames. */
	x = (const int16_t *)(const void *)a;
	y = (const int16_t *)(const void *)b;
	a_frames = a_size / (channels * sizeof(int16_t));
	b_frames = b_size / (channels * sizeof(int16_t));

	/* Each delay: the SNR over the middle of the signal (a second skipped at each end where it is long). */
	best = -1000.0;
	best_delay = 0L;
	start = a_frames / 8U;
	end = a_frames - a_frames / 8U;
	for (delay = -TOOL_DELAY_MAX; delay <= TOOL_DELAY_MAX; delay++) {
		signal = 0.0;
		noise = 0.0;
		for (index = start; index < end; index++) {
			other = (long)index + delay;
			if (other < 0L || (size_t)other >= b_frames)
				continue;
			for (channel = 0U; channel < channels; channel++) {
				error_value = (double)y[(size_t)other * channels + channel] - (double)x[index * channels + channel];
				signal += (double)x[index * channels + channel] * (double)x[index * channels + channel];
				noise += error_value * error_value;
			}
		}

		/* A delay without overlap says nothing. */
		if (signal == 0.0)
			continue;
		snr = 200.0;
		if (noise > 0.0)
			snr = 10.0 * log10(signal / noise);
		if (snr > best) {
			best = snr;
			best_delay = delay;
		}
	}

	/* The files are no longer needed. */
	free(a);
	free(b);

	/* The verdict. */
	if (best < least) {
		printf("FAIL compare snr=%.1f delay=%ld least=%.1f\n", best, best_delay, least);
		return 1;
	}

	/* The best fit. */
	printf("compare snr=%.1f delay=%ld\n", best, best_delay);
	return 0;
}

/* Checks without FFmpeg: settings, frame lengths, a wrong CRC, a short buffer, the perfect reconstruction's gain. */
static int
tool_self(
	void)
{
	struct media_sbc_config config;
	struct media_sbc_config found;
	struct media_sbc *encoder;
	struct media_sbc *decoder;
	uint8_t frame[MEDIA_SBC_FRAME_MAX];
	int16_t pcm[MEDIA_SBC_SAMPLES_MAX * 2U];
	int16_t out[MEDIA_SBC_SAMPLES_MAX * 2U];
	size_t length;
	size_t used;
	size_t count;
	unsigned failures;
	unsigned index;
	unsigned round;
	double peak;
	double magnitude;
	size_t bytes;
	int error;

	/* Frame lengths of known settings: mSBC 57; 44.1 kHz joint 8/16 bitpool 53 is 119; mono 4/8 bitpool 16 is 22. */
	failures = 0U;
	media_sbc_msbc_config(&config);
	bytes = media_sbc_frame_bytes(&config);
	if (bytes != MEDIA_SBC_MSBC_BYTES)
		failures++;
	memset(&config, 0, sizeof(config));
	config.rate = 44100U;
	config.blocks = 16U;
	config.channel_mode = MEDIA_SBC_JOINT;
	config.subbands = 8U;
	config.bitpool = 53U;
	bytes = media_sbc_frame_bytes(&config);
	if (bytes != 119U)
		failures++;
	config.channel_mode = MEDIA_SBC_MONO;
	config.subbands = 4U;
	config.blocks = 8U;
	config.bitpool = 16U;
	bytes = media_sbc_frame_bytes(&config);
	if (bytes != 22U)
		failures++;

	/* Their verdict. */
	if (failures == 0U) {
		printf("ok frame lengths\n");
	} else {
		printf("FAIL frame lengths\n");
	}

	/* Settings SBC does not have. */
	config.bitpool = 65U;
	error = media_sbc_check(&config);
	if (error != EINVAL) {
		printf("FAIL bitpool 65 of mono 4 subbands accepted\n");
		failures++;
	}

	/* A 1 kHz sine at 48 kHz, joint stereo, through the encoder and the decoder: its peak after the filters' delay. */
	memset(&config, 0, sizeof(config));
	config.rate = 48000U;
	config.blocks = 16U;
	config.channel_mode = MEDIA_SBC_JOINT;
	config.subbands = 8U;
	config.bitpool = 53U;
	error = media_sbc_encoder_create(&config, &encoder);
	if (error == 0)
		error = media_sbc_decoder_create(&decoder);
	if (error != 0) {
		printf("FAIL codecs: %s\n", strerror(error));
		return 1;
	}

	/* The frames, the first ten left to the filters' delay. */
	peak = 0.0;
	for (round = 0U; round < 40U; round++) {
		for (index = 0U; index < 128U; index++) {
			pcm[index * 2U] = (int16_t)lrint(16384.0 * sin(2.0 * TOOL_PI * 1000.0 * (double)(round * 128U + index) / 48000.0));
			pcm[index * 2U + 1U] = pcm[index * 2U];
		}

		/* Encoded and decoded. */
		error = media_sbc_encode(encoder, pcm, frame, sizeof(frame), &length);
		if (error == 0)
			error = media_sbc_decode(decoder, frame, length, &used, out, sizeof(out) / sizeof(out[0]), &count, &found);
		if (error != 0 || used != length || count != 128U) {
			printf("FAIL round %u: %s used=%zu count=%zu\n", round, strerror(error), used, count);
			failures++;
			break;
		}

		/* The largest output. */
		for (index = 0U; round >= 10U && index < 256U; index++) {
			magnitude = fabs((double)out[index]);
			if (magnitude > peak)
				peak = magnitude;
		}
	}

	/* The peak within 3 % of the input's. */
	if (peak < 16384.0 * 0.97 || peak > 16384.0 * 1.03) {
		printf("FAIL reconstruction peak %.0f, expected 16384 +- 3%%\n", peak);
		failures++;
	} else {
		printf("ok reconstruction peak %.0f\n", peak);
	}

	/* A wrong CRC: the frame is skipped whole. */
	frame[3] ^= 0x55U;
	error = media_sbc_decode(decoder, frame, length, &used, out, sizeof(out) / sizeof(out[0]), &count, &found);
	if (error != EBADMSG || used != length) {
		printf("FAIL wrong CRC: %s used=%zu\n", strerror(error), used);
		failures++;
	} else {
		printf("ok wrong CRC\n");
	}

	/* A short buffer, and a byte that is no syncword. */
	error = media_sbc_decode(decoder, frame, length - 1U, &used, out, sizeof(out) / sizeof(out[0]), &count, &found);
	if (error != EAGAIN || used != 0U)
		failures++;
	frame[0] = 0x12U;
	error = media_sbc_decode(decoder, frame, length, &used, out, sizeof(out) / sizeof(out[0]), &count, &found);
	if (error != EBADMSG || used != 1U)
		failures++;
	media_sbc_destroy(encoder);
	media_sbc_destroy(decoder);

	/* The verdict. */
	if (failures != 0U) {
		printf("FAIL self: %u\n", failures);
		return 1;
	}

	/* All passed. */
	printf("self all PASS\n");
	return 0;
}

/* Reads a whole file. */
static int
tool_read(
	const char *path,
	unsigned char **data,
	size_t *size)
{
	FILE *file;
	long length;
	size_t got;

	/* The file's length, then its bytes. */
	*data = NULL;
	*size = 0U;
	file = fopen(path, "rb");
	if (file == NULL)
		return errno;
	fseek(file, 0L, SEEK_END);
	length = ftell(file);
	fseek(file, 0L, SEEK_SET);
	*data = malloc((size_t)length + 1U);
	if (*data == NULL) {
		fclose(file);
		return ENOMEM;
	}

	/* The bytes. */
	got = fread(*data, 1U, (size_t)length, file);
	fclose(file);
	*size = got;
	return 0;
}

/* Writes a whole file. */
static int
tool_write(
	const char *path,
	const void *data,
	size_t size)
{
	FILE *file;
	size_t done;

	/* The bytes. */
	file = fopen(path, "wb");
	if (file == NULL)
		return errno;
	done = fwrite(data, 1U, size, file);
	fclose(file);
	if (done != size)
		return EIO;
	return 0;
}

/* Reads a channel mode's name. */
static int
tool_mode(
	const char *name,
	unsigned *mode)
{
	static const char *const names[4] = {"mono", "dual", "stereo", "joint"};
	unsigned index;
	int same;

	/* The four names, in the modes' order. */
	for (index = 0U; index < 4U; index++) {
		same = strcmp(name, names[index]);
		if (same == 0) {
			*mode = index;
			return 0;
		}
	}

	/* No mode of that name. */
	return EINVAL;
}
