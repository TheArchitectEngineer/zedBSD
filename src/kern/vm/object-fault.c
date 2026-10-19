/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Faults on object pages, page pins and the mappings an object records.
 */

#include "kern/vm-object.h"
#include "kern/cred.h"
#include "kern/file.h"
#include "kern/kmem.h"
#include "kern/page.h"
#include "kern/vm-reclaim.h"
#include "kern/vm-lock.h"
#include "kern/vmspace.h"
#include "kern/vm-commit.h"
#include "kern/io-pool.h"
#include "kern/disk.h"
#include "kern/cache-memory.h"
#include "kern/writeback.h"
#include <uapi/errno.h>
#include <uapi/mman.h>
#include "kern/lock.h"
#include "kern/swap.h"
#include <hal/hal.h>
#include <kern/pmem.h>
#include <limits.h>
#include "kern/sched.h"
#include "kern/clock.h"
#include "kern/thread.h"
#include "kern/process.h"
#include "kern/signal.h"
#include "kern/klog.h"
#include <uapi/signal.h>
#include <kern/kcrt.h>
#include "reclaim-internal.h"
#include "object-internal.h"

static int vm_object_page_pin_copy(struct vm_object_page *page, size_t offset, void *buffer, size_t length, int write, int nowait);

/*
 * Faults a page of an object in, returning it with a hold.
 *
 * The fault waits out resize and content transactions, retries when
 * one starts underneath it, and reads the backend outside every lock.
 */
int
vm_object_fault(
	struct vm_object *object,
	off_t offset,
	struct vm_object_page **result)
{
	struct vm_object_page *page;
	struct file_io io;
	off_t fault_size;
	uint64_t fault_generation;
	uint64_t fault_content_generation;
	size_t length;
	ssize_t count;
	unsigned long irq;
	unsigned read_retries;
	unsigned waits;
	int reclaimed;
	int owned;
	int error;
	uint64_t sequence;

	read_retries = 0;

	/* Rejects a missing object or result, or an unaligned offset. */
	if (object == NULL ||
	    result == NULL ||
	    offset < 0 ||
	    (offset & (PAGE_SIZE - 1U)) != 0)
		return EINVAL;

retry:
	/* Waits for any transaction and samples the generations. */
	irq = spin_lock_irqsave(&object->lock);

	/* Waits out any transaction, then samples what the fault is measured against. */
	while ((object->flags & (VM_OBJECT_RESIZING | VM_OBJECT_CONTENT)) != 0) {
		sequence = waitq_sequence(&object->page_waitq);
		error = waitq_sleep(&object->page_waitq, &object->lock,
		    sequence, 0, 0);
		if (error != 0 && error != EAGAIN) {
			spin_unlock_irqrestore(&object->lock, irq);
			return error;
		}
	}

	fault_generation = object->size_generation;
	fault_content_generation = object->content_generation;
	fault_size = object->logical_size;

	/* A cached page is held once it is not busy and still current. */
	page = find_page(object, offset);
	if (page != NULL) {
		while ((page->flags & VM_OBJECT_PAGE_BUSY) != 0) {
			sequence = waitq_sequence(&object->page_waitq);
			error = waitq_sleep(&object->page_waitq, &object->lock,
			    sequence, 0, 0);
			if (error != 0 && error != EAGAIN) {
				spin_unlock_irqrestore(&object->lock, irq);
				return error;
			}
		}

		if ((object->flags & (VM_OBJECT_RESIZING |
		    VM_OBJECT_CONTENT)) != 0 ||
		    object->size_generation != fault_generation ||
		    object->content_generation != fault_content_generation) {
			spin_unlock_irqrestore(&object->lock, irq);
			goto retry;
		}

		/* A page whose read failed is retried inside the file. */
		if ((page->flags & VM_OBJECT_PAGE_ERROR) != 0) {
			if (offset >= fault_size) {
				if (page->error != 0)
					error = page->error;
				else
					error = ENXIO;
				spin_unlock_irqrestore(&object->lock, irq);
				return error;
			}

			page->flags &= ~VM_OBJECT_PAGE_ERROR;
			page->flags |= VM_OBJECT_PAGE_BUSY;
			page->error = 0;
			spin_unlock_irqrestore(&object->lock, irq);
			goto read_page;
		}

		page->hold_count++;
		*result = page;
		spin_unlock_irqrestore(&object->lock, irq);
		return 0;
	}

	spin_unlock_irqrestore(&object->lock, irq);

	if (offset >= fault_size)
		return ENXIO;

