/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the glyph worker (ws213-p005, the design's 9 (j);
 * userland/desktop/wayland/glyph-worker.c) on the tree's Mahora fonts with
 * the real libtruetype.
 *
 * The first part asks for glyphs at several sizes and checks that they come
 * back in the order asked, each with the coverage and box a face of the
 * main thread's own renders; that a box other than the one measured, or a
 * font never given, comes back as an error without coverage; that the main
 * thread's wake pipe is written once for the glyphs it has not taken; and
 * that a stop with glyphs still asked for frees them.
 *
 * The second part plays the look's cache: a main thread gives the cells of
 * a small cache to random glyphs (a new assignment numbered each time),
 * asks the worker, and at random takes what was rendered and writes a
 * result only into a cell still at the assignment it was asked for, as the
 * start of a walk does.  At the end every cell holds exactly its last
 * glyph's coverage: a cell given twice keeps the later glyph.  Built with
 * -fsanitize=thread it is the design's 9 (m) for the worker.
 */

#include "userland/desktop/wayland/glyph-worker.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The cache's cells and the assignments of the second part. */
#define CACHE_CELLS		8U
#define CACHE_STEPS		4000U

/* The largest box the test keeps, as the look's cell. */
#define CELL_SIDE		64U

/* The checks run and failed. */
static unsigned checks;
static unsigned failures;

/* The font files' bytes, kept for the whole test. */
static void *regular_data;
static size_t regular_size;

/*
 * One cell of the played cache: the glyph and size it was last given, the
 * number of that assignment, and the coverage written into it.
 */
struct cell {
	unsigned glyph;
	unsigned pixels;
	uint64_t generation;
	uint32_t width;
	uint32_t height;
	uint8_t pixels_written[CELL_SIDE * CELL_SIDE];
	unsigned written;
};

static void check(int condition, const char *what);
static void *read_file(const char *path, size_t *size);
static struct kwl_glyph_job *ask(struct kwl_glyph_worker *worker, struct truetype_face *face, unsigned font, unsigned glyph, unsigned pixels, unsigned slot, uint64_t generation);
static struct kwl_glyph_job *wait_taken(struct kwl_glyph_worker *worker, int wake, unsigned count);
static int same_render(struct truetype_face *face, const struct kwl_glyph_job *job);
static void order_and_results(void);
static void cache_play(void);

/*
 * Runs both parts and reports.
 */
int
main(
	void)
{
	/* The regular font. */
	regular_data = read_file("fonts/keiland/Mahora-Regular.ttf", &regular_size);
	if (regular_data == NULL) {
		printf("glyph-worker-host-test: FAIL (no font)\n");
		return 1;
	}

	/* The parts. */
	order_and_results();
	cache_play();

	/* The result. */
	free(regular_data);
	printf("glyph-worker-host-test: %u checks, %u failed\n", checks, failures);
	if (failures != 0U)
		return 1;

	/* Succeeded: every check held. */
	return 0;
}

/* Counts a check and reports a failed one. */
static void
check(
	int condition,
	const char *what)
{
	/* The count. */
	checks++;
	if (condition)
		return;

	/* The failure. */
	failures++;
	printf("FAIL: %s\n", what);
}

/* Reads a whole file (NULL: it cannot be read). */
static void *
read_file(
	const char *path,
	size_t *size)
{
	FILE *file;
	void *data;
	long length;

	/* The file and its length. */
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	(void)fseek(file, 0, SEEK_END);
	length = ftell(file);
	(void)fseek(file, 0, SEEK_SET);
	if (length <= 0) {
		fclose(file);
		return NULL;
	}

	/* Its bytes. */
	data = malloc((size_t)length);
	if (data == NULL || fread(data, 1, (size_t)length, file) != (size_t)length) {
		free(data);
		fclose(file);
		return NULL;
	}
	fclose(file);

	/* Succeeded. */
	*size = (size_t)length;
	return data;
}

