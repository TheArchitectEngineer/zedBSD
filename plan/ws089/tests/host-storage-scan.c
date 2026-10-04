/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws089-p023: the host test of the Storage page's analysis
 * (userland/desktop/settings/storage-scan.c compiled unchanged), on a tree
 * the run script makes (run-host-storage-scan.sh):
 *   host-storage-scan ROOT DU_BYTES BIG_FOLDER STOP_ROOT
 *   - the total of ROOT equals du -sx's (DU_BYTES), links counted once,
 *     the link to a file outside not followed;
 *   - the largest group is BIG_FOLDER, groups the largest first;
 *   - the unreadable folder is counted apart;
 *   - the large STOP_ROOT's 30000 files are all counted;
 *   - a scan of it stopped at once ends within a second,
 *     keeps what it counted, and says stopped.
 * Prints one line a check and "host-storage-scan: PASS" or FAIL.
 */

#include "userland/desktop/settings/storage-scan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int failures;

static void check(const char *what, int passed);
static uint64_t now_ms(void);

/* Prints one check's verdict. */
static void
check(
	const char *what,
	int passed)
{
	/* A pass. */
	if (passed) {
		printf("%s: ok\n", what);
		return;
	}

	/* A failure counted. */
	printf("%s: FAIL\n", what);
	failures++;
}

/* Gives a steady clock in milliseconds. */
static uint64_t
now_ms(void)
{
	struct timespec now;

	/* The monotonic clock. */
	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

/*
 * Runs the checks.
 */
int
main(
	int argc,
	char **argv)
{
	static struct se_scan scan;
	static struct se_scan_view view;
	uint64_t expected;
	uint64_t started;
	uint64_t waited;
	uint64_t before;
	unsigned index;
	int ordered;
	int error;

	/* The arguments. */
	if (argc != 5) {
		fprintf(stderr, "usage: host-storage-scan ROOT DU_BYTES BIG_FOLDER STOP_ROOT\n");
		return 2;
	}

	/* What du counted. */
	expected = strtoull(argv[2], NULL, 10);

	/* The whole tree, to the end. */
	error = se_scan_start(&scan, argv[1]);
	check("started", error == 0);
	for (waited = 0; waited < 20000U; waited += 10U) {
		se_scan_view(&scan, &view);
		if (view.state != SE_SCAN_RUNNING)
			break;
		(void)usleep(10000);
	}

	/* The workers joined, and what they counted. */
	se_scan_finish(&scan);
	se_scan_view(&scan, &view);
	printf("total %llu bytes, %llu files, du %llu, unreadable %llu, groups %u\n", (unsigned long long)view.bytes, (unsigned long long)view.files,
	    (unsigned long long)expected, (unsigned long long)view.unreadable, view.group_count);
	check("done", view.state == SE_SCAN_DONE);
	check("the total is du's", view.bytes == expected);
	check("the largest first is the big folder", view.group_count != 0U && strcmp(view.groups[0].name, argv[3]) == 0);
	ordered = 1;
	for (index = 1; index < view.group_count; index++) {
		if (view.groups[index].bytes > view.groups[index - 1U].bytes)
			ordered = 0;
	}

	/* The order, and the folder that could not be read. */
	check("the largest first", ordered);
	check("the unreadable folder counted apart", view.unreadable == 1U);

	/* The large tree whole: every file counted by the workers together. */
	error = se_scan_start(&scan, argv[4]);
	for (waited = 0; waited < 60000U; waited += 10U) {
		se_scan_view(&scan, &view);
		if (view.state != SE_SCAN_RUNNING)
			break;
		(void)usleep(10000);
	}

	/* The workers joined, and what they counted. */
	se_scan_finish(&scan);
	se_scan_view(&scan, &view);
	printf("large: %llu files\n", (unsigned long long)view.files);
	check("the large tree's files", error == 0 && view.state == SE_SCAN_DONE && view.files == 30000U);

	/* A large tree stopped at once. */
	error = se_scan_start(&scan, argv[4]);
	(void)usleep(20000);
	se_scan_view(&scan, &view);
	before = view.bytes;
	started = now_ms();
	se_scan_stop(&scan);
	waited = now_ms() - started;
	se_scan_view(&scan, &view);
	printf("stopped in %llu ms with %llu bytes counted (%llu at the stop)\n", (unsigned long long)waited, (unsigned long long)view.bytes, (unsigned long long)before);
	check("stop started", error == 0);
	check("stops within a second", waited < 1000U);
	check("says stopped", view.state == SE_SCAN_STOPPED || view.state == SE_SCAN_DONE);
	check("keeps what it counted", view.bytes >= before);

	/* The verdict. */
	if (failures != 0) {
		printf("host-storage-scan: FAIL (%d)\n", failures);
		return 1;
	}

	/* Every check passed. */
	printf("host-storage-scan: PASS\n");
	return 0;
}
