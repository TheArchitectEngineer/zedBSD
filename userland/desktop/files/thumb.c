/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The pictures of files: their thumbnails for the icons, the pictures the
 * preview, Quick Look and the Today page show, and the fitting of a
 * picture into a box.
 *
 * ws168-p004 (plan/ws168/phase001/phase.md section 5): Files decodes no
 * picture itself.  keiland-preview makes each one in a sandbox (on
 * zedBSD sandbox_spawn, on Linux a process that confines itself with
 * seccomp; on FreeBSD, until it confines itself with Capsicum, in this
 * process: userland/desktop/preview/client.h), and Files reads back only
 * the binary PPM it wrote.
 *
 * Thumbnails are made when an item is drawn and not yet kept: the drawing
 * asks, one child is started, and the main loop looks at it each round
 * until it ends (the window keeps answering meanwhile); the child writes
 * the thumbnail's record in the cache (thumb-cache.c) beside its place,
 * renamed into it when it succeeded.  The last FM_THUMBS thumbnails are
 * kept, the least recently drawn going first; a file changed since it was
 * read is read again.
 */

#include "files.h"

#include "../preview/client.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The size the Today page's picture is made within. */
#define THUMB_HERO_WIDTH	1920
#define THUMB_HERO_HEIGHT	1200

/*
 * The thumbnail being made (one at a time): whether a child runs, its
 * job, the slot it goes in, the record's place in the cache and the file
 * the child writes beside it.
 */
struct thumb_making {
	int running;
	struct preview_job job;
	struct fm_thumb *thumb;
	char record[FM_PATH_MAX];
	char temporary[FM_PATH_MAX + 32];
};

/* The program's thumbnail being made. */
static struct thumb_making thumb_making;

static int thumb_start(struct fm_thumb *thumb);
static int thumb_follow(void);
static int thumb_picture(const char *path, int width, int height, struct kl_image *image);
static struct fm_thumb *thumb_find(struct fm_app *app, const char *path, time_t modified);
static struct fm_thumb *thumb_slot(struct fm_app *app);

/*
 * Reads a picture file into an image of opaque pixels no larger than the
 * Today page's (the first page of a PDF too).
 *
 * Returns 0, EINVAL for a file that is not a picture read or a damaged
 * one, EFBIG for one too large, or another errno value.
 */
int
fm_image_load(
	const char *path,
	struct kl_image *image)
{
	/* Made by keiland-preview. */
	return thumb_picture(path, THUMB_HERO_WIDTH, THUMB_HERO_HEIGHT, image);
}

/*
 * Reads a picture and shrinks it to fit a square of a side, keeping its
 * shape; a picture smaller than the square keeps its size.
 *
 * Returns 0 or an errno value.
 */
int
fm_image_thumbnail(
	const char *path,
	int side,
	struct kl_image *thumbnail)
{
	/* Made by keiland-preview. */
	return thumb_picture(path, side, side, thumbnail);
}

/*
 * Returns the kept thumbnail of a file as it is now, or NULL.
 *
 * A file that has no thumbnail yet is asked for, and its thumbnail is made
 * in later rounds of the main loop (fm_thumb_tick); only one file is
 * asked for at a time, so the drawing asks again for the others.
 */
const struct kl_image *
fm_thumb_get(
	struct fm_app *app,
	const char *path,
	time_t modified)
{
	struct fm_thumb *thumb;

	/* A thumbnail of the same file as it is now is used, and counts as recently drawn. */
	thumb = thumb_find(app, path, modified);
	if (thumb != NULL) {
		app->thumb_clock++;
		thumb->used = app->thumb_clock;

		/* A file that could not be read, or one still being made, has no picture. */
		if (thumb->failed != 0 || thumb->pending != 0)
			return NULL;

		/* The picture. */
		return &thumb->image;
	}

	/* Otherwise the file is asked for, unless another one already is. */
	if (app->thumb_wanted[0] == '\0') {
		snprintf(app->thumb_wanted, sizeof(app->thumb_wanted), "%s", path);
		app->thumb_wanted_modified = modified;
	}

	/* No picture yet. */
	return NULL;
}

