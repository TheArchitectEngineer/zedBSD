#!/bin/sh
# Host test of the user copy's pin (ws213-p007 K-2 (b)).
# copyin and copyout are cut out of src/kern/uaccess.c, and the copy, the pin,
# the range check and the readiness test out of src/kern/vmspace.c, and
# compiled with the real struct vmspace, struct vm_region and struct vm_page
# against a fake fault, fake backings and a counting metadata lock:
#  - a copy of a few ready pages enters the metadata lock once and faults
#    nothing, and its bytes arrive;
#  - a range whose later half is unmapped, or not writable for a copyout,
#    fails with EFAULT before any page is faulted or written;
#  - pages that are not ready are faulted in once each, then pinned;
#  - a copy longer than the stack array checks its range first: two entries
#    when ready, EFAULT with no allocation and no fault when not mapped;
#  - a copy-on-write page is faulted before a write pin;
#  - a page the fault leaves not ready is still pinned on the next pass
#    (no fault loop), and a fault's ENOMEM is reported as ENOMEM.
# Usage: plan/ws213/tests/pin-host-test.sh [build-dir]  (from the tree root)
# Last line: pin-host-test: PASS.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
out=${1:-build/ws213-p007-host}
mkdir -p "$out"

# Cuts a function (its return-type line and its body) out of a source file.
cut_function() {
	awk -v name="$2" '$0 ~ "^"name"\\(" {p=1; print previous} p{print} p&&/^}/{exit} {previous=$0}' "$1"
}

grep -E '^#define (PAGE_SIZE|VM_COPY_STACK_PAGES)[[:space:]]' src/kern/vmspace.c > "$out/pin-defines.inc"
{
	for name in vmspace_check_locked vmspace_pin_mapping_ready vmspace_pin_mapping_ready_locked \
		vmspace_pin_first_unready_locked vmspace_pin_fault_from copy_backing vmspace_copy_to_locked \
		vmspace_copy_from_locked find_region_locked find_page; do
		grep -E "^static [a-z_ ]+\*? ?$name\(" src/kern/vmspace.c
	done
	for name in vmspace_check vmspace_pin_user_pages vmspace_unpin_user_pages vmspace_copy_to vmspace_copy_from \
		vmspace_check_locked vmspace_pin_mapping_ready vmspace_pin_mapping_ready_locked \
		vmspace_pin_first_unready_locked vmspace_pin_fault_from copy_backing vmspace_copy_to_locked \
		vmspace_copy_from_locked find_region_locked find_page; do
		cut_function src/kern/vmspace.c "$name"
	done
	for name in copyin copyout; do
		cut_function src/kern/uaccess.c "$name"
	done
} > "$out/pin-functions.inc"

cat > "$out/pin-main.c" <<'EOT'
#include "kern/vmspace.h"
#include "kern/vm-object.h"
#include "kern/vm-device.h"
#include "kern/vm-lock.h"
#include "kern/uaccess.h"
#include "kern/kmem.h"
#include "kern/page.h"
#include <kern/kcrt.h>
#include <uapi/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pin-defines.inc"

struct vmspace kernel_vmspace;
static struct vmspace test_vm;
static unsigned metadata_entries;
static unsigned faults;
static unsigned callocs;
static int fault_enomem;
static int fault_stubborn;
static void (*vmspace_pin_page_checkpoint)(struct vmspace *vm, size_t index, size_t page_count);

void vm_metadata_enter(void) { metadata_entries++; }
void vm_metadata_leave(void) { }
void mutex_lock(struct mutex *mutex) { (void)mutex; }
void mutex_unlock(struct mutex *mutex) { (void)mutex; }
unsigned long spin_lock_irqsave(struct spinlock *lock) { (void)lock; return 0; }
void spin_unlock_irqrestore(struct spinlock *lock, unsigned long enabled) { (void)lock; (void)enabled; }
uint64_t waitq_sequence(const struct wait_queue *queue) { return queue->sequence; }
void *kern_calloc(size_t count, size_t size) { callocs++; return calloc(count, size); }
void kern_free(void *pointer) { free(pointer); }
void *hal_pmem_to_kernel(hal_physaddr_t paddr) { return (void *)(uintptr_t)paddr; }

