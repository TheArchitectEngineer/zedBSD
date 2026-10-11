/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of preview_info_read (ws157-p013, p016's zone): the reader of
 * keiland-preview --info's answer, against a stand-in child (a script in
 * the folder KEILAND_LIBEXECDIR names) that answers what the case file
 * says: a good answer, answers that are not --info's, a failure status,
 * a child that never ends (its time is up), and the real child's answers
 * (run-host-preview-info.sh puts the real one in place for those).
 *
 *   host-preview-info CASE INPUT
 *
 * CASE is good, utc, zone, header, kind, side, taken, status, hang or real; prints
 * one line, PASS or FAIL.
 */

#include "userland/desktop/preview/client.h"
#include "userland/desktop/picture/media-kind.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv);

/*
 * Runs one case.
 */
int
main(
	int argc,
	char **argv)
{
	struct preview_info info;
	const char *name;
	int input;
	int error;
	int same;
	int good;

	/* The case and the input. */
	if (argc != 3) {
		fprintf(stderr, "usage: host-preview-info CASE INPUT\n");
		return 2;
	}

	/* The case's name and the input. */
	name = argv[1];
	input = open(argv[2], O_RDONLY | O_CLOEXEC);
	if (input < 0) {
		printf("FAIL %s: input %s\n", name, strerror(errno));
		return 1;
	}

	/* The answer read. */
	error = preview_info_read(input, &info);
	(void)close(input);
	printf("%s: error=%d kind=%d width=%ld height=%ld dated=%d taken=%lld\n", name, error, info.kind, info.width, info.height,
	    info.dated, (long long)info.taken);

	/* What each case expects. */
	good = 0;
	same = strcmp(name, "good");
	if (same == 0 && error == 0 && info.kind == KL_MEDIA_JPEG && info.width == 640 && info.height == 480 && info.dated && info.taken == 1700000000 && !info.utc)
		good = 1;
	same = strcmp(name, "utc");
	if (same == 0 && error == 0 && info.kind == KL_MEDIA_VIDEO && info.width == 1920 && info.dated && info.taken == 1700000000 && info.utc)
		good = 1;
	same = strcmp(name, "zone");
	if (same == 0 && error == EPROTO)
		good = 1;
	same = strcmp(name, "header");
	if (same == 0 && error == EPROTO)
		good = 1;
	same = strcmp(name, "kind");
	if (same == 0 && error == EPROTO)
		good = 1;
	same = strcmp(name, "side");
	if (same == 0 && error == EPROTO)
		good = 1;
	same = strcmp(name, "taken");
	if (same == 0 && error == EPROTO)
		good = 1;
	same = strcmp(name, "status");
	if (same == 0 && error == EINVAL)
		good = 1;
	same = strcmp(name, "hang");
	if (same == 0 && error == ETIMEDOUT)
		good = 1;
	same = strcmp(name, "real");
	if (same == 0 && error == 0)
		good = 1;

	/* The verdict. */
	if (!good) {
		printf("FAIL %s\n", name);
		return 1;
	}

	/* Passed. */
	printf("PASS %s\n", name);
	return 0;
}
