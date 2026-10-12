/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Actual registered callbacks use the native flip/IRQ path and owned copy buffers. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <kern/clock.h>
#include <kern/dcache.h>
#include <kern/device-io.h>
#include <kern/kmem.h>
#include <kern/pmem.h>
#include <kern/sched.h>
#include <kern/text-display.h>
#include <kern/waitq.h>
#include <drivers/gpu/gpu.h>
#include <uapi/errno.h>
#include <uapi/gpu-allocation.h>

#include "drivers/gpu/bcm2711/bcm2711-private.h"

/* One allocated run lives until its actual last buffer reference is released. */
struct test_allocation {
	uint64_t physical;
	size_t bytes;
	void *address;
};

/* The model retains both private scanout targets and ordinary session resources. */
static struct test_allocation test_allocations[24];
static uint32_t test_hvs[8192];
static uint32_t test_pv[2][32];
static struct bcm2711_display test_display;
static const struct drv_gpu_ops *test_operations;
static void *test_device;
static uint8_t *test_console;
static unsigned test_run_count;
static unsigned test_freed;
static bool test_stall;

/* Text painting remains stopped until actual console adoption is confirmed. */
static bool test_text_suspended;

/* Models an allocator that violates requested placement before any DMA publication. */
static bool test_misplaced;

/* The clock provider can refuse a transition without permitting any list publication. */
static bool test_clock_refused;
static uint32_t test_core_hz;

/* A cursor move made while a present waits for its adoption (B-k2): who moves it, and the answer. */
static bool test_move_during_wait;
static int test_move_error;
static void *test_move_session;
static uint64_t test_move_lease;

/* The model's scheduler clock (display-lock-host.c). */
extern uint64_t display_test_ticks;

static int present_frame(void *session, void *resource, struct gpu_display_present *present);
static int drain_lists(void);
static void deliver_frame(void);
static void cursor_rows(void *session, void *resource, struct gpu_display_cursor_image *request, uint8_t value);
static unsigned named_cursor_images(void);

/*
 * Verifies the real node callbacks, byte conversion, lease isolation and DMA holds.
 * This host model cannot prove physical output or actual ARM cache ordering.
 */