/* Measures a glyph on the main thread's face and asks the worker for it, as the look does; NULL for an empty box. */
static struct kwl_glyph_job *
ask(
	struct kwl_glyph_worker *worker,
	struct truetype_face *face,
	unsigned font,
	unsigned glyph,
	unsigned pixels,
	unsigned slot,
	uint64_t generation)
{
	struct truetype_glyph metrics;
	struct kwl_glyph_job *job;

	/* The box at the size. */
	if (truetype_set_pixel_size(face, pixels) != 0)
		return NULL;
	if (truetype_glyph_metrics(face, glyph, &metrics) != 0)
		return NULL;
	if (metrics.width == 0U || metrics.height == 0U)
		return NULL;

	/* The job. */
	job = calloc(1, sizeof(*job));
	if (job == NULL)
		return NULL;
	job->font = font;
	job->pixels = pixels;
	job->glyph = glyph;
	job->width = metrics.width;
	job->height = metrics.height;
	job->slot = slot;
	job->generation = generation;
	kwl_glyph_worker_submit(worker, job);

	/* Succeeded: the job is the worker's. */
	return job;
}

/* Waits on the wake pipe until count glyphs were taken, and gives them as one list in order. */
static struct kwl_glyph_job *
wait_taken(
	struct kwl_glyph_worker *worker,
	int wake,
	unsigned count)
{
	struct kwl_glyph_job *list;
	struct kwl_glyph_job *tail;
	struct kwl_glyph_job *taken;
	struct pollfd descriptor;
	unsigned char drained[16];
	unsigned have;
	unsigned rounds;

	/* The glyphs as they come, for at most ten seconds. */
	list = NULL;
	tail = NULL;
	have = 0U;
	for (rounds = 0U; have < count && rounds < 1000U; rounds++) {
		descriptor.fd = wake;
		descriptor.events = POLLIN;
		descriptor.revents = 0;
		(void)poll(&descriptor, 1U, 10);
		while (read(wake, drained, sizeof(drained)) > 0)
			;
		taken = kwl_glyph_worker_take(worker);
		while (taken != NULL) {
			if (tail == NULL) {
				list = taken;
			} else {
				tail->next = taken;
			}
			tail = taken;
			taken = taken->next;
			have++;
		}
	}

	/* The list. */
	return list;
}

/* Tells whether a rendered job holds what the main thread's own face renders for it. */
static int
same_render(
	struct truetype_face *face,
	const struct kwl_glyph_job *job)
{
	static uint8_t bitmap[CELL_SIDE * CELL_SIDE * 4U];
	struct truetype_glyph metrics;

	/* The glyph on the main thread's face. */
	if (truetype_set_pixel_size(face, job->pixels) != 0)
		return 0;
	if (truetype_glyph_metrics(face, job->glyph, &metrics) != 0)
		return 0;
	if (truetype_render_glyph(face, job->glyph, &metrics, bitmap, metrics.width, sizeof(bitmap)) != 0)
		return 0;

	/* The same box and the same coverage. */
	if (metrics.width != job->metrics.width || metrics.height != job->metrics.height)
		return 0;
	if (metrics.left != job->metrics.left || metrics.top != job->metrics.top || metrics.advance != job->metrics.advance)
		return 0;
	if (memcmp(bitmap, job->bitmap, (size_t)metrics.width * metrics.height) != 0)
		return 0;

	/* Succeeded. */
	return 1;
}

