#!/bin/sh
# ws073-p046 (BUG-053): the host test of a region's page index (region_page_index_rebuild, _insert, _remove, _free
# and find_page of src/kern/vmspace.c).  The functions' text is cut out of vmspace.c and compiled with the real
# struct vm_region and struct vm_page against an allocator that fails on demand:
#  - pages added in random order are all found, removed ones are not, with the index or the list;
#  - while kern_calloc fails, the old index stays and keeps every page (insert, find, remove), and a larger index is
#    tried again only once the pages have doubled (the rebuilds are O(log n), not one a fault);
#  - the buckets never exceed VM_REGION_INDEX_MAXIMUM;
#  - a fresh rebuild (keep 0, as after a split) that fails leaves no index, and the list finds every page.
# Last line: region-index-host: PASS.
#   sh plan/ws073/tests/region-index-host.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws073-host/region-index
rm -rf "$out"
mkdir -p "$out"
for name in find_page region_page_index_rebuild region_page_index_insert region_page_index_remove region_page_index_free; do
	awk -v name="$name" '$0 ~ "^"name"\\(" {p=1; print previous} p{print} p&&/^}/{exit} {previous=$0}' src/kern/vmspace.c
done > "$out/index.inc"
grep -E '^#define (PAGE_SIZE|VM_REGION_INDEX_(MINIMUM|MAXIMUM))[[:space:]]' src/kern/vmspace.c > "$out/defines.inc"
cat > "$out/main.c" <<'EOT'
#include "kern/vmspace.h"
#include "kern/page.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defines.inc"

static int calloc_fail;
static unsigned long callocs;
static unsigned long largest;

static void *
kern_calloc(size_t count, size_t size)
{
	callocs++;
	if (count > largest)
		largest = count;
	if (calloc_fail)
		return NULL;
	return calloc(count, size);
}

static void
kern_free(void *pointer)
{
	free(pointer);
}

#undef HAL_FATAL
#define HAL_FATAL(message) do { printf("FATAL %s\n", message); exit(2); } while (0)

static void region_page_index_free(struct vm_region *region);
#include "index.inc"

static int failures;

static void
check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

static struct vm_page *
add(struct vm_region *region, uintptr_t address)
{
	struct vm_page *page;

	page = calloc(1, sizeof(*page));
	page->address = address;
	page->next = region->pages;
	region->pages = page;
	region_page_index_insert(region, page);
	return page;
}

static void
drop(struct vm_region *region, struct vm_page *page)
{
	struct vm_page **link;

	region_page_index_remove(region, page);
	for (link = &region->pages; *link != page; link = &(*link)->next)
		;
	*link = page->next;
	free(page);
}

/* Removes every page whose address' page number is a multiple of step, in one walk of the list. */
static void
drop_every(struct vm_region *region, size_t step)
{
	struct vm_page **link;
	struct vm_page *page;

	link = &region->pages;
	while (*link != NULL) {
		page = *link;
		if (((page->address - 0x400000U) / PAGE_SIZE) % step != 0U) {
			link = &page->next;
			continue;
		}
		region_page_index_remove(region, page);
		*link = page->next;
		free(page);
	}
}

static int
all_found(struct vm_region *region)
{
	struct vm_page *page;

	for (page = region->pages; page != NULL; page = page->next) {
		if (find_page(region, page->address + 5U) != page)
			return 0;
	}
	return 1;
}

int
main(void)
{
	static struct vm_page *pages[700000];
	static size_t order[700000];
	size_t swap;
	struct vm_region region;
	unsigned long before;
	size_t count;
	size_t index;
	size_t at;

	srand(53046);

	/* 1. 600000 pages in a shuffled order: every page found, the buckets at most the maximum. */
	memset(&region, 0, sizeof(region));
	count = 600000;
	for (index = 0; index < count; index++)
		order[index] = index;
	for (index = count - 1U; index > 0U; index--) {
		at = ((size_t)rand() * 32768U + (size_t)rand()) % (index + 1U);
		swap = order[index];
		order[index] = order[at];
		order[at] = swap;
	}
	for (index = 0; index < count; index++) {
		at = order[index];
		pages[at] = add(&region, 0x400000U + at * PAGE_SIZE);
	}
	check(pages[order[0]] != NULL, "1: the pages made");
	check(region.page_count == count, "1: the count");
	check(region.page_index != NULL && region.page_index_size == VM_REGION_INDEX_MAXIMUM, "1: the index at its maximum");
	check(largest <= VM_REGION_INDEX_MAXIMUM, "1: no larger index was asked for");
	check(all_found(&region), "1: every page found");
	check(find_page(&region, 0x400000U + count * PAGE_SIZE) == NULL, "1: a page past the end is not found");
	drop_every(&region, 3U);
	check(all_found(&region), "1: the rest found after removals");
	check(find_page(&region, 0x400000U) == NULL, "1: a removed page is not found");
	while (region.pages != NULL)
		drop(&region, region.pages);
	region_page_index_free(&region);

	/* 2. The memory runs out at 1000 pages: the old index stays and holds every page, few tries. */
	memset(&region, 0, sizeof(region));
	for (index = 0; index < 1000U; index++)
		pages[index] = add(&region, 0x400000U + index * PAGE_SIZE);
	check(region.page_index != NULL, "2: an index before the failure");
	calloc_fail = 1;
	before = callocs;
	for (index = 1000U; index < 64000U; index++)
		pages[index] = add(&region, 0x400000U + index * PAGE_SIZE);
	check(region.page_index != NULL, "2: the old index stays while memory is short");
	check(callocs - before <= 8U, "2: a larger index was tried O(log n) times, not each insert");
	check(all_found(&region), "2: every page found through the old index");
	drop_every(&region, 2U);
	check(all_found(&region), "2: the rest found after removals while short");
	calloc_fail = 0;
	for (index = 64000U; index < 200000U; index++)
		pages[index] = add(&region, 0x400000U + index * PAGE_SIZE);
	check(region.page_index_size >= 65536U, "2: a larger index once memory is back and the pages have doubled");
	check(all_found(&region), "2: every page found after the memory came back");
	while (region.pages != NULL)
		drop(&region, region.pages);
	region_page_index_free(&region);

	/* 3. A fresh rebuild that fails (after a split) leaves the list, which finds every page. */
	memset(&region, 0, sizeof(region));
	for (index = 0; index < 500U; index++)
		pages[index] = add(&region, 0x400000U + index * PAGE_SIZE);
	calloc_fail = 1;
	check(region_page_index_rebuild(&region, 0) == 0, "3: the fresh rebuild fails");
	check(region.page_index == NULL, "3: no index is left");
	check(all_found(&region), "3: the list finds every page");
	calloc_fail = 0;
	while (region.pages != NULL)
		drop(&region, region.pages);

	if (failures != 0) {
		printf("region-index-host: FAIL (%d)\n", failures);
		return 1;
	}
	printf("region-index-host: PASS\n");
	return 0;
}
EOT
cc -std=gnu99 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -I. -Iinclude -Isrc -I"$out" \
    -fsanitize=address,undefined "$out/main.c" -o "$out/region-index-host"
exec "$out/region-index-host"