int
main(
	void)
{
	struct gpu_info info;
	struct gpu_display_info output;
	struct gpu_display_claim claim;
	struct gpu_display_release release;
	struct gpu_display_present present;
	struct gpu_display_wait wait;
	struct gpu_resource_create create;
	struct drv_gpu_mapping mapping;
	struct bcm2711_flip_status status;
	struct drv_bcm2711_boot_screen overlay;
	struct gpu_blob_create_placed blob;
	struct gpu_image_descriptor image;
	struct drv_gpu_scanout_backing backing;
	struct gpu_scanout_constraints constraints;
	struct gpu_display_cursor_image cursor_image;
	struct gpu_display_cursor_move cursor_move;
	struct gpu_display_cursor_hide cursor_hide;
	struct gpu_display_overlay_info overlay_info;
	uint8_t arrow[16 * 4 * 8];
	void *capability;
	void *imported;
	void *alias;
	void *other;
	uint32_t identifier;
	uint32_t *words;
	const uint8_t pixels[4] = {0x11, 0x22, 0x33, 0xff};
	uint8_t *native;
	void *first;
	void *second;
	void *resource;
	uint64_t lease;
	uint64_t sequence;
	unsigned freed_before;
	int error;

	/* Prepares a running native-mode fixture and real IRQ service owners. */
	test_console = calloc(1, 8294400);
	assert(test_console != NULL);
	test_display.compositor.mapped = (volatile uint8_t *)test_hvs;
	test_display.compositor.size = sizeof(test_hvs);
	test_display.compositor_irq.registered = true;
	test_display.timing[0].mapped = (volatile uint8_t *)test_pv[0];
	test_display.timing[1].mapped = (volatile uint8_t *)test_pv[1];
	test_display.timing[0].size = sizeof(test_pv[0]);
	test_display.timing[1].size = sizeof(test_pv[1]);
	test_display.timing_irq[0].registered = true;
	test_display.timing_irq[1].registered = true;
	test_display.screen.physical = 0x01000000;
	test_display.screen.size = 8294400;
	test_display.screen.width = 1920;
	test_display.screen.height = 1080;
	test_display.screen.pitch = 7680;
	test_display.scanout_started = true;
	test_display.refresh_millihz = 60000;
	test_display.max_core_hz = 500000000;
	test_display.console_core_hz = 137600000;
	test_hvs[0x40 / 4] = 0x87800438;
	test_hvs[0x14 / 4] = 0xc0000000;
	test_hvs[0x20 / 4] = 43;
	test_hvs[0x30 / 4] = 43;
	test_pv[0][0] = 1;
	test_pv[0][1] = 1;
	error = bcm2711_display_irq_prepare(&test_display);
	assert(error == 0);
	error = bcm2711_display_register(&test_display);
	assert(error == 0 && test_operations != NULL);
	assert(test_run_count == 2 && test_freed == 0);
	assert(test_operations->display->flags == DRV_GPU_DISPLAY_UNGATED && test_operations->display->adoption_wait != NULL);

	/* Opens two independent sessions through the actual published operation table. */
	error = test_operations->open(test_device, &first);
	assert(error == 0);
	error = test_operations->open(test_device, &second);
	assert(error == 0);
	memset(&info, 0, sizeof(info));
	error = test_operations->get_info(test_device, first, &info);
	assert(error == 0 && (info.capabilities & GPU_CAP_COMMAND) == 0);
	memset(&output, 0, sizeof(output));
	error = test_operations->display->query(test_device, first, &output);
	assert(error == 0 && output.display_id == 1 && output.current_width == 1920);

	/* Only the first open can claim the one actual boot output. */
	memset(&claim, 0, sizeof(claim));
	claim.display_id = 1;
	claim.generation = 1;
	error = test_operations->display->claim(test_device, first, &claim);
	assert(error == 0 && claim.lease != 0);
	assert(test_text_suspended);
	lease = claim.lease;
	error = test_operations->display->claim(test_device, second, &claim);
	assert(error == EBUSY);

	/* Allocates source storage and transfers RGBA pixels through the real CPU callbacks. */
	memset(&create, 0, sizeof(create));
	create.bytes = 8294400;
	create.usage = GPU_RESOURCE_USAGE_STORAGE;
	error = test_operations->resource_create(test_device, first, &create, &resource);
	assert(error == 0);
	error = test_operations->resource_write(test_device, first, resource, 0, pixels, 4);
	assert(error == 0);
	error = test_operations->resource_map(test_device, first, resource, &mapping);
	assert(error == 0 && mapping.bytes == 8294400 && mapping.attributes == 0);

	/* A refused clock publishes nothing; then the copy is converted to native BGRA and adopted. */
	memset(&present, 0, sizeof(present));
	present.lease = lease;
	present.width = 1920;
	present.height = 1080;
	present.stride = 7680;
	present.format = GPU_PIXEL_RGBA8888;
	present.flags = GPU_DISPLAY_PRESENT_FIFO;
	present.generation = 1;
	test_clock_refused = true;
	error = present_frame(first, resource, &present);
	assert(error == EIO && test_hvs[0x20 / 4] == 43 && !test_display.flip.restoring);
	test_clock_refused = false;
	error = present_frame(first, resource, &present);
	assert(error == 0 && present.sequence == test_display.flip.state_serial && present.sequence != 0);
	sequence = present.sequence;
	bcm2711_display_flip_snapshot(&test_display, &status);
	native = kern_pmem_to_kernel(status.primary.frame.physical);
	assert(native[0] == 0x33 && native[1] == 0x22 && native[2] == 0x11 && native[3] == 0xff);
	assert(status.primary.hold == NULL && status.active_list == 256 && status.pending_count == 0);

	/* An opaque full-screen frame hides the console, which leaves the list. */
	words = &test_hvs[0x4000 / 4 + 256];
	assert(words[0] == 0x4800d807 && words[1] == 0 && words[2] == 0x4000fff0);
	assert(words[3] == 0x04380780 && words[5] == (0xc0000000U | (uint32_t)status.primary.frame.physical));
	assert(words[4] == 0xc0c0c0c0 && words[6] == 0xc0c0c0c0);
	assert(words[7] == 7680 && words[8] == 0x80000000);

	/* A composed frame keeps the transition floor; only the return to the console lowers it. */
	assert(test_core_hz == 500000000 && status.clock_error == 0);

	/* The source may be destroyed immediately; HVS still references its private copy. */
	freed_before = test_freed;
	test_operations->resource_destroy(test_device, first, resource);
	assert(test_freed == freed_before + 1 && native[0] == 0x33);
	memset(&wait, 0, sizeof(wait));
	wait.lease = lease;
	wait.sequence = sequence;
	error = test_operations->display->wait(test_device, first, &wait);
	assert(error == 0 && wait.completed_sequence == sequence);

	/* A number not yet recorded: EAGAIN at once without time, ETIMEDOUT after some frames. */
	wait.sequence = sequence + 100U;
	error = test_operations->display->wait(test_device, first, &wait);
	assert(error == EAGAIN);
	wait.timeout_ns = 5000000U;
	error = test_operations->display->wait(test_device, first, &wait);
	assert(error == ETIMEDOUT);
	wait.timeout_ns = 0;
	wait.sequence = sequence;

	/* A failed restoration retains both the private storage and the retryable lease. */
	memset(&release, 0, sizeof(release));
	release.lease = lease;
	test_stall = true;
	error = test_operations->display->release(test_device, first, &release);
	assert(error == ETIMEDOUT && test_freed == freed_before + 1);
	assert(test_text_suspended);
	error = test_operations->display->claim(test_device, second, &claim);
	assert(error == EBUSY);
	test_stall = false;
	error = test_operations->display->release(test_device, first, &release);
	assert(error == 0);
	bcm2711_display_flip_snapshot(&test_display, &status);
	assert(status.used_mask == 0 && status.active_list == 43);
	assert(!test_text_suspended);

	/* The output's numbers go on across leases: an earlier lease's number is adopted for the next. */
	error = test_operations->display->claim(test_device, second, &claim);
	assert(error == 0 && claim.lease != lease);
	wait.lease = claim.lease;
	wait.sequence = sequence;
	error = test_operations->display->wait(test_device, second, &wait);
	assert(error == 0 && wait.completed_sequence > sequence);
	wait.lease = lease;
	error = test_operations->display->wait(test_device, first, &wait);
	assert(error == EBUSY);
	test_operations->close(test_device, second);
	assert(!test_text_suspended);
	test_operations->close(test_device, first);
	assert(test_freed == 1);

	/* Exercises positioned premultiplied composition over the console. */
	overlay = test_display.screen;
	overlay.physical = test_allocations[0].physical;
	overlay.width = 32;
	overlay.height = 16;
	overlay.pitch = 128;
	overlay.size = 2048;
	error = bcm2711_display_flip_compose(&test_display, &overlay, 123, 456, true);
	assert(error == 0);
	words = &test_hvs[0x4000 / 4 + test_display.flip.active_list];
	assert(words[0] == 0x4800d807 && words[5] == 0xc1000000);
	assert(words[8] == 0x4800d807 && words[9] == 0x01c8007b);
	assert(words[10] == 0x2000fff0 && words[11] == 0x00100020);
	assert(words[13] == 0xc2000000 && words[15] == 128 && words[16] == 0x80000000);
	error = bcm2711_display_flip_restore(&test_display);
	assert(error == 0 && test_core_hz == 137600000);

	/* Advertised foreign sharing carries actual native constraints, never a companion guess. */
	error = test_operations->open(test_device, &first);
	assert(error == 0);
	error = test_operations->open(test_device, &second);
	assert(error == 0);
	memset(&constraints, 0, sizeof(constraints));
	constraints.display_id = 1;
	constraints.generation = 1;
	error = test_operations->scanout->constraints(test_device, first, &constraints);
	assert(error == 0 && constraints.flags == (GPU_SCANOUT_COPY | GPU_SCANOUT_SHARED | GPU_SCANOUT_FOREIGN));
	assert(constraints.max_dma_address == 0x3fffffff && constraints.placement == GPU_PLACEMENT_CONTIGUOUS);

	/* Placed allocation verifies the actual run and refuses requested cache coherence. */
	memset(&blob, 0, sizeof(blob));
	blob.blob.bytes = 8294400;
	blob.blob.flags = GPU_BLOB_MAPPABLE | GPU_BLOB_SHAREABLE | GPU_BLOB_CROSS_DEVICE;
	blob.placement.flags = GPU_PLACEMENT_COHERENT;
	error = test_operations->blob_create_placed(test_device, first, &blob, &resource, &identifier);
	assert(error == ENOTSUP && resource == NULL && identifier == 0);
	blob.placement.flags = GPU_PLACEMENT_CONTIGUOUS;
	blob.placement.max_dma_address = 0x3fffffff;
	blob.placement.alignment = 4096;
	freed_before = test_freed;
	test_misplaced = true;
	error = test_operations->blob_create_placed(test_device, first, &blob, &resource, &identifier);
	assert(error == ENOTSUP && resource == NULL && test_freed == freed_before + 1);
	test_misplaced = false;
	error = test_operations->blob_create_placed(test_device, first, &blob, &resource, &identifier);
	assert(error == 0 && resource != NULL && identifier != 0);
	error = test_operations->resource_write(test_device, first, resource, 0, pixels, 4);
	assert(error == 0);

	/* Export, same-device import and foreign import hold storage independently of the source. */
	memset(&image, 0, sizeof(image));
	image.width = 1920;
	image.height = 1080;
	image.stride = 7680;
	image.format = GPU_PIXEL_RGBA8888;
	image.allocation_bytes = 8294400;
	image.usage = 16;
	image.tiling = GPU_IMAGE_LINEAR;
	error = test_operations->share->export_resource(test_device, first, resource, &image, &capability);
	assert(error == 0);
	error = test_operations->share->import_resource(test_device, second, capability, &alias, &identifier);
	assert(error == 0 && identifier != 0);
	error = test_operations->share->get_scanout_backing(test_device, capability, &backing);
	assert(error == 0 && backing.page_count == 2025 && backing.page_bytes == 4096);
	backing.flags |= DRV_GPU_BACKING_DEVICE;
	error = test_operations->scanout->import_image(test_device, second, &image, &backing, &imported);
	assert(error == ENOTSUP && imported == NULL);
	backing.flags = DRV_GPU_BACKING_CONTIGUOUS;
	image.stride += 4;
	error = test_operations->scanout->import_image(test_device, second, &image, &backing, &imported);
	assert(error == ENOTSUP && imported == NULL);
	image.stride -= 4;
	error = test_operations->scanout->import_image(test_device, second, &image, &backing, &imported);
	assert(error == 0 && imported != NULL);
	freed_before = test_freed;
	test_operations->resource_destroy(test_device, first, resource);
	test_operations->close(test_device, first);
	test_operations->share->release(test_device, capability);
	test_operations->resource_destroy(test_device, second, alias);
	assert(test_freed == freed_before);

	/* Direct RGBA scanout encodes its own channel order; the recorded plane borrows the allocation. */
	memset(&claim, 0, sizeof(claim));
	claim.display_id = 1;
	claim.generation = 1;
	error = test_operations->display->claim(test_device, second, &claim);
	assert(error == 0);
	present.lease = claim.lease;
	present.flags = GPU_DISPLAY_PRESENT_FIFO | GPU_DISPLAY_PRESENT_BLOB;
	present.refresh_millihz = 60000;
	present.sequence = 0;
	error = present_frame(second, imported, &present);
	assert(error == 0);
	bcm2711_display_flip_snapshot(&test_display, &status);
	assert(status.primary.frame.format == 1 && status.primary.hold != NULL);
	words = &test_hvs[0x4000 / 4 + status.active_list];
	assert(words[0] == 0x4800f807 && words[8] == 0x80000000);
	test_operations->resource_destroy(test_device, second, imported);
	assert(test_freed == freed_before);
	release.lease = claim.lease;
	error = test_operations->display->release(test_device, second, &release);
	assert(error == 0 && test_freed == freed_before + 1);

	/* Two uncertain native blob frames survive both descriptors and a failed close restore. */
	error = test_operations->blob_create(test_device, second, &blob.blob, &resource, &identifier);
	assert(error == 0);
	error = test_operations->blob_create(test_device, second, &blob.blob, &other, &identifier);
	assert(error == 0);
	error = test_operations->display->claim(test_device, second, &claim);
	assert(error == 0);
	present.lease = claim.lease;
	present.sequence = 0;
	error = present_frame(second, resource, &present);
	assert(error == 0);
	test_stall = true;
	present.sequence = 0;
	error = present_frame(second, other, &present);
	assert(error == ETIMEDOUT);
	bcm2711_display_flip_snapshot(&test_display, &status);
	assert(status.uncertain && status.used_mask != 0 && status.retiring_count == 1 && status.primary.hold != NULL);
	freed_before = test_freed;
	test_operations->resource_destroy(test_device, second, resource);
	test_operations->resource_destroy(test_device, second, other);
	test_operations->close(test_device, second);
	assert(test_freed == freed_before);
	assert(test_text_suspended);
	test_stall = false;
	error = test_operations->open(test_device, &first);
	assert(error == 0);
	error = test_operations->display->claim(test_device, first, &claim);
	assert(error == 0 && test_freed == freed_before + 2);
	assert(test_text_suspended);
	test_operations->close(test_device, first);
	assert(!test_text_suspended);

	/* The output has a cursor plane, and only the lease's open uses it. */
	error = test_operations->open(test_device, &first);
	assert(error == 0);
	memset(&output, 0, sizeof(output));
	error = test_operations->display->query(test_device, first, &output);
	assert(error == 0 && (output.flags & GPU_DISPLAY_HAS_CURSOR) != 0 && (output.flags & GPU_DISPLAY_HAS_OVERLAYS) != 0);
	assert(output.plane_count == 3);
	memset(&overlay_info, 0, sizeof(overlay_info));
	overlay_info.display_id = 1;
	overlay_info.generation = 1;
	overlay_info.plane = 3;
	error = test_operations->display->overlay_info(test_device, first, &overlay_info);
	assert(error == EINVAL);
	overlay_info.plane = 2;
	error = test_operations->display->overlay_info(test_device, first, &overlay_info);
	assert(error == 0 && overlay_info.max_src_width == 1920 && overlay_info.max_dst_height == 1080);
	assert(overlay_info.flags == (GPU_DISPLAY_OVERLAY_SCALE_UP | GPU_DISPLAY_OVERLAY_SCALE_DOWN_X | GPU_DISPLAY_OVERLAY_SCALE_DOWN_Y));
	memset(&claim, 0, sizeof(claim));
	claim.display_id = 1;
	claim.generation = 1;
	error = test_operations->display->claim(test_device, first, &claim);
	assert(error == 0);
	memset(&create, 0, sizeof(create));
	create.bytes = 8294400;
	create.usage = GPU_RESOURCE_USAGE_STORAGE;
	error = test_operations->resource_create(test_device, first, &create, &resource);
	assert(error == 0);
	present.lease = claim.lease;
	present.flags = GPU_DISPLAY_PRESENT_FIFO;
	present.format = GPU_PIXEL_RGBA8888;
	present.sequence = 0;
	error = present_frame(first, resource, &present);
	assert(error == 0 && test_display.flip.pending_count == 0);

	/* A 16x8 image from the open's storage; a wrong lease or an imageless move is refused. */
	memset(arrow, 0x7f, sizeof(arrow));
	error = test_operations->resource_write(test_device, first, resource, 0, arrow, sizeof(arrow));
	assert(error == 0);
	memset(&cursor_image, 0, sizeof(cursor_image));
	cursor_image.lease = claim.lease + 1U;
	cursor_image.width = 16;
	cursor_image.height = 8;
	cursor_image.stride = 64;
	cursor_image.format = GPU_PIXEL_BGRA8888;
	cursor_image.generation = 1;
	error = test_operations->display->cursor_set(test_device, first, resource, &cursor_image);
	assert(error == EBUSY);
	memset(&cursor_move, 0, sizeof(cursor_move));
	cursor_move.lease = claim.lease;
	cursor_move.generation = 1;
	cursor_move.x = 10;
	cursor_move.y = 20;
	error = test_operations->display->cursor_move(test_device, first, &cursor_move);
	assert(error == EINVAL);

	/* Setting a hidden cursor's image publishes nothing; a move shows it without waiting. */
	cursor_image.lease = claim.lease;
	error = test_operations->display->cursor_set(test_device, first, resource, &cursor_image);
	assert(error == 0 && test_display.flip.pending_count == 0 && test_display.flip.cursor.physical != 0);
	native = kern_pmem_to_kernel(test_display.flip.cursor.physical);
	assert(native != NULL && native[0] == 0x7f && native[63] == 0x7f && native[64] == 0 && native[256] == 0x7f);
	error = test_operations->display->cursor_move(test_device, first, &cursor_move);
	words = &test_hvs[0x4000 / 4 + test_hvs[0x20 / 4]];
	assert(error == 0 && test_display.flip.pending_count == 1);
	assert(words[8] == 0x4800d807 && words[9] == ((20U << 16) | 10U) && words[10] == 0x2000fff0);
	assert(words[11] == ((8U << 16) | 16U) && words[13] == (0xc0000000U | test_display.flip.cursor.physical));
	assert(words[15] == 256 && words[16] == 0x80000000);

	/* The same unchanged rows show the image that holds them again, without a copy, even while a list names it. */
	identifier = test_display.flip.cursor.physical;
	native[1] = 0x11;
	error = test_operations->display->cursor_set(test_device, first, resource, &cursor_image);
	assert(error == 0 && test_display.flip.cursor.physical == identifier && native[1] == 0x11);
	native[1] = 0x7f;

	/* Changed rows (the write advances the run's generation) go to another image at once; the lists keep their own. */
	memset(arrow, 0x6e, sizeof(arrow));
	error = test_operations->resource_write(test_device, first, resource, 0, arrow, sizeof(arrow));
	assert(error == 0);
	error = test_operations->display->cursor_set(test_device, first, resource, &cursor_image);
	assert(error == 0 && test_display.flip.pending_count == 2 && test_display.flip.list_serial < test_display.flip.state_serial);
	assert(test_display.flip.cursor.physical != identifier && bcm2711_display_flip_cursor_named(&test_display, identifier));
	assert(((uint8_t *)kern_pmem_to_kernel(test_display.flip.cursor.physical))[0] == 0x6e && native[0] == 0x7f);

	/* Other rows of the same run are another source, and copy again. */
	identifier = test_display.flip.cursor.physical;
	cursor_image.offset = 64;
	error = test_operations->display->cursor_set(test_device, first, resource, &cursor_image);
	assert(error == 0 && test_display.flip.cursor.physical != identifier);
	cursor_image.offset = 0;
	error = test_operations->display->cursor_set(test_device, first, resource, &cursor_image);
	assert(error == 0 && test_display.flip.cursor.physical == identifier);

	/* The next frame is recorded at once and carries the cursor; a move during its adoption wait goes through. */
	present.sequence = 0;
	test_move_during_wait = true;
	test_move_error = -1;
	test_move_session = first;
	test_move_lease = claim.lease;
	error = present_frame(first, resource, &present);
	assert(error == 0 && test_move_error == 0 && !test_move_during_wait);
	error = drain_lists();
	assert(error == 0 && test_display.flip.pending_count == 0);
	words = &test_hvs[0x4000 / 4 + test_display.flip.active_list];
	assert(words[8] == 0x4800d807 && words[9] == ((21U << 16) | 11U) && words[16] == 0x80000000);
	assert(words[13] == (0xc0000000U | test_display.flip.cursor.physical));

	/* Five images: three lists and the recorded cursor each name one, and a fifth is still free. */
	cursor_rows(first, resource, &cursor_image, 0x21);
	error = drain_lists();
	assert(error == 0);
	test_stall = true;
	cursor_rows(first, resource, &cursor_image, 0x22);
	cursor_rows(first, resource, &cursor_image, 0x23);
	cursor_rows(first, resource, &cursor_image, 0x24);
	assert(test_display.flip.pending_count == 2 && test_display.flip.list_serial < test_display.flip.state_serial);
	assert(named_cursor_images() == 4);
	cursor_rows(first, resource, &cursor_image, 0x25);
	assert(named_cursor_images() == 4);
	test_stall = false;
	error = drain_lists();
	assert(error == 0 && named_cursor_images() == 1);

	/* Hiding publishes the frame alone; the release takes the cursor and its image away. */
	memset(&cursor_hide, 0, sizeof(cursor_hide));
	cursor_hide.lease = claim.lease;
	cursor_hide.generation = 1;
	error = test_operations->display->cursor_hide(test_device, first, &cursor_hide);
	assert(error == 0 && test_hvs[0x4000 / 4 + test_hvs[0x20 / 4] + 8] == 0x80000000);
	release.lease = claim.lease;
	error = test_operations->display->release(test_device, first, &release);
	assert(error == 0 && test_display.flip.active_list == 43);
	assert(!test_display.flip.cursor.shown && test_display.flip.cursor.physical == 0);
	error = test_operations->display->cursor_move(test_device, first, &cursor_move);
	assert(error == EBUSY);
	test_operations->resource_destroy(test_device, first, resource);
	test_operations->close(test_device, first);

	/* Succeeded: private scanout storage survives every ordinary open and resource. */
	puts("display-device-host-test PASS");
	return 0;
}