	/*
	 * Allocates a new busy page, reclaiming and then waiting while memory
	 * is short.  A caller inside the metadata lock cannot wait.
	 */
	page = alloc_object_page(object, offset, 0);
	for (waits = 0; page == NULL; waits++) {
		reclaimed = vm_reclaim_one(NULL);
		if (reclaimed != 0) {
			owned = vm_metadata_owned();
			if (owned || waits >= VM_OBJECT_FAULT_WAITS)
				return ENOMEM;
			vm_reclaim_wait_free();
		}
		page = alloc_object_page(object, offset, 0);
	}

	/* Publishes it unless the world changed or another fault won. */
	irq = spin_lock_irqsave(&object->lock);

	/*
	 * Starts over when a transaction, a size change or another fault
	 * overtook this one while the page was being read.
	 */
	if ((object->flags & (VM_OBJECT_RESIZING | VM_OBJECT_CONTENT)) != 0 ||
	    object->size_generation != fault_generation ||
	    object->content_generation != fault_content_generation ||
	    find_page(object, offset) != NULL) {
		spin_unlock_irqrestore(&object->lock, irq);
		release_object_page_storage(page);
		goto retry;
	}

	page->next = object->pages;
	object->pages = page;
	object_page_index_insert(object, page);
	(void)atomic_fetch_add_relaxed(&object_pages, 1);

	spin_unlock_irqrestore(&object->lock, irq);

read_page:
	/* Reads the page's data, zero-filling past the end of file. */
	kern_memset((void *)hal_pmem_to_kernel(page->pmem.paddr), 0, PAGE_SIZE);
	length = (size_t)(fault_size - offset);
	if (length > PAGE_SIZE)
		length = PAGE_SIZE;

read_io_retry:
	/* An anonymous object needs no backend read; its page starts zeroed. */
	if ((object->flags & VM_OBJECT_ANONYMOUS) != 0) {
		count = (ssize_t)length;
	} else {
		/* Reads the page through the backend under the file's I/O lease. */
		error = file_io_begin(object->file, FILE_IO_PREAD, offset,
		    FILE_IO_VM_OBJECT, &io);
		if (error != 0) {
			count = -error;
		} else {
			/*
			 * i_io_lock is now held.  A resize published after page
			 * BUSY but before the backend read is detected before
			 * stale I/O starts.
			 */
			irq = spin_lock_irqsave(&object->lock);
			error = 0;
			if ((object->flags & (VM_OBJECT_RESIZING |
			    VM_OBJECT_CONTENT)) != 0 ||
			    object->size_generation != fault_generation ||
			    object->content_generation != fault_content_generation ||
			    offset >= object->logical_size)
				error = EAGAIN;
			spin_unlock_irqrestore(&object->lock, irq);

			/* Transfers only into a page the file still describes. */
			if (error == 0)
				count = file_io_transfer(&io,
				    (void *)hal_pmem_to_kernel(page->pmem.paddr), length);
			else
				count = -error;
			file_io_end(&io);
		}
	}

	/* A read that ran out of memory retries after reclaiming a reserve. */
	if (count == -(ssize_t)ENOMEM &&
	    read_retries < VM_OBJECT_FAULT_RECLAIM_RETRIES &&
	    reclaim_object_fault_reserve() != 0) {
		read_retries++;
		goto read_io_retry;
	}

	/* Publishes the result, marking the page in error on failure. */
	irq = spin_lock_irqsave(&object->lock);

	/* Fails the fault when a transaction or a size change overtook the read. */
	if ((object->flags & (VM_OBJECT_RESIZING | VM_OBJECT_CONTENT)) != 0 ||
	    object->size_generation != fault_generation ||
	    object->content_generation != fault_content_generation) {
		page->flags = VM_OBJECT_PAGE_ERROR;
		page->error = EAGAIN;
		waitq_wake_all(&object->page_waitq);
		spin_unlock_irqrestore(&object->lock, irq);
		return EAGAIN;
	}

	/* Fails the fault when the backend did not return the whole page. */
	if (count != (ssize_t)length) {
		if (count < 0)
			error = (int)-count;
		else
			error = EIO;
		page->flags = VM_OBJECT_PAGE_ERROR;
		page->error = error;
		waitq_wake_all(&object->page_waitq);
		spin_unlock_irqrestore(&object->lock, irq);
		return error;
	}

	page->flags = 0;
	page->error = 0;
	page->hold_count = 1;

	waitq_wake_all(&object->page_waitq);

	*result = page;

	spin_unlock_irqrestore(&object->lock, irq);

	/* Reports the held page. */
	return 0;
}

/*
 * Pins an object page for copying, holding its object as an operation.
 */