/*
 * Follows the thumbnail being made, or starts the one asked for in the
 * slot least recently drawn (taken from the cache when it is there).
 *
 * Returns nonzero when one was made, so that the window is drawn again.
 */
int
fm_thumb_tick(
	struct fm_app *app)
{
	struct fm_thumb *thumb;
	int error;

	/* A child running: looked at. */
	if (thumb_making.running)
		return thumb_follow();

	/* Nothing is asked for. */
	if (app->thumb_wanted[0] == '\0')
		return 0;

	/* The slot, emptied of the thumbnail it held, now the file asked for's. */
	thumb = thumb_slot(app);
	kl_image_release(&thumb->image);
	snprintf(thumb->path, sizeof(thumb->path), "%s", app->thumb_wanted);
	thumb->modified = app->thumb_wanted_modified;
	app->thumb_clock++;
	thumb->used = app->thumb_clock;
	thumb->failed = 0;
	thumb->pending = 0;
	app->thumb_wanted[0] = '\0';

	/* The thumbnail kept on disk for the file as it is now (ws127-p002). */
	error = fm_thumb_cache_read(thumb->path, &thumb->image);
	if (error == 0) {
		fm_log("THUMB path=%s error=0 width=%d height=%d cached=1", thumb->path, thumb->image.width, thumb->image.height);
		return 1;
	}

	/* Else a child makes it (made at once in this process on FreeBSD: followed now). */
	error = thumb_start(thumb);
	if (error == 0)
		return thumb_follow();

	/* One that cannot be started is a file without a thumbnail. */
	thumb->failed = 1;
	fm_log("THUMB path=%s error=%d width=0 height=0 cached=0", thumb->path, error);
	return 1;
}

/*
 * Tells whether a thumbnail is being made (the main loop then looks at it
 * again soon).
 */
int
fm_thumb_busy(void)
{
	/* A child running. */
	return thumb_making.running;
}

/*
 * Frees the kept thumbnails (a child still running is ended).
 */
void
fm_thumb_release(
	struct fm_app *app)
{
	int index;

	/* The child, ended, and what it wrote. */
	if (thumb_making.running) {
		thumb_making.job.limit_ms = 0;
		(void)preview_poll(&thumb_making.job);
		(void)unlink(thumb_making.temporary);
	}

	/* None running. */
	thumb_making.running = 0;

	/* Each slot's picture, and the slot emptied. */
	for (index = 0; index < FM_THUMBS; index++) {
		kl_image_release(&app->thumbs[index].image);
		memset(&app->thumbs[index], 0, sizeof(app->thumbs[index]));
	}

	/* Nothing is asked for any more. */
	app->thumb_wanted[0] = '\0';
}

/*
 * Works out the size of a picture fitted in a box, keeping its shape; the
 * fitted picture is at least a pixel each way.
 */
void
fm_image_fit(
	int width,
	int height,
	int box_width,
	int box_height,
	int *fit_width,
	int *fit_height)
{
	/* An empty picture fills nothing. */
	if (width <= 0 || height <= 0) {
		*fit_width = 1;
		*fit_height = 1;
		return;
	}

	/* The box's width decides when the picture is wider in shape than the box; its height otherwise. */
	if ((long)width * box_height >= (long)height * box_width) {
		*fit_width = box_width;
		*fit_height = (int)((long)height * box_width / width);
	} else {
		*fit_height = box_height;
		*fit_width = (int)((long)width * box_height / height);
	}

	/* At least a pixel each way. */
	if (*fit_width < 1)
		*fit_width = 1;
	if (*fit_height < 1)
		*fit_height = 1;
}

/*
 * Starts the child that writes a slot's thumbnail as its record in the
 * cache (the file beside the record, renamed when it succeeds).  Returns
 * 0, or an errno value.
 */
static int
thumb_start(
	struct fm_thumb *thumb)
{
	struct preview_request request;
	int error;
	int fd;

	/* The record's place and the stamp the cache reads (the file's time and size). */
	memset(&request, 0, sizeof(request));
	request.width = FM_THUMB_SIDE;
	request.height = FM_THUMB_SIDE;
	error = fm_thumb_cache_target(thumb->path, thumb_making.record, sizeof(thumb_making.record), request.stamp, sizeof(request.stamp));
	if (error != 0)
		return error;

