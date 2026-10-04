/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Video Player (WS122 p002): the simple player of beta 1.  It opens a file,
 * plays it, pauses, stops and seeks.  FFmpeg's libraries (the libavcodec
 * package, LGPL) read the container and decode; libswscale fits a picture
 * to the window; the sound goes to audiod (its client is audio.c); the
 * window, its menu and the controls are libkeiland's.
 *
 * Two threads: the window's (main.c), which draws the picture whose time
 * has come and takes the input, and the media thread (media.c), which reads
 * and decodes ahead, keeps a few pictures and writes the sound.  The clock
 * is the sound's (audiod's read position) when there is sound, otherwise
 * the monotonic clock.
 */

#ifndef VIDEOPLAYER_VIDEOPLAYER_H
#define VIDEOPLAYER_VIDEOPLAYER_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

/* How many decoded pictures wait at most. */
#define VP_PICTURES		8U

/* The media's state, as the window shows it. */
#define VP_EMPTY		0U	/* nothing open */
#define VP_PLAYING		1U
#define VP_PAUSED		2U
#define VP_ENDED		3U	/* played to its end */

struct AVFrame;

/*
 * A connection to audiod and one playback stream (audio.c): the socket,
 * the stream's shared memory (its header and ring), the format, and the
 * serial of the next request.  The lock keeps the requests of the two
 * threads apart.
 */
struct vp_audio {
	int socket;
	pthread_mutex_t lock;
	uint32_t serial;
	void *shm;
	size_t shm_bytes;
	uint32_t rate;
	uint32_t channels;
	uint32_t capacity;
	int created;
	int running;

	/* What audiod sent and is not read yet (a stream socket of whole messages). */
	unsigned char input[256];
	size_t input_used;
};

/*
 * The media (media.c): the file, what is known of it, the pictures decoded
 * ahead, the clock's anchors and the requests to the media thread.  The
 * lock covers every field after it; the condition wakes the thread when a
 * picture is taken or a request comes.
 */
struct vp_media {
	pthread_t thread;
	int thread_started;
	struct vp_audio *audio;

	pthread_mutex_t lock;
	pthread_cond_t wake;

	/* What is open: the path, the picture's size, the length (seconds), and whether there is sound. */
	char path[1024];
	int width;
	int height;
	double duration;
	int has_audio;
	int error;

	/* The state, and the end of the file reached by the reader. */
	unsigned state;
	int eof;

	/* The pictures decoded ahead (a ring of references), with their times (seconds). */
	struct AVFrame *pictures[VP_PICTURES];
	double picture_times[VP_PICTURES];
	unsigned picture_first;
	unsigned picture_count;

	/* The clock: the time at an anchor, and the anchor (audiod's read position, or the monotonic time). */
	double clock_time;
	uint64_t clock_frames;
	uint64_t clock_us;

	/* Requests: a seek (to a time), and the end of the thread. */
	int seek_wanted;
	double seek_to;
	int quit;
};

/* The sound (audio.c). */
int vp_audio_open(struct vp_audio *audio);
void vp_audio_close(struct vp_audio *audio);
int vp_audio_start(struct vp_audio *audio);
int vp_audio_stop(struct vp_audio *audio);
int vp_audio_flush(struct vp_audio *audio);
uint64_t vp_audio_read_position(const struct vp_audio *audio);
uint64_t vp_audio_write_position(const struct vp_audio *audio);
size_t vp_audio_write(struct vp_audio *audio, const int16_t *samples, size_t frames);

/* The media (media.c). */
void vp_media_init(struct vp_media *media, struct vp_audio *audio);
int vp_media_open(struct vp_media *media, const char *path);
void vp_media_close(struct vp_media *media);
void vp_media_play(struct vp_media *media);
void vp_media_pause(struct vp_media *media);
void vp_media_seek(struct vp_media *media, double seconds);
double vp_media_clock(struct vp_media *media);
struct AVFrame *vp_media_take(struct vp_media *media, double clock, double *time, double *next);

/* The log the tests read (main.c). */
void vp_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

#endif