int
vm_object_page_pin(
	struct vm_object_page *page)
{
	struct vm_object *object;
	unsigned long irq;
	bool enabled;

	/* Rejects a missing page or one without an owner. */
	if (page == NULL)
		return EINVAL;

	object = page->owner;
	if (object == NULL)
		return EINVAL;

	/*
	 * The caller supplies a stable page pointer (normally while holding
	 * VM metadata).  Registry -> object is the same lifetime order as
	 * detach.
	 */
	enabled = registry_lock();

	if ((object->flags & VM_OBJECT_DETACHING) != 0) {
		registry_unlock(enabled);
		return EBUSY;
	}

	irq = spin_lock_irqsave(&object->lock);

	if (page->owner != object ||
	    page->hold_count == UINT_MAX ||
	    page->pin_count == UINT_MAX) {
		spin_unlock_irqrestore(&object->lock, irq);
		registry_unlock(enabled);

		if (page->owner == object)
			return EOVERFLOW;

		return EINVAL;
	}

	/* Takes the hold, the pin, the operation, and a reference. */
	page->hold_count++;
	page->pin_count++;

	object->active_operations++;
	if (object->active_operations == 0)
		HAL_FATAL("VM object pin operation counter overflow");
	refcount_get(&object->refs);

	spin_unlock_irqrestore(&object->lock, irq);

	registry_unlock(enabled);

	/* Reports the pinned page. */
	return 0;
}

/*
 * Copies out of a pinned page.
 */
