/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the power button's release (ws182-p002): zedBSD's
 * events-zedbsd.c is built here with the records given by hand, and the
 * presses it passes to the compositor are counted.
 */

#include "userland/desktop/libkeiland-backend-zedbsd/events-zedbsd.c"

#include <stdlib.h>

/* The presses passed on, and the last button. */
static unsigned test_presses;
static unsigned test_button;

/* The failures seen. */
static unsigned test_failures;

static void test_power_button(void *data, unsigned button);
static void test_record(struct kl_backend *backend, const char *subject, uint64_t ms);
static void test_expect(const char *what, unsigned presses);

/*
 * Runs the cases and reports whether every one passed.
 */
int
main(void)
{
	struct kl_backend backend;

	/* A backend with only the power button's callback. */
	memset(&backend, 0, sizeof(backend));
	backend.events_descriptor = -1;
	backend.host.power_button = test_power_button;

	/* A tap: the press and its release 180 ms later are one press. */
	test_record(&backend, "power-button", 1000U);
	test_record(&backend, "power-button", 1180U);
	test_expect("tap", 1U);

	/* Another tap 3 s later is a new press, its release dropped. */
	test_record(&backend, "power-button", 4180U);
	test_record(&backend, "power-button", 4300U);
	test_expect("second tap", 2U);

	/* A press held 1.5 s: still one. */
	test_record(&backend, "power-button", 10000U);
	test_record(&backend, "power-button", 11500U);
	test_expect("held 1.5 s", 3U);

	/* A press held 2.5 s: the late release counts as a press (the dialog shown already takes it as nothing). */
	test_record(&backend, "power-button", 20000U);
	test_record(&backend, "power-button", 22500U);
	test_expect("held 2.5 s", 5U);

	/* A record after a dropped release is a new press. */
	test_record(&backend, "power-button", 30000U);
	test_record(&backend, "power-button", 30100U);
	test_record(&backend, "power-button", 30900U);
	test_expect("quick second press", 7U);

	/* The sleep button is not paired, and does not end a power button's pairing. */
	test_record(&backend, "sleep-button", 40000U);
	test_record(&backend, "sleep-button", 40100U);
	test_expect("sleep button", 9U);
	if (test_button != KL_BACKEND_BUTTON_SLEEP) {
		printf("FAIL sleep button told as %u\n", test_button);
		test_failures++;
	}

	/* The result. */
	if (test_failures != 0U) {
		printf("FAIL %u\n", test_failures);
		return EXIT_FAILURE;
	}

	/* Succeeded: every case passed. */
	printf("PASS host-power-release\n");
	return EXIT_SUCCESS;
}

/* Counts a press passed to the compositor. */
static void
test_power_button(
	void *data,
	unsigned button)
{
	(void)data;

	/* One more, and which. */
	test_presses++;
	test_button = button;
}

/* Hands one POWER PRESS record to the dispatch, at a time in milliseconds. */
static void
test_record(
	struct kl_backend *backend,
	const char *subject,
	uint64_t ms)
{
	struct system_event event;

	/* The record as the kernel writes it. */
	memset(&event, 0, sizeof(event));
	event.size = sizeof(event);
	event.class_bit = KERN_SYSTEM_EVENT_POWER;
	event.action = KERN_SYSTEM_EVENT_PRESS;
	event.value = 1;
	event.time_ns = ms * 1000000U;
	(void)snprintf(event.subject, sizeof(event.subject), "%s", subject);

	/* Dispatched. */
	(void)events_dispatch(backend, &event);
}

/* Compares the presses passed on so far with those expected. */
static void
test_expect(
	const char *what,
	unsigned presses)
{
	/* A difference fails. */
	if (test_presses != presses) {
		printf("FAIL %s: %u presses, expected %u\n", what, test_presses, presses);
		test_failures++;
		return;
	}

	/* Succeeded. */
	printf("ok %s\n", what);
}