/* Records a present and waits for its adoption, as the GPU core does for an ungated display. */
static int
present_frame(
	void *session,
	void *resource,
	struct gpu_display_present *present)
{
	int error;

	/* The record, which pins nothing past its return. */
	present->sequence = 0;
	error = test_operations->display->present(test_device, session, resource, present);
	if (error != 0)
		return error;

	/* The adoption wait, which numbers the present. */
	error = test_operations->display->adoption_wait(test_device, session, present->lease, &present->sequence, NULL);
	if (error != 0)
		return error;

	/* Succeeded: the frame is shown. */
	return 0;
}

/* Lets the hardware take lists until none is out and the newest shows every record. */
static int
drain_lists(
	void)
{
	unsigned frames;

	/* At most a few frames: each adoption may publish the coalesced rest. */
	for (frames = 0; frames < 8U; frames++) {
		/* Nothing is out and nothing is left to publish. */
		if (test_display.flip.pending_count == 0 && test_display.flip.list_serial == test_display.flip.state_serial)
			return 0;
		deliver_frame();
	}

	/* The lists did not settle. */
	return ETIMEDOUT;
}

/* Writes new cursor rows of one byte into the resource and sets them (a copy into a free image). */
static void
cursor_rows(
	void *session,
	void *resource,
	struct gpu_display_cursor_image *request,
	uint8_t value)
{
	uint8_t rows[16 * 4 * 8];
	int error;

	/* The write advances the run's generation, so the rows are copied. */
	memset(rows, value, sizeof(rows));
	error = test_operations->resource_write(test_device, session, resource, 0, rows, sizeof(rows));
	assert(error == 0);
	error = test_operations->display->cursor_set(test_device, session, resource, request);
	assert(error == 0);
	assert(((uint8_t *)kern_pmem_to_kernel(test_display.flip.cursor.physical))[0] == value);
}