	/* The file the child writes, new. */
	(void)snprintf(thumb_making.temporary, sizeof(thumb_making.temporary), "%s.%ld", thumb_making.record, (long)getpid());
	fd = open(thumb_making.temporary, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
	if (fd < 0)
		return errno;

	/* The child. */
	error = preview_start(thumb->path, fd, &request, &thumb_making.job);
	(void)close(fd);
	if (error != 0) {
		(void)unlink(thumb_making.temporary);
		return error;
	}

	/* Followed from now on. */
	thumb_making.running = 1;
	thumb_making.thumb = thumb;
	thumb->pending = 1;
	return 0;
}

/*
 * Looks at the child: when it has ended, its record is put in place and
 * read into the slot, or the slot is marked failed.  Returns nonzero when
 * it ended.
 */
static int
thumb_follow(void)
{
	struct fm_thumb *thumb;
	int finished;
	int status;
	int error;

	/* Still running. */
	finished = preview_poll(&thumb_making.job);
	if (!finished)
		return 0;

	/* Ended: the record in place and read, or nothing. */
	thumb_making.running = 0;
	thumb = thumb_making.thumb;
	thumb->pending = 0;
	status = thumb_making.job.status;
	error = EINVAL;
	if (status == PREVIEW_OK) {
		error = rename(thumb_making.temporary, thumb_making.record);
		if (error == 0)
			error = fm_thumb_cache_read(thumb->path, &thumb->image);
		if (error == 0)
			(void)fm_thumb_cache_trim(FM_THUMB_RECORDS_MAX, FM_THUMB_RECORDS_KEEP);
	}

	/* A failure leaves nothing, and the file is not tried again until it changes. */
	if (error != 0) {
		(void)unlink(thumb_making.temporary);
		thumb->failed = 1;
	}

	/* The log line the tests wait for. */
	fm_log("THUMB path=%s error=%d width=%d height=%d cached=0 status=%d pid=%ld", thumb->path, error, thumb->image.width, thumb->image.height, status,
	    (long)thumb_making.job.pid);
	return 1;
}

/* Has keiland-preview make a picture within a size and reads it into an image; 0 or an errno value. */
static int
thumb_picture(
	const char *path,
	int width,
	int height,
	struct kl_image *image)
{
	struct preview_request request;
	struct preview_picture picture;
	int error;

	/* The picture, waited for. */
	memset(image, 0, sizeof(*image));
	memset(&request, 0, sizeof(request));
	request.width = width;
	request.height = height;
	error = preview_picture(path, &request, &picture);
	if (error != 0)
		return error;

	/* The image takes its pixels (no padding between the rows). */
	image->pixels = picture.pixels;
	image->width = picture.width;
	image->height = picture.height;
	image->stride = (size_t)picture.width;
	return 0;
}

/* Finds the kept thumbnail of a file as it is now; NULL when there is none. */
static struct fm_thumb *
thumb_find(
	struct fm_app *app,
	const char *path,
	time_t modified)
{
	struct fm_thumb *thumb;
	int index;
	int match;

	/* Each slot in use, for the same path and the same modification time. */
	for (index = 0; index < FM_THUMBS; index++) {
		thumb = &app->thumbs[index];
		if (thumb->path[0] == '\0')
			continue;
		if (thumb->modified != modified)
			continue;

		/* The same file. */
		match = strcmp(thumb->path, path);
		if (match == 0)
			return thumb;
	}

	/* No slot holds it. */
	return NULL;
}

/* Chooses the slot a new thumbnail goes in: a free one, else the least recently drawn (never the one being made). */
static struct fm_thumb *
thumb_slot(
	struct fm_app *app)
{
	int oldest;
	int index;

	/* A free slot, or the one drawn longest ago (a free slot was never drawn, so it is the oldest). */
	oldest = 0;
	if (app->thumbs[0].pending != 0)
		oldest = 1;
	for (index = oldest + 1; index < FM_THUMBS; index++) {
		if (app->thumbs[index].pending != 0)
			continue;
		if (app->thumbs[index].used < app->thumbs[oldest].used)
			oldest = index;
	}

	/* Reports the slot. */
	return &app->thumbs[oldest];
}
