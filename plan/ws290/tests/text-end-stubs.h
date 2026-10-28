/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The kernel and hardware stand-ins under the PC/AT console's end
 * (text-end-stubs.c), and what they let a scene set and see.
 */

#ifndef PLAN_WS290_TESTS_TEXT_END_STUBS_H
#define PLAN_WS290_TESTS_TEXT_END_STUBS_H

#include <stddef.h>
#include <stdint.h>

struct cdev_ops;

/* The boot framebuffer the scenes hand over: 640x480, a pitch of 640 pixels, at a high physical address. */
#define STUB_FB_WIDTH		640U
#define STUB_FB_HEIGHT		480U
#define STUB_FB_PITCH		640U
#define STUB_FB_BYTES		((uint64_t)STUB_FB_PITCH * STUB_FB_HEIGHT * 4U)
#define STUB_FB_PHYSICAL	0xc0000000ULL

/* The legacy aperture, which the hardware layer keeps mapped for good. */
#define STUB_LEGACY_PHYSICAL	0x000a0000ULL
#define STUB_LEGACY_BYTES	0x00020000ULL

/* How many map and unmap calls the log keeps. */
#define STUB_CALLS_MAX		64U

/* What one kernel mapping the stand-in made is. */
enum stub_kind {
	STUB_KIND_VIEW,
	STUB_KIND_WINDOW,
	STUB_KIND_FIXED
};

/* One call of kern_device_map() or kern_device_unmap(), as the stand-in saw it. */
struct stub_call {
	int unmap;
	uint64_t physical;
	size_t bytes;
	void *mapped;
	enum stub_kind kind;
	int result;
	unsigned cpu;
};

/* The state of the hardware layer's early console window. */
enum stub_window_state {
	STUB_WINDOW_HELD,
	STUB_WINDOW_GIVEN,
	STUB_WINDOW_REMOVED
};

/* What a scene sets before it starts. */
extern int stub_handoff;
extern int stub_quiet;
extern int stub_window_fixed;

/* What the stand-ins saw, and the failures found (by the stand-ins and by the scenes). */
extern unsigned stub_failures;
extern struct stub_call stub_calls[STUB_CALLS_MAX];
extern unsigned stub_call_count;
extern unsigned stub_bad_unmaps;
extern unsigned stub_mirrored;
extern unsigned stub_io_writes;
extern unsigned stub_resumes;
extern unsigned stub_mutex_locks;
extern enum stub_window_state stub_window_state;
extern uint32_t *stub_window;
extern uint8_t *stub_legacy;
extern const struct cdev_ops *stub_graphics_ops;
extern uint64_t stub_last_sleep;

/* The order of events across threads. */
extern unsigned stub_mutex_waits;
extern unsigned long stub_unlock_sequence[2];
extern unsigned long stub_acquire_sequence[2];

/* Hooks a scene may set: called on the next unmap, and on every sleep. */
extern void (*stub_unmap_hook)(void);
extern void (*stub_sleep_hook)(unsigned sleeps);

/* The threads the console asked for, in the order asked. */
extern void (*stub_thread_entry[4])(void *);
extern void *stub_thread_argument[4];
extern unsigned stub_thread_count;

/* The calling thread's processor number (0 for the main thread). */
extern __thread unsigned stub_cpu;

void stub_setup(void);
void stub_inject_map(unsigned nth, int error);
void stub_inject_unmap(unsigned nth, int error);
unsigned long stub_sequence_next(void);
unsigned stub_count(int unmap);
const struct stub_call *stub_find(int unmap, unsigned nth);
uint64_t stub_sum(const volatile void *pixels, size_t bytes);
void stub_fail(const char *what);

/* The host's services (text-end-host-os.c, built with the host's headers). */
void *text_end_host_pages(size_t bytes, size_t *length);
void text_end_host_read_only(void *pages, size_t length);
void text_end_host_pause(long nanoseconds);
void text_end_host_thread_start(void *(*entry)(void *));
void text_end_host_thread_join(void);

/* The text layer's own state, from the build of text.c that exposes it (text-end-text-peek.c). */
const volatile uint32_t *text_end_peek_pixels(void);
int text_end_peek_ended(void);
int text_end_peek_test_done(void);
int text_end_peek_test_result(void);

#endif