/* Counts the cursor images a list the hardware may read, or the recorded cursor, names. */
static unsigned
named_cursor_images(
	void)
{
	unsigned count;
	unsigned index;
	bool named;

	/* The cursor images are the allocations made after the two copy targets and the first resources. */
	count = 0;
	for (index = 0; index < test_run_count; index++) {
		/* Only live allocations of cursor size are cursor images. */
		if (test_allocations[index].address == NULL || test_allocations[index].bytes != 64U * 64U * 4U)
			continue;
		named = bcm2711_display_flip_cursor_named(&test_display, (uint32_t)test_allocations[index].physical);
		if (named)
			count++;
	}

	/* Succeeded: the number of named images. */
	return count;
}

/*
 * Keeps the visible diagnostic pauses bounded without physical wall-clock delay.
 */
void
bcm2711_stage_pause(
	const char *family,
	const char *stage)
{
	/* The real default path remains selected; only the platform time source is modeled. */
	(void)family;
	(void)stage;
}

/*
 * Models core-clock changes and verifies the provider is called outside the IRQ guard.
 */
int
drv_rpi4_firmware_property(
	uint32_t tag,
	uint32_t *values,
	unsigned request_count,
	unsigned capacity,
	uint32_t *answered)
{
	/* Firmware requests sleep: under the clock mutex, never under the spinlock. */
	assert(test_display.flip.clock_mutex.locked == 1 && test_display.flip.guard.held.value == 0);
	assert(tag == 0x00038002U && request_count == 3 && capacity == 3);
	assert(values[0] == 4 && values[2] == 0);
	if (test_clock_refused)
		return EIO;
	test_core_hz = values[1];
	*answered = 8;

	/* Succeeded: subsequent register writes observe the accepted floor. */
	return 0;
}

