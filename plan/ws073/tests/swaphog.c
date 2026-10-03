/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws073-p046 (BUG-053): touches more anonymous memory than the guest has
 * RAM, so that the kernel pages it out to swap, and checks it all back.
 *
 *   swaphog MIB [STAT_SECONDS]   write MIB MiB a page at a time, then read every page back
 *   swaphog stat                 print one line of the kernel's VM statistics
 *
 * Every page gets a pattern made of its own number, so a page that comes
 * back from swap with another page's contents, or zeros, is counted bad.
 * During both passes a line of statistics is printed every STAT_SECONDS
 * (default 10): the elapsed time, the pass and its progress, free memory,
 * resident, swapped, page-ins and page-outs, free swap and the commit.
 * The last line is
 *   SWAPHOG mib=N bad=B write_s=W verify_s=V total_s=T
 * and the exit status is 0 when B is 0.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <uapi/system.h>

#define HOG_PAGE 4096UL

static double hog_now(void);
static void hog_stat(const char *pass, size_t done, size_t total, double start);

/* The descriptor of /dev/system, or -1 when it could not be opened. */
static int hog_system = -1;

int
main(
	int argc,
	char **argv)
{
	unsigned long mib;
	double interval;
	double start;
	double last;
	double written;
	double verified;
	double now;
	size_t pages;
	size_t page;
	size_t word;
	size_t bad;
	unsigned long *base;
	unsigned long *cell;

	hog_system = open("/dev/system", O_RDONLY);

	/* A statistics line alone. */
	if (argc >= 2 && strcmp(argv[1], "stat") == 0) {
		hog_stat("stat", 0, 0, hog_now());
		return 0;
	}

	if (argc < 2) {
		fprintf(stderr, "usage: swaphog MIB [STAT_SECONDS] | swaphog stat\n");
		return 2;
	}
	mib = strtoul(argv[1], NULL, 10);
	interval = 10.0;
	if (argc >= 3)
		interval = (double)strtoul(argv[2], NULL, 10);
	pages = (size_t)mib * (1024UL * 1024UL / HOG_PAGE);

	/* The memory, private and anonymous. */
	base = mmap(NULL, pages * HOG_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (base == MAP_FAILED) {
		printf("SWAPHOG mmap failed: %s\n", strerror(errno));
		return 1;
	}

	/* The write pass: every word of a page carries the page number and the word. */
	start = hog_now();
	last = start;
	hog_stat("write", 0, pages, start);
	for (page = 0; page < pages; page++) {
		cell = base + page * (HOG_PAGE / sizeof(*base));
		for (word = 0; word < HOG_PAGE / sizeof(*base); word += 64)
			cell[word] = (page << 12) ^ word ^ 0x5a5a5a5aUL;
		now = hog_now();
		if (now - last >= interval) {
			hog_stat("write", page + 1, pages, start);
			last = now;
		}
	}
	written = hog_now() - start;
	hog_stat("write-done", pages, pages, start);

	/* The verify pass. */
	bad = 0;
	for (page = 0; page < pages; page++) {
		cell = base + page * (HOG_PAGE / sizeof(*base));
		for (word = 0; word < HOG_PAGE / sizeof(*base); word += 64) {
			if (cell[word] != ((page << 12) ^ word ^ 0x5a5a5a5aUL)) {
				bad++;
				break;
			}
		}
		now = hog_now();
		if (now - last >= interval) {
			hog_stat("verify", page + 1, pages, start);
			last = now;
		}
	}
	verified = hog_now() - start - written;
	hog_stat("verify-done", pages, pages, start);

	printf("SWAPHOG mib=%lu bad=%lu write_s=%.1f verify_s=%.1f total_s=%.1f\n",
	    mib, (unsigned long)bad, written, verified, written + verified);
	fflush(stdout);
	if (bad != 0)
		return 1;
	return 0;
}

/* Returns the monotonic time in seconds. */
static double
hog_now(
	void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

/* Prints one line of progress and VM statistics. */
static void
hog_stat(
	const char *pass,
	size_t done,
	size_t total,
	double start)
{
	struct vm_statistics vm;

	memset(&vm, 0, sizeof(vm));
	if (hog_system >= 0)
		(void)ioctl(hog_system, KERN_SYSTEM_GET_VMSTAT, &vm);
	printf("STAT t=%.1f %s %lu/%lu free_mib=%llu resident=%llu swapped=%llu page_in=%llu page_out=%llu "
	    "reclaims=%llu io_err=%llu swap_free=%llu/%llu commit_used_mib=%llu/%llu\n",
	    hog_now() - start, pass, (unsigned long)done, (unsigned long)total,
	    (unsigned long long)(vm.physical_free >> 20),
	    (unsigned long long)vm.vm_resident, (unsigned long long)vm.vm_swapped,
	    (unsigned long long)vm.vm_page_in, (unsigned long long)vm.vm_page_out,
	    (unsigned long long)vm.vm_reclaims, (unsigned long long)vm.vm_io_errors,
	    (unsigned long long)vm.swap_free, (unsigned long long)vm.swap_total,
	    (unsigned long long)(vm.vm_commit_used >> 20), (unsigned long long)(vm.vm_commit_limit >> 20));
	fflush(stdout);
}