int
vm_object_page_pin_read(
	struct vm_object_page *page,
	size_t offset,
	void *buffer,
	size_t length)
{
	int error;

	/* Reports why the copy failed. */
	error = vm_object_page_pin_copy(page, offset, buffer, length, 0, 0);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Copies into a pinned page, marking it dirty.
 */
int
vm_object_page_pin_write(
	struct vm_object_page *page,
	size_t offset,
	const void *buffer,
	size_t length)
{
	int error;

	/* Reports why the copy failed. */
	error = vm_object_page_pin_copy(page, offset, (void *)buffer, length, 1, 0);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Copies out of a pinned page unless a write-back owns it (EBUSY).  A fault
 * that holds a mapping reserved uses it: a sync that owns the page waits
 * for that reservation while revoking the page's mappings (BUG-082).
 */
int
vm_object_page_pin_read_nowait(
	struct vm_object_page *page,
	size_t offset,
	void *buffer,
	size_t length)
{
	int error;

	/* Reports why the copy failed. */
	error = vm_object_page_pin_copy(page, offset, buffer, length, 0, 1);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Waits until no write-back owns a pinned page, holding no VM lock.
 */
int
vm_object_page_pin_wait(
	struct vm_object_page *page)
{
	uint8_t nothing;
	int error;

	/* A zero-length copy waits and moves nothing. */
	error = vm_object_page_pin_copy(page, 0, &nothing, 0, 0, 0);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Releases a pin, freeing a resize orphan whose last pin it was.
 */
void
vm_object_page_unpin(
	struct vm_object_page *page)
{
	struct vm_object *object;
	struct vm_object_page *free_page;
	unsigned long irq;

	free_page = NULL;

	/* Ignores a missing page or one without an owner. */
	if (page == NULL)
		return;
	object = page->owner;
	if (object == NULL)
		return;

	/* Drops the pin and hold; an orphan with no holds left is freed. */
	irq = spin_lock_irqsave(&object->lock);

	if (page->hold_count == 0 || page->pin_count == 0)
		HAL_FATAL("VM object page pin underflow");
	page->pin_count--;
	page->hold_count--;

	/* Takes an orphan out of the object once its last hold is gone. */
	if ((page->flags & VM_OBJECT_PAGE_ORPHANED) != 0 &&
	    (object->flags & VM_OBJECT_RESIZING) == 0 &&
	    page->hold_count == 0) {
		if (page->pin_count != 0 ||
		    page->mapping_count != 0 ||
		    !unlink_page_locked(&object->orphan_pages, page))
			HAL_FATAL("lost VM object resize orphan");
		page->flags &= ~VM_OBJECT_PAGE_ORPHANED;
		free_page = page;
	}

	waitq_wake_all(&object->page_waitq);

	spin_unlock_irqrestore(&object->lock, irq);

	/* Frees the unlinked orphan and ends the pin's operation. */
	if (free_page != NULL)
		free_object_page(free_page);
	object_operation_end(object);

	/*
	 * A concurrent final put may have unlinked the object after the
	 * operation wake.  The pin reference makes this the unique
	 * final-destroy point.
	 */
	if (refcount_put(&object->refs))
		destroy_object(object);
}

/*
 * Releases the hold a fault returned without mapping the page.
 */
void
vm_object_fault_release(
	struct vm_object_page *page)
{
	unsigned long irq;

	/* Ignores a missing page or one without an owner. */
	if (page == NULL || page->owner == NULL)
		return;

	/* Drops the hold and wakes the waiters. */
	irq = spin_lock_irqsave(&page->owner->lock);

	if (page->hold_count == 0)
		HAL_FATAL("VM object fault hold underflow");
	page->hold_count--;
	waitq_wake_all(&page->owner->page_waitq);

	spin_unlock_irqrestore(&page->owner->lock, irq);
}

/*
 * Holds a page the object already caches, without reading or waiting.
 *
 * Fault-around maps the neighbours of a faulting page only when they are
 * already there: a page that is absent, being read, failed, past the end
 * of the file, or inside a resize or content transaction is left to its
 * own fault.  The hold is released as vm_object_fault()'s is.
 */
int
vm_object_fault_resident(
	struct vm_object *object,
	off_t offset,
	struct vm_object_page **result)
{
	struct vm_object_page *page;
	unsigned long irq;
	int error;

	/* Rejects a missing object or result, or an unaligned offset. */
	if (object == NULL ||
	    result == NULL ||
	    offset < 0 ||
	    (offset & (PAGE_SIZE - 1U)) != 0)
		return EINVAL;

	/* Looks the page up under the object lock. */
	error = 0;
	page = NULL;
	irq = spin_lock_irqsave(&object->lock);

	/* Leaves the page to its own fault while a transaction runs. */
	if ((object->flags & (VM_OBJECT_RESIZING | VM_OBJECT_CONTENT)) != 0)
		error = EAGAIN;

	/* Leaves a page past the end of the file to its own fault. */
	if (error == 0 && offset >= object->logical_size)
		error = ENXIO;

	/* Finds the cached page. */
	if (error == 0) {
		page = find_page(object, offset);
		if (page == NULL)
			error = ENOENT;
	}

	/* Skips a page being read or one whose read failed. */
	if (error == 0 &&
	    (page->flags & (VM_OBJECT_PAGE_BUSY | VM_OBJECT_PAGE_ERROR)) != 0)
		error = EAGAIN;

	/* Holds the page for the caller's mapping. */
	if (error == 0) {
		page->hold_count++;
		*result = page;
	}

	/* Lets the object's other users in. */
	spin_unlock_irqrestore(&object->lock, irq);

	/* Reports whether the page was held. */
	return error;
}

/*
 * Converts a fault hold into a reverse mapping.
 */
void
vm_object_mapping_add(
	struct vm_object_page *object_page,
	struct vm_page *mapping)
{
	unsigned long irq;

	/* Ignores a missing page or mapping. */
	if (object_page == NULL || mapping == NULL || object_page->owner == NULL)
		return;

	/* Links the mapping and releases the hold under the metadata lock. */
	vm_metadata_enter();
	irq = spin_lock_irqsave(&object_page->owner->lock);

	/* Turns the fault's hold into a published mapping. */
	if (object_page->hold_count == 0)
		HAL_FATAL("mapping VM object page without fault hold");

	/*
	 * Puts the mapping at the head of the list.  Each mapping's
	 * object_link points at the link that points at it, so that it can
	 * leave without a walk of the list.
	 */
	mapping->object_next = object_page->mappings;
	if (mapping->object_next != NULL)
		mapping->object_next->object_link = &mapping->object_next;
	mapping->object_link = &object_page->mappings;

	/* The mapping is published, and the fault's hold becomes it. */
	object_page->mappings = mapping;
	object_page->mapping_count++;
	object_page->hold_count--;

	waitq_wake_all(&object_page->owner->page_waitq);

	spin_unlock_irqrestore(&object_page->owner->lock, irq);
	vm_metadata_leave();
}

/*
 * Unlinks a reverse mapping; the caller holds the owner's lock.
 */
void
vm_object_mapping_remove_locked(
	struct vm_object_page *object_page,
	struct vm_page *mapping)
{
	struct vm_page **link;

	/*
	 * The page knows the link that points at it, so it leaves without a
	 * walk of the list, which a page of a shared library makes as long
	 * as the number of processes.  A page that is not linked is left as
	 * it is.
	 */
	link = mapping->object_link;
	if (link == NULL || *link != mapping)
		return;

	/* Unlinks the mapping; the one after it now hangs from its link. */
	*link = mapping->object_next;
	if (mapping->object_next != NULL)
		mapping->object_next->object_link = link;
	mapping->object_next = NULL;
	mapping->object_link = NULL;

	/* One mapping fewer keeps the object page. */
	if (object_page->mapping_count == 0)
		HAL_FATAL("VM object mapping counter underflow");
	object_page->mapping_count--;
}

/*
 * Unlinks a reverse mapping under the owner's lock.
 */
void
vm_object_mapping_remove(
	struct vm_object_page *object_page,
	struct vm_page *mapping)
{
	unsigned long irq;

	/* Ignores a missing page or mapping. */
	if (object_page == NULL || mapping == NULL || object_page->owner == NULL)
		return;

	/* Unlinks under the metadata and owner locks. */
	vm_metadata_enter();
	irq = spin_lock_irqsave(&object_page->owner->lock);

	vm_object_mapping_remove_locked(object_page, mapping);

	spin_unlock_irqrestore(&object_page->owner->lock, irq);
	vm_metadata_leave();
}

/*
 * Marks an object page dirty with a fresh generation.
 */
void
vm_object_mark_dirty(
	struct vm_object_page *page)
{
	unsigned long irq;

	/* Ignores a missing page or one without an owner. */
	if (page == NULL || page->owner == NULL)
		return;

	/* Sets the flag under the metadata and owner locks. */
	vm_metadata_enter();
	irq = spin_lock_irqsave(&page->owner->lock);

	object_page_dirty_mark(page);

	spin_unlock_irqrestore(&page->owner->lock, irq);
	vm_metadata_leave();
}

/*
 * Tests whether any page in a range has a wired mapping.
 */
int
range_has_wired_mapping(
	struct vm_object *object,
	uint64_t start,
	uint64_t end)
{
	struct vm_object_page *page;
	struct vm_page *mapping;

	/* Looks for a wired mapping on any page of the range. */
	for (page = object->pages; page != NULL; page = page->next) {
		if (!page_overlaps(page, start, end))
			continue;

		/* Reports the first mapping of this page that is wired. */
		for (mapping = page->mappings;
		     mapping != NULL;
		     mapping = mapping->object_next) {
			if (mapping->wire_count != 0)
				return 1;
		}
	}

	/* Reports a range nothing wires down. */
	return 0;
}

/* Copies into or out of a pinned page once it is not busy. */
static int
vm_object_page_pin_copy(
	struct vm_object_page *page,
	size_t offset,
	void *buffer,
	size_t length,
	int write,
	int nowait)
{
	struct vm_object *object;
	unsigned long irq;
	int error;
	uint64_t sequence;

	/* Rejects a call that names no page to copy through. */
	if (page == NULL)
		return EINVAL;

	/* Rejects a call that names no buffer to copy from or into. */
	if (buffer == NULL)
		return EINVAL;

	/* Rejects a page that no object owns, because it has no lock to take. */
	object = page->owner;
	if (object == NULL)
		return EINVAL;

	/* Rejects a start that lies past the end of the page. */
	if (offset > PAGE_SIZE)
		return EINVAL;

	/* Rejects a run that would reach past the end of the page. */
	if (length > PAGE_SIZE - offset)
		return EINVAL;

	/* Waits for the page to leave any writeback, still pinned. */
	irq = spin_lock_irqsave(&object->lock);

	for (;;) {
		if (page->owner != object || page->pin_count == 0) {
			spin_unlock_irqrestore(&object->lock, irq);
			return EINVAL;
		}

		if ((page->flags & (VM_OBJECT_PAGE_BUSY | VM_OBJECT_PAGE_WRITEBACK)) == 0)
			break;

		/* A caller that may not wait is told the page is taken. */
		if (nowait) {
			spin_unlock_irqrestore(&object->lock, irq);
			return EBUSY;
		}

		sequence = waitq_sequence(&object->page_waitq);
		error = waitq_sleep(&object->page_waitq, &object->lock, sequence, 0, 0);

		if (error != 0 && error != EAGAIN) {
			spin_unlock_irqrestore(&object->lock, irq);
			return error;
		}
	}

	/*
	 * Orphan writes stay dirty so abort can restore the old cache image.
	 * Commit discards the orphan without ever putting it on writeback.
	 */
	if (write) {
		kern_memcpy((uint8_t *)hal_pmem_to_kernel(page->pmem.paddr) + offset, buffer, length);
		object_page_dirty_mark(page);
	} else {
		kern_memcpy(buffer, (const uint8_t *)hal_pmem_to_kernel(page->pmem.paddr) + offset, length);
	}

	spin_unlock_irqrestore(&object->lock, irq);

	return 0;
}