/*
 * Captures the common node publication after validating complete callback pairs.
 */
int
drv_gpu_register(
	const struct drv_gpu_ops *operations,
	void *private_data,
	struct drv_gpu_device **result)
{
	/* Registration can borrow only initialized tables and an attached native pipeline. */
	assert(operations->version == DRV_GPU_INTERFACE_VERSION);
	assert(operations->size == sizeof(*operations));
	assert(operations->display != NULL && operations->scanout != NULL);
	assert(test_display.flip.attached);
	test_operations = operations;
	test_device = private_data;
	*result = (struct drv_gpu_device *)private_data;

	/* Succeeded: the model can open the real published callbacks. */
	return 0;
}

/*
 * Allocates host RAM with independently checked physical placement descriptors.
 */
int
kern_pmem_alloc_limited(
	size_t bytes,
	size_t alignment,
	uint64_t limit,
	size_t boundary,
	struct kern_pmem *memory)
{
	struct test_allocation *allocation;

	/* Sequential physical slots are disjoint and satisfy the actual one-GiB request. */
	assert(test_run_count < 24 && alignment == 4096 && boundary == 0);
	allocation = &test_allocations[test_run_count];
	allocation->physical = 0x02000000ULL + test_run_count * 0x02000000ULL;
	if (test_misplaced) {
		/* A malformed returned run must be rejected before CPU mapping or publication. */
		allocation->physical += 0x40000000ULL;
	} else {
		assert(allocation->physical + bytes - 1 <= limit);
	}

