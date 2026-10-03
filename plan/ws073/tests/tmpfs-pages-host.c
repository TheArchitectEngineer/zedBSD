/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws073-p047 (BUG-052): the host test of the tmpfs page index
 * (src/kern/tmpfs-pages.c).  The physical allocator is a host stub that
 * counts the live pages.  The cases: dense and sparse inserts with lookups
 * checked against a reference, holes, EEXIST, page numbers near the 64-bit
 * file limit, truncation at and inside table boundaries, and a final
 * truncation to zero that must return every page (no leak).
 * Prints "tmpfs-pages-host: PASS" or FAIL.
 *
 *   sh plan/ws073/tests/tmpfs-pages-host.sh
 */

#include "kern/tmpfs-pages.h"
#include "kern/pmem.h"
#include <kern/kcrt.h>
#include <uapi/errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The pages the stub allocator has handed out and not taken back. */
static long live_pages;

/* The cases that failed. */
static int failures;

/* The kernel's pieces the index calls. */
int
kern_pmem_alloc(size_t size, size_t alignment, struct kern_pmem *run)
{
	void *page;

	if (posix_memalign(&page, alignment, size) != 0)
		return ENOMEM;
	memset(page, 0xa5, size);
	run->paddr = (hal_physaddr_t)page;
	run->size = size;
	live_pages++;
	return 0;
}

int
kern_pmem_free(struct kern_pmem *run)
{
	free((void *)run->paddr);
	live_pages--;
	return 0;
}

void *
kern_pmem_to_kernel(hal_physaddr_t address)
{
	return (void *)address;
}

void
hal_fatal(const char *file, int line, const char *message)
{
	fprintf(stderr, "fatal %s:%d %s\n", file, line, message);
	abort();
}

static void
check(const char *what, int passed)
{
	if (!passed) {
		printf("%s: FAIL\n", what);
		failures++;
	}
}

/* Inserts a data page that carries its own page number. */
static int
put(struct tmpfs_pages *pages, uint64_t index)
{
	struct kern_pmem run;
	int error;

	error = kern_pmem_alloc(4096, 4096, &run);
	if (error != 0)
		return error;
	memcpy((void *)run.paddr, &index, sizeof(index));
	error = tmpfs_pages_insert(pages, index, run.paddr);
	if (error != 0)
		(void)kern_pmem_free(&run);
	return error;
}

/* Reports whether the page at a number is present and carries that number. */
static int
present(struct tmpfs_pages *pages, uint64_t index)
{
	uint64_t stored;
	void *data;

	data = tmpfs_pages_lookup(pages, index);
	if (data == NULL)
		return 0;
	memcpy(&stored, data, sizeof(stored));
	return stored == index;
}

#define DENSE 3000U

int
main(void)
{
	static const uint64_t sparse[] = {
		511U, 512U, 513U, 262143U, 262144U, 1000000U,
		(uint64_t)1 << 30, ((uint64_t)1 << 40) + 7U, ((uint64_t)1 << 51) - 1U
	};
	struct tmpfs_pages pages;
	char what[128];
	uint64_t index;
	size_t count;
	size_t freed;
	unsigned i;
	int ok;

	tmpfs_pages_init(&pages);
	check("empty lookup", tmpfs_pages_lookup(&pages, 0) == NULL);
	check("empty truncate", tmpfs_pages_truncate(&pages, 0) == 0);

	/* Dense: 0..DENSE-1 in an interleaved order. */
	for (i = 0; i < DENSE; i++) {
		index = (i * 7U) % DENSE;
		snprintf(what, sizeof(what), "dense insert %llu", (unsigned long long)index);
		check(what, put(&pages, index) == 0);
	}
	ok = 1;
	for (i = 0; i < DENSE; i++)
		if (!present(&pages, i))
			ok = 0;
	check("dense lookups", ok);
	check("dense count", pages.count == DENSE);
	check("hole past the end", tmpfs_pages_lookup(&pages, DENSE) == NULL);
	check("EEXIST", put(&pages, 5U) == EEXIST);

	/* Sparse, including the largest page number of a 64-bit offset. */
	for (i = 0; i < sizeof(sparse) / sizeof(sparse[0]); i++) {
		if (sparse[i] < DENSE)
			continue;
		snprintf(what, sizeof(what), "sparse insert %llu", (unsigned long long)sparse[i]);
		check(what, put(&pages, sparse[i]) == 0);
	}
	for (i = 0; i < sizeof(sparse) / sizeof(sparse[0]); i++) {
		snprintf(what, sizeof(what), "sparse lookup %llu", (unsigned long long)sparse[i]);
		check(what, present(&pages, sparse[i]));
	}
	check("sparse hole", tmpfs_pages_lookup(&pages, ((uint64_t)1 << 40) + 6U) == NULL);
	check("hole of a missing middle table", tmpfs_pages_lookup(&pages, (uint64_t)1 << 35) == NULL);

	/* Truncation inside a level-one table, then at a table boundary. */
	count = pages.count;
	freed = tmpfs_pages_truncate(&pages, 1000000U);
	check("truncate 1000000 frees the 4 pages at and above", freed == 4U && pages.count == count - 4U);
	check("1000000 gone", tmpfs_pages_lookup(&pages, 1000000U) == NULL);
	check("262144 kept", present(&pages, 262144U));
	freed = tmpfs_pages_truncate(&pages, 2049U);
	check("truncate 2049", freed == (DENSE - 2049U) + 2U);
	ok = 1;
	for (i = 0; i < 2049U; i++)
		if (!present(&pages, i))
			ok = 0;
	check("pages below 2049 kept", ok);
	check("2049 gone", tmpfs_pages_lookup(&pages, 2049U) == NULL);
	freed = tmpfs_pages_truncate(&pages, 1024U);
	check("truncate at a table boundary", freed == 1025U && pages.count == 1024U);
	check("1023 kept", present(&pages, 1023U));
	check("re-insert after truncate", put(&pages, 1500U) == 0 && present(&pages, 1500U));

	/* Truncation to zero returns every page and table. */
	freed = tmpfs_pages_truncate(&pages, 0);
	check("truncate 0 frees all", freed == 1025U && pages.count == 0 && pages.height == 0 && pages.root == 0);
	snprintf(what, sizeof(what), "no leak (live pages %ld)", live_pages);
	check(what, live_pages == 0);

	/* A tree emptied by truncation can grow again. */
	check("insert after empty", put(&pages, ((uint64_t)1 << 51) - 1U) == 0);
	(void)tmpfs_pages_truncate(&pages, 0);
	check("no leak at the end", live_pages == 0);

	if (failures != 0) {
		printf("tmpfs-pages-host: FAIL (%d)\n", failures);
		return 1;
	}
	printf("tmpfs-pages-host: PASS\n");
	return 0;
}
