/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The glyph worker (ws213-p005, the work thread W): renders glyphs the main
 * thread asks for away from the event loop, on fonts of its own opened over
 * the same bytes.
 */

#ifndef KWL_GLYPH_WORKER_H
#define KWL_GLYPH_WORKER_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include <truetype/truetype.h>

/* The fonts the worker can be given (the look's first font, its fallback and the colour emoji font). */
#define KWL_GLYPH_FONTS		3U

/*
 * A font as the main thread opened it, for the worker to open its own face
 * over: the font file's bytes (the main thread keeps them unchanged until
 * the worker stops) and the companions it opened with it (NULL: none).
 */
struct kwl_glyph_font {
	const void *data;
	size_t size;
	const char *bold_path;
	const char *next_path;
};

/*
 * One glyph to render, from the main thread's ask to its taking of the
 * result.  The main thread fills the ask: the font, the size in pixels per
 * em, the glyph, the box it measured (the worker draws only a glyph of
 * that box) and its own marks (kind, slot and generation, which the worker
 * leaves alone).  The worker fills the result: error 0 with the coverage
 * (width times height bytes, its own allocation) and the metrics of the box
 * drawn, or the reason it could not.  The job belongs to the worker from
 * its submission to its taking.
 */
struct kwl_glyph_job {
	struct kwl_glyph_job *next;
	unsigned font;
	unsigned pixels;
	unsigned glyph;
	uint32_t width;
	uint32_t height;
	unsigned kind;
	unsigned slot;
	uint64_t generation;
	int error;
	struct truetype_glyph metrics;
	uint8_t *bitmap;
};

/*
 * The worker and what it shares with the main thread.
 *
 * Under lock: the jobs asked for and not rendered yet (in their order), the
 * jobs rendered and not taken (in the same order), the fonts given, the
 * stop request and whether the main thread was woken for the rendered
 * ones.  work wakes the worker.  The faces are the worker's own; wake_main
 * is the main thread's wake pipe's write end.
 */
struct kwl_glyph_worker {
	pthread_mutex_t lock;
	pthread_cond_t work;
	struct kwl_glyph_job *jobs;
	struct kwl_glyph_job *jobs_tail;
	struct kwl_glyph_job *done;
	struct kwl_glyph_job *done_tail;
	struct kwl_glyph_font fonts[KWL_GLYPH_FONTS];
	struct truetype_face *faces[KWL_GLYPH_FONTS];
	unsigned stop;
	unsigned main_woken;
	int wake_main;
	pthread_t thread;
	unsigned started;
};

int kwl_glyph_worker_start(struct kwl_glyph_worker *worker, int wake_main);
void kwl_glyph_worker_stop(struct kwl_glyph_worker *worker);
void kwl_glyph_worker_font(struct kwl_glyph_worker *worker, unsigned slot, const struct kwl_glyph_font *font);
void kwl_glyph_worker_submit(struct kwl_glyph_worker *worker, struct kwl_glyph_job *job);
struct kwl_glyph_job *kwl_glyph_worker_take(struct kwl_glyph_worker *worker);
int kwl_glyph_worker_ready(struct kwl_glyph_worker *worker);
void kwl_glyph_job_free(struct kwl_glyph_job *job);

#endif