	/* CPU storage belongs to the actual returned physical descriptor, including malformed placement. */
	allocation->address = calloc(1, bytes);
	assert(allocation->address != NULL);
	allocation->bytes = bytes;
	memory->paddr = allocation->physical;
	memory->size = bytes;
	test_run_count++;

	/* Succeeded: the caller owns one stable mapped run. */
	return 0;
}

/*
 * Frees one exact allocation and rejects stale or mismatched run descriptions.
 */
int
kern_pmem_free(
	struct kern_pmem *memory)
{
	unsigned index;

	/* Resolves the actual allocation by immutable physical identity. */
	for (index = 0; index < test_run_count; index++) {
		/* Only a matching live allocation may be returned to this allocator. */
		if (test_allocations[index].physical != memory->paddr)
			continue;
		assert(test_allocations[index].address != NULL && test_allocations[index].bytes == memory->size);
		free(test_allocations[index].address);
		test_allocations[index].address = NULL;
		memory->size = 0;
		test_freed++;
		return 0;
	}

	/* An unknown allocation is not silently accepted as a physical release. */
	return EINVAL;
}

/*
 * Translates only the persistent console and still-live modeled allocations.
 */
void *
kern_pmem_to_kernel(
	hal_physaddr_t physical)
{
	unsigned index;

	/* The firmware console remains separately owned throughout the model. */
	if (physical == 0x01000000)
		return test_console;

	/* Every resource mapping is stable until its real final reference disappears. */
	for (index = 0; index < test_run_count; index++) {
		/* Exact base translations suffice for production buffer and flip preparation. */
		if (test_allocations[index].physical == physical)
			return test_allocations[index].address;
	}

	/* Unknown storage cannot grant a usable CPU mapping. */
	return NULL;
}