/* The first part: order, results, errors, the wake and the stop. */
static void
order_and_results(void)
{
	static const unsigned sizes[] = { 14U, 20U, 36U, 64U };
	struct kwl_glyph_worker worker;
	struct kwl_glyph_font font;
	struct truetype_face *face;
	struct kwl_glyph_job *job;
	struct kwl_glyph_job *list;
	struct kwl_glyph_job *bad;
	unsigned char drained[16];
	int pipes[2];
	unsigned asked;
	unsigned index;
	unsigned size;
	unsigned in_order;
	unsigned same;
	ssize_t got;

	/* The main thread's face and wake pipe, and the worker given the font. */
	check(truetype_open(regular_data, regular_size, 0U, &face) == 0, "the main thread's face opens");
	check(pipe2(pipes, O_NONBLOCK | O_CLOEXEC) == 0, "the wake pipe");
	check(kwl_glyph_worker_start(&worker, pipes[1]) == 0, "the worker starts");
	memset(&font, 0, sizeof(font));
	font.data = regular_data;
	font.size = regular_size;
	font.bold_path = "fonts/keiland/Mahora-Bold.ttf";
	kwl_glyph_worker_font(&worker, 0U, &font);

	/* The glyphs of 'A' to 'z' at each size, numbered in the order asked. */
	asked = 0U;
	for (size = 0U; size < sizeof(sizes) / sizeof(sizes[0]); size++) {
		for (index = 'A'; index <= 'z'; index++) {
			job = ask(&worker, face, 0U, truetype_glyph_index(face, index), sizes[size], asked, 1U);
			if (job != NULL)
				asked++;
		}
	}

	/* They come back in that order, each as the main thread's face renders it. */
	list = wait_taken(&worker, pipes[0], asked);
	in_order = 1U;
	same = 1U;
	index = 0U;
	for (job = list; job != NULL; job = job->next) {
		if (job->slot != index || job->error != 0)
			in_order = 0U;
		if (job->error == 0 && !same_render(face, job))
			same = 0U;
		index++;
	}
	check(index == asked, "every glyph asked for comes back");
	check(in_order, "the glyphs come back in the order asked, without errors");
	check(same, "each coverage and box is the main thread's face's");
	while (list != NULL) {
		job = list->next;
		kwl_glyph_job_free(list);
		list = job;
	}

	/* A box other than the one measured, and a font never given, come back as errors. */
	job = ask(&worker, face, 0U, truetype_glyph_index(face, 'M'), 20U, 0U, 2U);
	check(job != NULL, "a glyph is asked for");
	bad = calloc(1, sizeof(*bad));
	bad->font = 2U;
	bad->pixels = 20U;
	bad->glyph = truetype_glyph_index(face, 'M');
	bad->width = 3U;
	bad->height = 3U;
	bad->slot = 1U;
	kwl_glyph_worker_submit(&worker, bad);
	bad = calloc(1, sizeof(*bad));
	bad->font = 0U;
	bad->pixels = 20U;
	bad->glyph = truetype_glyph_index(face, 'M');
	bad->width = 3U;
	bad->height = 3U;
	bad->slot = 2U;
	kwl_glyph_worker_submit(&worker, bad);
	list = wait_taken(&worker, pipes[0], 3U);
	check(list != NULL && list->error == 0 && list->bitmap != NULL, "the measured glyph is rendered");
	check(list != NULL && list->next != NULL && list->next->error == ENOENT && list->next->bitmap == NULL, "a font never given is an error without coverage");
	check(list != NULL && list->next != NULL && list->next->next != NULL && list->next->next->error == EINVAL && list->next->next->bitmap == NULL, "another box is an error without coverage");
	while (list != NULL) {
		job = list->next;
		kwl_glyph_job_free(list);
		list = job;
	}

	/* Glyphs rendered and not taken write the pipe once. */
	while (read(pipes[0], drained, sizeof(drained)) > 0)
		;
	(void)kwl_glyph_worker_take(&worker);
	for (index = 0U; index < 20U; index++)
		(void)ask(&worker, face, 0U, truetype_glyph_index(face, 'a' + index), 36U, index, 3U);
	for (index = 0U; index < 1000U && kwl_glyph_worker_ready(&worker) == 0; index++)
		usleep(1000);
	usleep(200000);
	got = read(pipes[0], drained, sizeof(drained));
	check(got == 1, "the pipe is written once for the glyphs not taken");
	list = kwl_glyph_worker_take(&worker);
	check(list != NULL, "the glyphs are taken");
	while (list != NULL) {
		job = list->next;
		kwl_glyph_job_free(list);
		list = job;
	}

	/* A stop with glyphs still asked for frees them. */
	for (index = 0U; index < 2000U; index++)
		(void)ask(&worker, face, 0U, truetype_glyph_index(face, 'a' + index % 26U), 64U, index, 4U);
	kwl_glyph_worker_stop(&worker);
	check(worker.started == 0U && worker.jobs == NULL && worker.done == NULL, "the stop ends the worker and frees its jobs");

	/* The face and the pipe. */
	truetype_close(face);
	close(pipes[0]);
	close(pipes[1]);
}