int
vmspace_user_range_valid(uintptr_t start, size_t size)
{
	if (start < 0x1000U || size == 0 || start > 0x7fffffffU - size)
		return 0;
	return 1;
}

int
vm_private_page_is_resident(const struct vm_page *page)
{
	if (page == NULL || page->private_page == NULL)
		return 0;
	return (page->private_page->flags & VM_PAGE_RESIDENT) != 0;
}

int
vm_private_page_pin(struct vm_private_page *backing, struct kern_pmem *memory)
{
	backing->pin_count++;
	*memory = backing->pmem;
	return 0;
}

void vm_private_page_unpin(struct vm_private_page *backing) { backing->pin_count--; }
void vm_private_page_mark_dirty(struct vm_private_page *backing) { backing->generation++; }
void vm_private_page_ref(struct vm_private_page *backing) { (void)backing; }
void vm_private_page_put(struct vm_private_page *backing) { (void)backing; }
int vm_private_page_wait_idle(struct vm_private_page *backing) { (void)backing; return 0; }
int vm_object_page_pin(struct vm_object_page *page) { (void)page; return EFAULT; }
void vm_object_page_unpin(struct vm_object_page *page) { (void)page; }
int vm_object_page_pin_read(struct vm_object_page *page, size_t offset, void *buffer, size_t length)
{ (void)page; (void)offset; (void)buffer; (void)length; return EFAULT; }
int vm_object_page_pin_write(struct vm_object_page *page, size_t offset, const void *buffer, size_t length)
{ (void)page; (void)offset; (void)buffer; (void)length; return EFAULT; }
void vm_device_ref(struct vm_device_mapping *mapping) { (void)mapping; }
void vm_device_put(struct vm_device_mapping *mapping) { (void)mapping; }
int vm_device_read(struct vm_device_mapping *mapping, size_t offset, void *destination, size_t bytes)
{ (void)mapping; (void)offset; (void)destination; (void)bytes; return EFAULT; }
int vm_device_write(struct vm_device_mapping *mapping, size_t offset, const void *source, size_t bytes)
{ (void)mapping; (void)offset; (void)source; (void)bytes; return EFAULT; }

static void
vmspace_wait_fault_event(struct vmspace *vm, uint64_t sequence)
{
	(void)vm;
	(void)sequence;
}

static struct vmspace *
current_vmspace(void)
{
	return &test_vm;
}

/* The fake fault: maps the page, makes its backing resident and breaks copy-on-write. */
int
vmspace_fault(struct vmspace *vm, uintptr_t address, uint32_t required)
{
	struct vm_region *region;
	struct vm_page *page;

	(void)required;
	faults++;
	if (faults > 100U) {
		printf("FAIL the pin faults the same page forever\n");
		exit(1);
	}
	if (fault_enomem)
		return ENOMEM;
	for (region = vm->regions; region != NULL; region = region->next) {
		if (address < region->start || address >= region->start + region->size)
			continue;
		for (page = region->pages; page != NULL; page = page->next) {
			if (page->address != (address & ~(uintptr_t)(PAGE_SIZE - 1U)))
				continue;
			if (fault_stubborn)
				return 0;
			page->flags |= VM_MAPPING_MAPPED;
			page->flags &= ~VM_MAPPING_COW;
			page->private_page->flags |= VM_PAGE_RESIDENT;
			return 0;
		}
	}
	return EFAULT;
}

#include "pin-functions.inc"

#define REGION_PAGES_MAX 8

static struct vm_region regions[3];
static struct vm_page pages[3][REGION_PAGES_MAX];
static struct vm_private_page backings[3][REGION_PAGES_MAX];
static unsigned char memory[3][REGION_PAGES_MAX][4096] __attribute__((aligned(4096)));
static int failures;