/*
 * Supplies observable W1C semantics for actual PV/HVS source services.
 */
void
kern_mmio_write32(
	volatile void *address,
	uint32_t word)
{
	unsigned port;

	/* Quieting a source differs from an ordinary register assignment. */
	if (address == &test_hvs[1]) {
		test_hvs[1] &= ~word;
		return;
	}

	/* Clears only the addressed pixelvalve interrupt source. */
	for (port = 0; port < 2; port++) {
		/* The selected output never acknowledges the other port's frame. */
		if (address == &test_pv[port][0x28 / 4]) {
			test_pv[port][0x28 / 4] &= ~word;
			return;
		}
	}

	/* Actual current-list adoption is deferred until the next modeled vblank. */
	if (address == &test_hvs[0x20 / 4])
		assert(test_core_hz == 500000000);

	/* Models ordered register storage after the publication check. */
	*(volatile uint32_t *)address = word;
}

/*
 * Reads the actual persistent model register word.
 */
uint32_t
kern_mmio_read32(
	const volatile void *address)
{
	/* Registers share storage with independently driven hardware completion. */
	return *(const volatile uint32_t *)address;
}

/*
 * Delivers one native selected-PV interrupt during the boot observation's short sleeps.
 */
void
kern_usleep_range(
	unsigned minimum,
	unsigned maximum)
{
	/* A stalled model delivers nothing. */
	(void)minimum;
	(void)maximum;
	if (test_stall)
		return;
	deliver_frame();
}