/* The second part: the look's cache, a cell given twice keeping the later glyph. */
static void
cache_play(void)
{
	static struct cell cells[CACHE_CELLS];
	static uint8_t bitmap[CELL_SIDE * CELL_SIDE * 4U];
	struct truetype_glyph metrics;
	struct kwl_glyph_worker worker;
	struct kwl_glyph_font font;
	struct truetype_face *face;
	struct kwl_glyph_job *job;
	struct kwl_glyph_job *list;
	struct cell *cell;
	unsigned char drained[16];
	uint64_t generation;
	unsigned step;
	unsigned which;
	unsigned glyph;
	unsigned right;
	unsigned seed;
	int pipes[2];
	uint32_t line;

	/* The main thread's face, the pipe and the worker. */
	check(truetype_open(regular_data, regular_size, 0U, &face) == 0, "the cache's face opens");
	check(pipe2(pipes, O_NONBLOCK | O_CLOEXEC) == 0, "the cache's wake pipe");
	check(kwl_glyph_worker_start(&worker, pipes[1]) == 0, "the cache's worker starts");
	memset(&font, 0, sizeof(font));
	font.data = regular_data;
	font.size = regular_size;
	kwl_glyph_worker_font(&worker, 0U, &font);
	memset(cells, 0, sizeof(cells));

	/* Random assignments, and takes at random, as walks start. */
	seed = 12345U;
	generation = 0U;
	for (step = 0U; step < CACHE_STEPS; step++) {
		seed = seed * 1103515245U + 12345U;
		which = (seed >> 16) % CACHE_CELLS;
		glyph = truetype_glyph_index(face, 'A' + (seed >> 8) % 26U);
		generation++;
		cell = &cells[which];
		cell->glyph = glyph;
		cell->pixels = 14U + (seed >> 4) % 40U;
		cell->generation = generation;
		cell->written = 0U;
		job = ask(&worker, face, 0U, glyph, cell->pixels, which, generation);
		if (job == NULL)
			cell->generation = 0U;

		/* A walk starts now and then: the results go only into cells at their assignment. */
		if ((seed >> 20) % 7U != 0U)
			continue;
		while (read(pipes[0], drained, sizeof(drained)) > 0)
			;
		list = kwl_glyph_worker_take(&worker);
		while (list != NULL) {
			job = list->next;
			cell = &cells[list->slot];
			if (list->error == 0 && cell->generation == list->generation) {
				for (line = 0U; line < list->metrics.height; line++)
					memcpy(&cell->pixels_written[line * CELL_SIDE], &list->bitmap[line * list->metrics.width], list->metrics.width);
				cell->width = list->metrics.width;
				cell->height = list->metrics.height;
				cell->written = 1U;
			}
			kwl_glyph_job_free(list);
			list = job;
		}
	}

	/* The last results. */
	for (step = 0U; step < 2000U; step++) {
		list = kwl_glyph_worker_take(&worker);
		while (list != NULL) {
			job = list->next;
			cell = &cells[list->slot];
			if (list->error == 0 && cell->generation == list->generation) {
				for (line = 0U; line < list->metrics.height; line++)
					memcpy(&cell->pixels_written[line * CELL_SIDE], &list->bitmap[line * list->metrics.width], list->metrics.width);
				cell->width = list->metrics.width;
				cell->height = list->metrics.height;
				cell->written = 1U;
			}
			kwl_glyph_job_free(list);
			list = job;
		}
		right = 1U;
		for (which = 0U; which < CACHE_CELLS; which++) {
			if (cells[which].generation != 0U && !cells[which].written)
				right = 0U;
		}
		if (right)
			break;
		usleep(1000);
	}

	/* Every cell holds its last glyph's coverage. */
	right = 1U;
	for (which = 0U; which < CACHE_CELLS; which++) {
		cell = &cells[which];
		if (cell->generation == 0U)
			continue;
		if (!cell->written) {
			right = 0U;
			continue;
		}
		(void)truetype_set_pixel_size(face, cell->pixels);
		(void)truetype_glyph_metrics(face, cell->glyph, &metrics);
		(void)truetype_render_glyph(face, cell->glyph, &metrics, bitmap, metrics.width, sizeof(bitmap));
		if (metrics.width != cell->width || metrics.height != cell->height)
			right = 0U;
		for (line = 0U; line < metrics.height && right; line++) {
			if (memcmp(&cell->pixels_written[line * CELL_SIDE], &bitmap[line * metrics.width], metrics.width) != 0)
				right = 0U;
		}
	}
	check(right, "every cell holds the coverage of the glyph it was last given");

	/* The worker, the face and the pipe. */
	kwl_glyph_worker_stop(&worker);
	truetype_close(face);
	close(pipes[0]);
	close(pipes[1]);
}