static void
check(int condition, const char *what)
{
	if (!condition) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

/* Builds one region of private pages, ready (mapped and resident) or not. */
static void
make_region(unsigned slot, uintptr_t start, size_t count, uint32_t prot, int ready)
{
	struct vm_region *region = &regions[slot];
	size_t index;

	memset(region, 0, sizeof(*region));
	region->start = start;
	region->size = count * PAGE_SIZE;
	region->prot = prot;
	region->max_prot = prot;
	for (index = count; index > 0; index--) {
		struct vm_page *page = &pages[slot][index - 1U];
		struct vm_private_page *backing = &backings[slot][index - 1U];

		memset(page, 0, sizeof(*page));
		memset(backing, 0, sizeof(*backing));
		memset(memory[slot][index - 1U], 0, sizeof(memory[slot][index - 1U]));
		backing->pmem.paddr = (hal_physaddr_t)(uintptr_t)memory[slot][index - 1U];
		backing->pmem.size = PAGE_SIZE;
		page->address = start + (index - 1U) * PAGE_SIZE;
		page->vm = &test_vm;
		page->region = region;
		page->private_page = backing;
		if (ready) {
			page->flags = VM_MAPPING_MAPPED;
			backing->flags = VM_PAGE_RESIDENT;
		}
		page->next = region->pages;
		region->pages = page;
	}
	region->page_count = count;
}

/* Starts an address space of the given regions, in address order. */
static void
make_space(unsigned count)
{
	unsigned slot;

	memset(&test_vm, 0, sizeof(test_vm));
	test_vm.fault_waitq.sequence = 1;
	test_vm.regions = NULL;
	for (slot = count; slot > 0; slot--) {
		regions[slot - 1U].next = test_vm.regions;
		test_vm.regions = &regions[slot - 1U];
	}
	metadata_entries = 0;
	faults = 0;
	callocs = 0;
	fault_enomem = 0;
	fault_stubborn = 0;
}

static unsigned
pins_held(void)
{
	unsigned slot;
	unsigned index;
	unsigned held = 0;

	for (slot = 0; slot < 3; slot++)
		for (index = 0; index < REGION_PAGES_MAX; index++)
			held += backings[slot][index].pin_count;
	return held;
}

int
main(void)
{
	unsigned char buffer[8 * 4096];
	unsigned char readback[8 * 4096];
	size_t index;
	int error;
	int written;

	const uint32_t rw = HAL_SPACE_READ | HAL_SPACE_WRITE;

	for (index = 0; index < sizeof(buffer); index++)
		buffer[index] = (unsigned char)(index * 7U + 1U);

	/* A ready two-page copyout: one entry, no fault, the bytes arrive. */
	make_region(0, 0x10000U, 2, rw, 1);
	make_space(1);
	error = copyout(buffer, 0x10000U + 100U, 5000U);
	check(error == 0, "ready: copyout succeeds");
	check(metadata_entries == 1, "ready: one metadata entry");
	check(faults == 0, "ready: no fault");
	check(callocs == 0, "ready: no allocation");
	check(memcmp(&memory[0][0][100], buffer, 4096U - 100U) == 0, "ready: first page bytes");
	check(memcmp(&memory[0][1][0], buffer + 4096U - 100U, 5000U - (4096U - 100U)) == 0, "ready: second page bytes");
	check(pins_held() == 0, "ready: pins released");
	error = copyin(0x10000U + 100U, readback, 5000U);
	check(error == 0 && memcmp(readback, buffer, 5000U) == 0, "ready: copyin reads them back");
	check(metadata_entries == 2, "ready: copyin is one more entry");

	/* The later half unmapped: EFAULT before any fault or write. */
	make_region(0, 0x10000U, 1, rw, 0);
	make_space(1);
	error = copyout(buffer, 0x10000U, 2U * 4096U);
	check(error == EFAULT, "half unmapped: EFAULT");
	check(faults == 0, "half unmapped: no fault");
	check(memory[0][0][0] == 0, "half unmapped: nothing written");
	check(metadata_entries == 1, "half unmapped: one entry");
	error = copyin(0x10000U, readback, 2U * 4096U);
	check(error == EFAULT && faults == 0, "half unmapped: copyin EFAULT, no fault");

	/* The later half read-only: a copyout fails with EFAULT and faults nothing. */
	make_region(0, 0x10000U, 1, rw, 0);
	make_region(1, 0x11000U, 1, HAL_SPACE_READ, 0);
	make_space(2);
	error = copyout(buffer, 0x10000U + 10U, 4096U);
	check(error == EFAULT, "half read-only: EFAULT");
	check(faults == 0, "half read-only: no fault");
	written = memory[0][0][10] != 0;
	check(!written, "half read-only: nothing written");

	/* Pages not ready are faulted once each, then pinned. */
	make_region(0, 0x10000U, 3, rw, 0);
	make_space(1);
	error = copyout(buffer, 0x10000U, 3U * 4096U);
	check(error == 0, "unready: copyout succeeds");
	check(faults == 3, "unready: three faults");
	check(memcmp(memory[0][2], buffer + 2U * 4096U, 4096U) == 0, "unready: bytes arrive");
	check(pins_held() == 0, "unready: pins released");
	make_space(1);
	error = copyout(buffer, 0x10000U, 3U * 4096U);
	check(error == 0 && faults == 0 && metadata_entries == 1, "unready: next copy is one entry");

	/* A longer copy: two entries when ready; EFAULT with no allocation when not mapped. */
	make_region(0, 0x20000U, 6, rw, 1);
	make_space(1);
	error = copyout(buffer, 0x20000U, 6U * 4096U);
	check(error == 0, "long: copyout succeeds");
	check(metadata_entries == 2, "long: two entries (check, pin)");
	check(callocs == 1, "long: one allocation");
	check(memcmp(memory[0][5], buffer + 5U * 4096U, 4096U) == 0, "long: bytes arrive");
	make_space(1);
	error = copyout(buffer, 0x20000U, 7U * 4096U);
	check(error == EFAULT, "long unmapped: EFAULT");
	check(callocs == 0, "long unmapped: no allocation");
	check(faults == 0, "long unmapped: no fault");

	/* A copy-on-write page is faulted before the write pin. */
	make_region(0, 0x10000U, 1, rw, 1);
	make_space(1);
	pages[0][0].flags |= VM_MAPPING_COW;
	error = copyout(buffer, 0x10000U, 16U);
	check(error == 0 && faults == 1, "cow: one fault, then the copy");
	check((pages[0][0].flags & VM_MAPPING_COW) == 0, "cow: broken");

	/* A page the fault leaves unready is pinned on the next pass, not faulted forever. */
	make_region(0, 0x10000U, 1, rw, 0);
	make_space(1);
	backings[0][0].flags = VM_PAGE_RESIDENT;
	fault_stubborn = 1;
	error = copyout(buffer, 0x10000U, 16U);
	check(error == 0, "stubborn: pinned after one fault");
	check(faults == 1, "stubborn: one fault");

	/* A fault's ENOMEM is reported as ENOMEM. */
	make_region(0, 0x10000U, 1, rw, 0);
	make_space(1);
	fault_enomem = 1;
	error = copyin(0x10000U, readback, 16U);
	check(error == ENOMEM, "enomem: reported");

	if (failures != 0) {
		printf("pin-host-test: FAIL (%d)\n", failures);
		return 1;
	}
	printf("pin-host-test: PASS\n");
	return 0;
}
EOT

# The kernel's own ABI headers (KERN_UAPI_NATIVE), plainly and with ASan and UBSan.
for mode in plain sanitize; do
	extra=
	if [ "$mode" = sanitize ]; then
		extra='-fsanitize=address,undefined -fno-omit-frame-pointer'
	fi
	# shellcheck disable=SC2086
	cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -DKERN_USER_ABI_LP64 -DKERN_UAPI_NATIVE \
		-I. -Iinclude -Iinclude/libc -Iinclude/uapi -Isrc -I"$out" $extra "$out/pin-main.c" -o "$out/pin-host-test-$mode"
	UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$out/pin-host-test-$mode"
done