/*
 * Lets a flip wait progress: a cursor move from the waiting present's open
 * when the scenario asks, then a frame that takes the newest list, or with
 * a stalled model the deadline.
 */
void
display_test_sleep(
	struct wait_queue *queue,
	uint64_t deadline)
{
	struct gpu_display_cursor_move move;

	/* The wait released the guard; a move now takes no mutex a present holds. */
	(void)queue;
	assert(test_display.flip.guard.held.value == 0);
	if (test_move_during_wait) {
		test_move_during_wait = false;
		memset(&move, 0, sizeof(move));
		move.lease = test_move_lease;
		move.generation = 1;
		move.x = 11;
		move.y = 21;
		test_move_error = test_operations->display->cursor_move(test_device, test_move_session, &move);
	}

	/* A stalled model lets the time run out. */
	display_test_ticks++;
	if (test_stall) {
		if (deadline != 0)
			display_test_ticks = deadline;
		return;
	}
	deliver_frame();
}

/* Delivers the selected PV's frame, at which the hardware takes the newest list. */
static void
deliver_frame(
	void)
{
	bool handled;

	/* The current list becomes the next one, and the selected source interrupts. */
	test_hvs[0x30 / 4] = test_hvs[0x20 / 4];
	test_pv[0][0x28 / 4] = 0x80;
	handled = test_display.timing_irq[0].service(test_display.timing_irq[0].owner);
	assert(handled);
}

/*
 * Verifies that cache publication uses a valid owned CPU allocation.
 */
void
kern_dcache_clean_range(
	const void *address,
	size_t bytes)
{
	/* Production copy and native flip always clean a nonempty persistent run. */
	assert(address != NULL && bytes != 0);
}

/*
 * Checks that a cursor copy discards a cacheable source's lines before reading it.
 */
void
kern_dcache_invalidate_range(
	const void *address,
	size_t bytes)
{
	/* The source rows are a nonempty interval of a live run. */
	assert(address != NULL && bytes != 0);
}

/*
 * Keeps architecture ordering outside this CPU-only register model.
 */
void
kern_io_write_barrier(
	void)
{
	/* Source ordering is exercised through actual MMIO calls and deferred IRQ adoption. */
}

/*
 * Supplies heap objects whose ownership is exercised by the actual callbacks.
 */
void *
kern_calloc(
	size_t count,
	size_t bytes)
{
	void *address;

	/* Host allocation preserves sanitizer visibility of resource and session lifetimes. */
	address = calloc(count, bytes);

	/* Succeeded: returns the host allocation or its ordinary NULL failure. */
	return address;
}

/*
 * Releases one actual session or descriptor allocation.
 */
void
kern_free(
	void *address)
{
	/* Hardware-facing private copy targets never reach this through ordinary close. */
	free(address);
}

/*
 * Allows each explicitly implemented diagnostic stage in this host fixture.
 */
bool
bcm2711_stage_allowed(
	const char *family,
	const char *stage)
{
	/* No hidden switch changes the production operation being exercised. */
	(void)family;
	(void)stage;

	/* Succeeded: the stage can proceed. */
	return true;
}

/* Reports the cost report as off in this model, which has no boot line. */
bool
bcm2711_stage_perf_on(
	void)
{
	/* No cadence is kept. */
	return false;
}

/*
 * Discards stage prose without changing any hardware or ownership observation.
 */
void
bcm2711_stage_mark(
	const char *family,
	const char *format,
	...)
{
	/* The model checks callback outcomes rather than parsing debug text. */
	(void)family;
	(void)format;
}

/*
 * Discards allocation diagnostics; unexpected release errors are asserted separately.
 */
void
kern_logf(
	const char *format,
	...)
{
	/* Log output is never used to manufacture a successful functional test. */
	(void)format;
}

/*
 * Supplies inert interrupt-controller line unmasking for direct source delivery.
 */
void
kern_irq_unmask(
	int irq)
{
	/* Direct service invocation remains independent of GIC routing. */
	(void)irq;
}

/*
 * Supplies inert interrupt-controller line masking for shared helper references.
 */
void
kern_irq_mask(
	int irq)
{
	/* Owned source W1C is modeled by register writes above. */
	(void)irq;
}

/*
 * Models the existing text backend's framebuffer suspension during a display lease.
 */
void
kern_text_suspend(
	void)
{
	/* Tracks whether any later restore could paint into graphics-owned memory. */
	test_text_suspended = true;

	/* Succeeded: the text backend is suspended. */
	return;
}

/*
 * Models text repaint only after a confirmed console restoration.
 */
void
kern_text_resume(
	void)
{
	/* Returns pixel output to the retained console. */
	test_text_suspended = false;

	/* Succeeded: the text backend is active. */
	return;
}

/*
 * Supplies a stationary clock for this fixture, which does not measure runtime.
 */
uint64_t
clock_ticks(
	void)
{
	/* No time elapses in the host's synchronous fake display. */
	return 0;
}
