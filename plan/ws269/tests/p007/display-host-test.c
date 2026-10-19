/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the PowerVR display node (ws269-p007) on a fake boot
 * framebuffer: the kernel calls it makes are stubs over host memory.
 */

#include "drivers/gpu/powervr/powervr.h"
#include <drivers/gpu/gpu.h>
#include <kern/boot.h>
#include <kern/lock.h>
#include <kern/platform.h>
#include <kern/pmem.h>
#include <uapi/errno.h>
#include <uapi/gpu-display.h>
#include <uapi/gpu-scanout.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The fake screen: 8x4 pixels, rows of 12 pixels, B, G, R, X. */
#define FB_WIDTH	8U
#define FB_HEIGHT	4U
#define FB_PITCH	48U
#define FB_PHYSICAL	0x40000000ULL

/* The screen's memory, the registered operations, and what the stubs saw. */
static uint8_t screen[FB_PITCH * FB_HEIGHT];
static const struct drv_gpu_ops *ops;
static void *node;
static int suspended;
static int resumed;
static int cleaned;
static int failures;

/* Records one failed expectation. */
static void
check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

int
kern_boot_framebuffer(const char *prefix, struct kern_boot_framebuffer *fb)
{
	check(strcmp(prefix, "riscv64.framebuffer") == 0, "framebuffer prefix");
	fb->physical_base = FB_PHYSICAL;
	fb->size = sizeof(screen);
	fb->width = FB_WIDTH;
	fb->height = FB_HEIGHT;
	fb->pitch = FB_PITCH;
	fb->format = KERN_BOOT_FRAMEBUFFER_BGRX8888;
	return 0;
}

void *
kern_pmem_to_kernel(hal_physaddr_t address)
{
	if (address == FB_PHYSICAL)
		return screen;
	return (void *)(uintptr_t)address;
}

int
kern_pmem_alloc(size_t size, size_t alignment, struct kern_pmem *run)
{
	void *memory;

	memory = aligned_alloc(alignment, size);
	if (memory == NULL)
		return ENOMEM;
	run->paddr = (hal_physaddr_t)(uintptr_t)memory;
	run->size = size;
	return 0;
}

int kern_pmem_free(struct kern_pmem *run) { free((void *)(uintptr_t)run->paddr); return 0; }
int kern_device_map(uint64_t a, size_t s, unsigned t, void **m) { (void)a; (void)s; (void)t; (void)m; return ENODEV; }
int kern_device_unmap(void *m, size_t s) { (void)m; (void)s; return 0; }
void kern_dcache_clean_range(const void *a, size_t s) { (void)a; (void)s; cleaned++; }
void *kern_calloc(size_t c, size_t s) { return calloc(c, s); }
void kern_free(void *p) { free(p); }
int mutex_init(struct mutex *m, enum lock_rank r, const char *n) { (void)m; (void)r; (void)n; return 0; }
void mutex_lock(struct mutex *m) { (void)m; }
void mutex_unlock(struct mutex *m) { (void)m; }
uint64_t sched_ticks(void) { return 5U; }
void kern_text_suspend(void) { suspended++; }
void kern_text_resume(void) { resumed++; }
void kern_logf(const char *f, ...) { va_list ap; va_start(ap, f); vprintf(f, ap); va_end(ap); }

int
kern_snprintf(char *b, size_t c, const char *f, ...)
{
	va_list ap;
	int n;

	va_start(ap, f);
	n = vsnprintf(b, c, f, ap);
	va_end(ap);
	return n;
}

int
drv_gpu_register(const struct drv_gpu_ops *o, void *p, struct drv_gpu_device **r)
{
	ops = o;
	node = p;
	*r = (struct drv_gpu_device *)p;
	return 0;
}

