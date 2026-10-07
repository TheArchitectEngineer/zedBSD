/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the compositor's sleep rules (ws052-p012,
 * userland/desktop/wayland/sleep-rules.c): the time without input on the
 * adapter and on battery, the pending request's two frames and its time
 * limit, sending again while another request is answered, the growing
 * pauses and their end with input, an idle sleep that waits for new input,
 * a machine that cannot sleep, the short wakes, the rest after a wake that
 * was not the user's, the presses and keys around a sleep, and the
 * reasons' words.
 */

#include "userland/desktop/wayland/sleep-rules.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The number of failed checks. */
static int test_failures;

static void check(int condition, const char *what);
static void outcome_set(struct kl_backend_power_outcome *outcome, enum kl_backend_sleep_kind kind, int error, const char *wake, const char *device);

/*
 * Runs the checks.
 */
int
main(void)
{
	struct kwl_sleep sleep;
	struct kl_backend_power_outcome outcome;
	enum kwl_sleep_reason reason;
	char name[KL_BACKEND_POWER_DEVICE_MAX];
	unsigned actions;
	uint64_t now;
	int sent;

	/* The time without input: the adapter's, the battery's, unknown as the adapter, 0 never. */
	check(kwl_sleep_idle_limit_ms(KL_BACKEND_POWER_SOURCE_AC, 30, 15) == 30U * 60000U, "the adapter's time");
	check(kwl_sleep_idle_limit_ms(KL_BACKEND_POWER_SOURCE_BATTERY, 30, 15) == 15U * 60000U, "the battery's time");
	check(kwl_sleep_idle_limit_ms(KL_BACKEND_POWER_SOURCE_UNKNOWN, 30, 15) == 30U * 60000U, "an unknown source counts as the adapter");
	check(kwl_sleep_idle_limit_ms(KL_BACKEND_POWER_SOURCE_AC, 0, 15) == 0U, "0 is never");

	/* A pending sleep sends after two frames of the lock screen. */
	memset(&sleep, 0, sizeof(sleep));
	now = 1000000U;
	check(kwl_sleep_may_begin(&sleep, now), "an idle sleep may begin");
	kwl_sleep_begin(&sleep, KWL_SLEEP_VIA_LID, now);
	check(!kwl_sleep_may_begin(&sleep, now), "one sleep at a time");
	kwl_sleep_framed(&sleep, 40U);
	kwl_sleep_framed(&sleep, 41U);
	check(sleep.pending_frame == 40U, "the frames count from the lock screen's first");
	check(kwl_sleep_pending_step(&sleep, 41U, now + 20U) == KWL_SLEEP_STEP_WAIT, "one frame waits");
	check(kwl_sleep_pending_step(&sleep, 42U, now + 40U) == KWL_SLEEP_STEP_SEND, "two frames send");
	check(kwl_sleep_pending_step(&sleep, 41U, now + KWL_SLEEP_LOCK_MS + 1U) == KWL_SLEEP_STEP_GIVE_UP, "no frames in time give up");
	check(kwl_sleep_button_ignored(&sleep, now), "a press while pending is not the user's");
	check(kwl_sleep_keys_held(&sleep, now), "keys are held while pending");

	/* Another request awaited: asked again, until KWL_SLEEP_SEND_MS. */
	sent = kwl_sleep_sent(&sleep, EBUSY, now);
	check(sent == 0, "busy asks again");
	sent = kwl_sleep_sent(&sleep, EBUSY, now + KWL_SLEEP_SEND_MS);
	check(sent == 0, "busy asks again within the limit");
	sent = kwl_sleep_sent(&sleep, EBUSY, now + KWL_SLEEP_SEND_MS + 1U);
	check(sent == -1, "busy past the limit fails");

	/* Sent: waiting; a sleep already asked counts as sent. */
	sent = kwl_sleep_sent(&sleep, 0, now + 50U);
	check(sent == 1 && sleep.state == KWL_SLEEP_WAITING, "sent waits for the answer");
	kwl_sleep_dropped(&sleep);
	kwl_sleep_begin(&sleep, KWL_SLEEP_VIA_BUTTON, now);
	sent = kwl_sleep_sent(&sleep, EALREADY, now + 50U);
	check(sent == 1, "a sleep asked already is waited for");

	/* Slept and woken by the lid: the failures end, no rest, the screen's clock starts again. */
	outcome_set(&outcome, KL_BACKEND_SLEEP_SLEPT, 0, "lid", "");
	actions = kwl_sleep_answered(&sleep, &outcome, 1, now + 60000U, now);
	check((actions & KWL_SLEEP_DO_WOKE) != 0U && (actions & KWL_SLEEP_DO_SAY) == 0U, "a sleep that woke says nothing");
	check(sleep.state == KWL_SLEEP_IDLE && !sleep.resting, "the lid's wake is the user's: no rest");
	check(kwl_sleep_button_ignored(&sleep, now + 60000U + KWL_SLEEP_BUTTON_QUIET_MS - 1U), "a press just after the answer is ignored");
	check(!kwl_sleep_button_ignored(&sleep, now + 60000U + KWL_SLEEP_BUTTON_QUIET_MS), "a press later is the user's");
	check(kwl_sleep_keys_held(&sleep, now + 60000U + KWL_SLEEP_KEY_QUIET_MS - 1U), "a key just after the answer is held");
	check(!kwl_sleep_keys_held(&sleep, now + 60000U + KWL_SLEEP_KEY_QUIET_MS), "a key later goes to the screen");

	/* Woken by a timer with the lid open: the rest, KWL_SLEEP_REST_MS without input. */
	now += 100000U;
	kwl_sleep_begin(&sleep, KWL_SLEEP_VIA_IDLE, now);
	(void)kwl_sleep_sent(&sleep, 0, now);
	outcome_set(&outcome, KL_BACKEND_SLEEP_SLEPT, 0, "timer", "");
	(void)kwl_sleep_answered(&sleep, &outcome, 1, now + 600000U, now);
	check(sleep.resting, "a timer's wake rests");
	check(!kwl_sleep_rest_due(&sleep, now + 600000U + KWL_SLEEP_REST_MS - 1U, now + 600000U), "the rest is not over yet");
	check(kwl_sleep_rest_due(&sleep, now + 600000U + KWL_SLEEP_REST_MS, now + 600000U), "the rest is over");
	kwl_sleep_input(&sleep, now + 600001U);
	check(!sleep.resting, "input ends the rest");

	/* Short wakes: three in a row pause like a failure. */
	now += 1000000U;
	outcome_set(&outcome, KL_BACKEND_SLEEP_SLEPT, 0, "lid", "");
	kwl_sleep_begin(&sleep, KWL_SLEEP_VIA_LID, now);
	(void)kwl_sleep_sent(&sleep, 0, now);
	(void)kwl_sleep_answered(&sleep, &outcome, 0, now + 1000U, now);
	check(sleep.short_wakes == 1U, "one short wake");
	kwl_sleep_begin(&sleep, KWL_SLEEP_VIA_LID, now + 2000U);
	(void)kwl_sleep_sent(&sleep, 0, now + 2000U);
	(void)kwl_sleep_answered(&sleep, &outcome, 0, now + 3000U, now);
	check(sleep.short_wakes == 2U, "two short wakes");
	kwl_sleep_begin(&sleep, KWL_SLEEP_VIA_LID, now + 4000U);
	(void)kwl_sleep_sent(&sleep, 0, now + 4000U);
	(void)kwl_sleep_answered(&sleep, &outcome, 0, now + 5000U, now);
	check(!kwl_sleep_may_begin(&sleep, now + 5000U + KWL_SLEEP_PAUSE_FIRST_MS - 1U), "the third pauses");
	check(kwl_sleep_may_begin(&sleep, now + 5000U + KWL_SLEEP_PAUSE_FIRST_MS), "the pause ends");
	kwl_sleep_begin(&sleep, KWL_SLEEP_VIA_LID, now + 900000U);
	(void)kwl_sleep_sent(&sleep, 0, now + 900000U);
	(void)kwl_sleep_answered(&sleep, &outcome, 0, now + 900000U + KWL_SLEEP_SHORT_MS, now);
	check(sleep.short_wakes == 0U, "a long sleep ends the short wakes");

	/* Failures: 30 s, 2 min, 10 min, 10 min; input starts them again. */
	memset(&sleep, 0, sizeof(sleep));
	now = 5000000U;
	kwl_sleep_begin(&sleep, KWL_SLEEP_VIA_LID, now);
	kwl_sleep_failed(&sleep, now, 1U);
	check(sleep.pause_until_ms == now + KWL_SLEEP_PAUSE_FIRST_MS, "the first pause");
	kwl_sleep_begin(&sleep, KWL_SLEEP_VIA_LID, now);
	kwl_sleep_failed(&sleep, now, 1U);
	check(sleep.pause_until_ms == now + KWL_SLEEP_PAUSE_SECOND_MS, "the second pause");
	kwl_sleep_begin(&sleep, KWL_SLEEP_VIA_LID, now);
	kwl_sleep_failed(&sleep, now, 1U);
	kwl_sleep_begin(&sleep, KWL_SLEEP_VIA_LID, now);
	kwl_sleep_failed(&sleep, now, 1U);
	check(sleep.pause_until_ms == now + KWL_SLEEP_PAUSE_LAST_MS, "the last pause stays");
	kwl_sleep_input(&sleep, 2U);
	check(sleep.failures == 0U && kwl_sleep_may_begin(&sleep, now), "input ends the pauses");

	/* A failed idle sleep waits for new input. */
	kwl_sleep_begin(&sleep, KWL_SLEEP_VIA_IDLE, now);
	kwl_sleep_failed(&sleep, now, 7U);
	check(!kwl_sleep_idle_allowed(&sleep, 7U), "the same input does not try again");
	check(kwl_sleep_idle_allowed(&sleep, 8U), "newer input tries again");

	/* Refused by a device: said, notified once, a failure; a driver without the part pauses the longest. */
	memset(&sleep, 0, sizeof(sleep));
	kwl_sleep_begin(&sleep, KWL_SLEEP_VIA_BUTTON, now);
	(void)kwl_sleep_sent(&sleep, 0, now);
	outcome_set(&outcome, KL_BACKEND_SLEEP_DEVICE, EBUSY, "", "pci 0000:00:14.3 intel-ax211");
	actions = kwl_sleep_answered(&sleep, &outcome, 1, now + 100U, now);
	check((actions & KWL_SLEEP_DO_SAY) != 0U && (actions & KWL_SLEEP_DO_NOTIFY) != 0U, "a refusal is said and notified");
	check(sleep.failures == 1U && sleep.state == KWL_SLEEP_IDLE, "a refusal is a failure");
	kwl_sleep_begin(&sleep, KWL_SLEEP_VIA_BUTTON, now + 200000U);
	(void)kwl_sleep_sent(&sleep, 0, now + 200000U);
	actions = kwl_sleep_answered(&sleep, &outcome, 1, now + 200100U, now);
	check((actions & KWL_SLEEP_DO_SAY) != 0U && (actions & KWL_SLEEP_DO_NOTIFY) == 0U, "the same reason is notified once");
	kwl_sleep_begin(&sleep, KWL_SLEEP_VIA_BUTTON, now + 400000U);
	(void)kwl_sleep_sent(&sleep, 0, now + 400000U);
	outcome_set(&outcome, KL_BACKEND_SLEEP_DEVICE, EOPNOTSUPP, "", "pci 0000:00:1f.6 e1000e");
	(void)kwl_sleep_answered(&sleep, &outcome, 1, now + 400100U, now);
	check(sleep.pause_until_ms == now + 400100U + KWL_SLEEP_PAUSE_LAST_MS, "a driver without the part pauses the longest");

	/* A machine that cannot sleep is not asked again. */
	memset(&sleep, 0, sizeof(sleep));
	kwl_sleep_begin(&sleep, KWL_SLEEP_VIA_LID, now);
	(void)kwl_sleep_sent(&sleep, 0, now);
	outcome_set(&outcome, KL_BACKEND_SLEEP_UNSUPPORTED, EOPNOTSUPP, "", "");
	(void)kwl_sleep_answered(&sleep, &outcome, 1, now + 1U, now);
	check(sleep.unsupported && !kwl_sleep_may_begin(&sleep, now + 100000000U), "unsupported is for good");

	/* Cancelled and busy are not failures and say nothing. */
	memset(&sleep, 0, sizeof(sleep));
	kwl_sleep_begin(&sleep, KWL_SLEEP_VIA_LID, now);
	(void)kwl_sleep_sent(&sleep, 0, now);
	outcome_set(&outcome, KL_BACKEND_SLEEP_CANCELLED, ECANCELED, "", "");
	actions = kwl_sleep_answered(&sleep, &outcome, 1, now + 1U, now);
	check(actions == 0U && sleep.failures == 0U, "a cancel says nothing and is not a failure");

	/* Nothing can be asked: dropped, no failure. */
	kwl_sleep_begin(&sleep, KWL_SLEEP_VIA_LID, now);
	sent = kwl_sleep_sent(&sleep, ENOTSUP, now);
	check(sent == -2 && sleep.state == KWL_SLEEP_IDLE && sleep.failures == 0U, "ENOTSUP drops the sleep");

	/* The reasons. */
	outcome_set(&outcome, KL_BACKEND_SLEEP_DEVICE, EBUSY, "", "pci 0000:00:14.3 intel-ax211");
	check(kwl_sleep_reason_of(&outcome, name, sizeof(name)) == KWL_SLEEP_REASON_WIFI, "the Wi-Fi driver busy");
	outcome_set(&outcome, KL_BACKEND_SLEEP_DEVICE, EBUSY, "", "pci 0000:01:00.0 nvme");
	check(kwl_sleep_reason_of(&outcome, name, sizeof(name)) == KWL_SLEEP_REASON_DISK, "the disk busy");
	outcome_set(&outcome, KL_BACKEND_SLEEP_DEVICE, EBUSY, "", "pci 0000:00:0d.0 xhci");
	check(kwl_sleep_reason_of(&outcome, name, sizeof(name)) == KWL_SLEEP_REASON_USB, "USB busy");
	outcome_set(&outcome, KL_BACKEND_SLEEP_DEVICE, EBUSY, "", "pci 0000:00:02.0 i915");
	check(kwl_sleep_reason_of(&outcome, name, sizeof(name)) == KWL_SLEEP_REASON_DISPLAY, "the display busy");
	outcome_set(&outcome, KL_BACKEND_SLEEP_DEVICE, EBUSY, "", "pci 0000:00:1f.3 hda");
	reason = kwl_sleep_reason_of(&outcome, name, sizeof(name));
	check(reason == KWL_SLEEP_REASON_DRIVER_BUSY && strcmp(name, "hda") == 0, "another driver busy, named");
	outcome_set(&outcome, KL_BACKEND_SLEEP_DEVICE, EOPNOTSUPP, "", "pci 0000:00:1f.6 e1000e");
	reason = kwl_sleep_reason_of(&outcome, name, sizeof(name));
	check(reason == KWL_SLEEP_REASON_DRIVER_CANNOT && strcmp(name, "e1000e") == 0, "a driver without the part, named");
	outcome_set(&outcome, KL_BACKEND_SLEEP_DEVICE, EIO, "", "lps0");
	reason = kwl_sleep_reason_of(&outcome, name, sizeof(name));
	check(reason == KWL_SLEEP_REASON_DEVICE && strcmp(name, "lps0") == 0, "a part that is not a PCI device, named as is");
	outcome_set(&outcome, KL_BACKEND_SLEEP_NETWORK, EBUSY, "", "");
	outcome.network = KL_BACKEND_SLEEP_NETWORK_CONFIRMED;
	check(kwl_sleep_reason_of(&outcome, name, sizeof(name)) == KWL_SLEEP_REASON_CONFIRMED, "a change to confirm");
	outcome.network = KL_BACKEND_SLEEP_NETWORK_BUSY;
	check(kwl_sleep_reason_of(&outcome, name, sizeof(name)) == KWL_SLEEP_REASON_WIFI_BUSY, "the user's Wi-Fi change");
	outcome.network = KL_BACKEND_SLEEP_NETWORK_RADIO;
	check(kwl_sleep_reason_of(&outcome, name, sizeof(name)) == KWL_SLEEP_REASON_WIFI, "the radio would not go off");
	outcome_set(&outcome, KL_BACKEND_SLEEP_SLEPT, 0, "lid", "");
	outcome.resume_error = EIO;
	check(kwl_sleep_reason_of(&outcome, name, sizeof(name)) == KWL_SLEEP_REASON_RESUME, "a device that did not come back");
	outcome_set(&outcome, KL_BACKEND_SLEEP_BUSY, EBUSY, "", "");
	check(kwl_sleep_reason_of(&outcome, name, sizeof(name)) == KWL_SLEEP_REASON_NONE, "busy says nothing");
	check(strcmp(kwl_sleep_via_name(KWL_SLEEP_VIA_BUTTON), "sleep-button") == 0, "the cause's name");

	/* Reports the outcome. */
	if (test_failures != 0) {
		fprintf(stderr, "host-sleep-rules: %d checks failed\n", test_failures);
		return 1;
	}

	/* Succeeded: every check passed. */
	printf("host-sleep-rules: PASS\n");
	return 0;
}

/* Counts and reports one failed check. */
static void
check(
	int condition,
	const char *what)
{
	/* A failed check is reported and counted. */
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", what);
		test_failures++;
	}
}

/* Fills an outcome of a kind, an error, a wake and a device. */
static void
outcome_set(
	struct kl_backend_power_outcome *outcome,
	enum kl_backend_sleep_kind kind,
	int error,
	const char *wake,
	const char *device)
{
	/* The fields the rules read. */
	memset(outcome, 0, sizeof(*outcome));
	outcome->kind = kind;
	outcome->error = error;
	(void)snprintf(outcome->wake, sizeof(outcome->wake), "%s", wake);
	(void)snprintf(outcome->device, sizeof(outcome->device), "%s", device);
}