int
main(void)
{
	struct gpu_display_info info;
	struct gpu_display_mode mode;
	struct gpu_display_claim claim;
	struct gpu_display_release release;
	struct gpu_display_present present;
	struct gpu_display_wait wait;
	struct gpu_resource_create create;
	struct gpu_image_descriptor image;
	struct drv_gpu_scanout_backing backing;
	void *a;
	void *b;
	void *resource;
	void *imported;
	uint32_t frame[10 * 6];
	uint32_t pixel;
	unsigned i;

	/* Attach: registered once, a second attach refused. */
	check(drv_powervr_display_attach() == 0, "attach");
	check(drv_powervr_display_attach() == EALREADY, "second attach");
	check(ops != NULL && (ops->capabilities & GPU_CAP_DISPLAY) != 0U, "display capability");

	/* Query and mode. */
	memset(&info, 0, sizeof(info));
	check(ops->display->query(node, NULL, &info) == 0, "query");
	check(info.count == 1U && info.display_id == 1U && info.current_width == FB_WIDTH && info.current_height == FB_HEIGHT, "query values");
	memset(&mode, 0, sizeof(mode));
	mode.display_id = 1U;
	mode.generation = 1U;
	mode.operation = GPU_DISPLAY_MODE_VALIDATE;
	mode.width = FB_WIDTH + 1U;
	mode.height = 1U;
	check(ops->display->mode(node, NULL, &mode) == EINVAL, "mode too wide");
	mode.width = FB_WIDTH;
	check(ops->display->mode(node, NULL, &mode) == 0 && mode.refresh_millihz == 60000U, "mode fits");

	/* Two sessions; a resource of the first. */
	check(ops->open(node, &a) == 0 && ops->open(node, &b) == 0, "open");
	memset(&create, 0, sizeof(create));
	create.bytes = sizeof(frame);
	create.usage = GPU_RESOURCE_USAGE_STORAGE;
	check(ops->resource_create(node, a, &create, &resource) == 0, "resource");

	/* A 10x6 RGBA frame: clipped to 8x4, red and blue exchanged onto BGRX. */
	for (i = 0; i < 10U * 6U; i++)
		frame[i] = 0x11223344U + i;
	check(ops->resource_write(node, a, resource, 0, frame, sizeof(frame)) == 0, "write");
	check(ops->resource_write(node, b, resource, 0, frame, 4) == EINVAL, "foreign write");

	/* Claim: the console stops, the screen is cleared; a second claim is busy. */
	memset(screen, 0x5a, sizeof(screen));
	memset(&claim, 0, sizeof(claim));
	claim.display_id = 1U;
	claim.generation = 1U;
	check(ops->display->claim(node, a, &claim) == 0 && claim.lease != 0U, "claim");
	check(suspended == 1 && screen[0] == 0U && screen[FB_WIDTH * 4U] == 0x5aU, "console suspended, visible part cleared");
	check(ops->display->claim(node, b, &claim) == EBUSY, "second claim");

	/* Wait before any frame. */
	memset(&wait, 0, sizeof(wait));
	wait.lease = claim.lease;
	check(ops->display->wait(node, a, &wait) == EAGAIN, "wait before present");

	/* Present. */
	memset(&present, 0, sizeof(present));
	present.lease = claim.lease;
	present.generation = 1U;
	present.width = 10U;
	present.height = 6U;
	present.stride = 40U;
	present.format = GPU_PIXEL_RGBA8888;
	present.flags = GPU_DISPLAY_PRESENT_FIFO;
	check(ops->display->present(node, b, resource, &present) == EINVAL, "foreign resource");
	cleaned = 0;
	check(ops->display->present(node, a, resource, &present) == 0 && present.sequence == 1U, "present");
	check(cleaned == 1, "rows cleaned");
	memcpy(&pixel, screen + 3U * FB_PITCH + 7U * 4U, 4);
	check(pixel == 0x11693322U, "swapped pixel");
	memcpy(&pixel, screen + 3U * FB_PITCH + 8U * 4U, 4);
	check(pixel == 0x5a5a5a5aU, "clipped column untouched");

	/* Shared frames and imports wait for the render node. */
	present.flags = GPU_DISPLAY_PRESENT_FIFO | GPU_DISPLAY_PRESENT_BLOB;
	check(ops->display->present(node, a, resource, &present) == ENOTSUP, "blob refused");
	memset(&image, 0, sizeof(image));
	memset(&backing, 0, sizeof(backing));
	check(ops->scanout->import_image(node, a, &image, &backing, &imported) == ENOTSUP && imported == NULL, "import refused");

	/* Wait, then release by the wrong session and the holder. */
	wait.sequence = 1U;
	check(ops->display->wait(node, a, &wait) == 0 && wait.completed_sequence == 1U, "wait");
	memset(&release, 0, sizeof(release));
	release.lease = claim.lease;
	check(ops->display->release(node, b, &release) == EINVAL, "foreign release");
	check(ops->display->release(node, a, &release) == 0 && resumed == 1, "release resumes console");

	/* A holder that closes without releasing gives the screen back too. */
	check(ops->display->claim(node, b, &claim) == 0, "claim by b");
	ops->close(node, b);
	check(resumed == 2, "close resumes console");
	ops->resource_destroy(node, a, resource);
	ops->close(node, a);

	if (failures != 0)
		return 1;
	printf("display-host-test: PASS\n");
	return 0;
}
