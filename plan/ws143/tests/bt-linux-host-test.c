/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the Linux Bluetooth backend (ws143-p007, phase.md
 * section 9.7): drives userland/desktop/libkeiland-backend-linux's
 * bluetooth-linux.c through the backend's calls against
 * bt-linux-fake-bluez.py on a private bus, and steers the fake through
 * org.keiland.Test.Control on a connection of its own.
 *
 * usage: bt-linux-host-test   (built with -DBT_DBUS_SOCKET naming the
 * private bus's socket, with the fake already running on it)
 *
 * Each check prints "ok NAME" or "FAIL NAME: why"; the exit status is 1
 * when any failed.
 */

#include "userland/desktop/libkeiland-backend/keiland-backend.h"
#include "userland/desktop/libkeiland-backend-linux/dbus-linux.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The fake's control: its bus name, object and interface. */
#define FAKE_NAME		"org.keiland.TestFake"
#define FAKE_PATH		"/test"
#define FAKE_INTERFACE		"org.keiland.Test"

/* The addresses of the fake's first devices: a paired keyboard, a mouse, an LE mouse. */
#define KEYBOARD		"AA:00:00:00:00:01"
#define MOUSE			"AA:00:00:00:00:02"
#define LASER			"CC:00:00:00:00:03"

/* What the test saw of the backend: the last result and the questions in order. */
struct seen {
	unsigned results;
	uint32_t result_id;
	int result_error;
	char result_reason[KL_BACKEND_BT_REASON_MAX];
	unsigned questions;
	struct kl_backend_bluetooth_question question;
	unsigned ends;
	uint32_t last_end;
};

/* The answer to a call of the control connection: its serial, and the error name seen. */
struct foreign {
	uint32_t serial;
	char error[128];
	int answered;
};

/* What a predicate of pump() is asked about. */
typedef int (*until_fn)(struct kl_backend_bluetooth *bluetooth, const struct seen *seen, const void *argument);

/* The control connection to the fake. */
static struct linux_dbus control;

/* How many checks failed. */
static unsigned failures;

static void check(int condition, const char *name, const char *format, ...);
static int fake(const char *command, const char *argument, char *answer, size_t size);
static void sleep_ms(unsigned milliseconds);
static int pump(struct kl_backend_bluetooth *bluetooth, struct seen *seen, unsigned milliseconds, until_fn until, const void *argument);
static int until_never(struct kl_backend_bluetooth *bluetooth, const struct seen *seen, const void *argument);
static int until_ready(struct kl_backend_bluetooth *bluetooth, const struct seen *seen, const void *argument);
static int until_result(struct kl_backend_bluetooth *bluetooth, const struct seen *seen, const void *argument);
static int until_question(struct kl_backend_bluetooth *bluetooth, const struct seen *seen, const void *argument);
static int until_device(struct kl_backend_bluetooth *bluetooth, const struct seen *seen, const void *argument);
static int until_absent(struct kl_backend_bluetooth *bluetooth, const struct seen *seen, const void *argument);
static int until_state(struct kl_backend_bluetooth *bluetooth, const struct seen *seen, const void *argument);
static int until_agent_result(struct kl_backend_bluetooth *bluetooth, const struct seen *seen, const void *argument);
static int find_device(struct kl_backend_bluetooth *bluetooth, const char *address, struct kl_backend_bluetooth_device *device);
static int request(struct kl_backend_bluetooth *bluetooth, struct seen *seen, unsigned kind, const char *address, unsigned type);
static int calls_contain(const char *text);
static int foreign_agent_call(struct kl_backend_bluetooth *bluetooth, struct seen *seen);
static int foreign_message(struct dbus_reply *message, void *data);

/*
 * Runs the checks in order; each starts from what the one before left.
 */
int
main(
	void)
{
	struct kl_backend_bluetooth *bluetooth;
	struct kl_backend_bluetooth_state state;
	struct kl_backend_bluetooth_device devices[KL_BACKEND_BT_DEVICES_MAX];
	struct kl_backend_bluetooth_device device;
	struct seen seen;
	char answer[4096];
	char argument[256];
	unsigned results;
	unsigned target;
	size_t count;
	size_t index;
	int found;
	int error;

	/* The control connection and the backend. */
	error = dbus_open_address(&control, BT_DBUS_SOCKET);
	if (error != 0) {
		printf("FAIL open control: %d\n", error);
		return 1;
	}
	bluetooth = kl_backend_bluetooth_open();
	memset(&seen, 0, sizeof(seen));

	/* 1. The adapter and the fake's three devices, the paired first, and the agent. */
	found = pump(bluetooth, &seen, 8000, until_ready, NULL);
	(void)pump(bluetooth, &seen, 500, until_never, NULL);
	kl_backend_bluetooth_get_state(bluetooth, &state);
	check(found, "ready", "state=%u count=%zu agent=%u", state.state, kl_backend_bluetooth_get_devices(bluetooth, devices, 0), state.agent);
	check(state.reachable && state.state == KL_BACKEND_BT_ON && state.power, "state-on", "reachable=%u state=%u power=%u", state.reachable, state.state, state.power);
	check(strcmp(state.address, "00:11:22:33:44:55") == 0 && strcmp(state.name, "fake-hci0") == 0, "adapter-names", "%s %s", state.address, state.name);
	check(state.agent == 1U, "agent-default", "agent=%u", state.agent);
	count = kl_backend_bluetooth_get_devices(bluetooth, devices, KL_BACKEND_BT_DEVICES_MAX);
	check(count == 3U && strcmp(devices[0].address, KEYBOARD) == 0, "paired-first", "count=%zu first=%s", count, devices[0].address);
	check(devices[0].paired && devices[0].battery == 80 && devices[0].kind == KL_BACKEND_BT_KIND_KEYBOARD, "keyboard", "paired=%u battery=%d kind=%u", devices[0].paired, devices[0].battery, devices[0].kind);
	(void)find_device(bluetooth, LASER, &device);
	check(device.type == KL_BACKEND_BT_LE_RANDOM && strcmp(device.name, "Laser L") == 0, "le-random", "type=%u name=%s", device.type, device.name);
	(void)find_device(bluetooth, MOUSE, &device);
	check(device.type == KL_BACKEND_BT_BREDR && device.rssi == -50 && device.battery == -1, "mouse", "type=%u rssi=%d battery=%d", device.type, device.rssi, device.battery);
	check(calls_contain("RegisterAgent DisplayYesNo"), "capability", "no RegisterAgent DisplayYesNo");

	/* 2. Signals: a device added, a signal strength changed and invalidated, a connection. */
	(void)fake("add", "address=AA:00:00:00:00:04;name=Nano N;icon=phone;paired=0;rssi=-60;kind=bredr;announce=1", answer, sizeof(answer));
	found = pump(bluetooth, &seen, 3000, until_device, "AA:00:00:00:00:04");
	(void)find_device(bluetooth, "AA:00:00:00:00:04", &device);
	check(found && device.kind == KL_BACKEND_BT_KIND_PHONE, "interfaces-added", "found=%d kind=%u", found, device.kind);
	(void)fake("rssi", "address=" MOUSE ";value=-40", answer, sizeof(answer));
	(void)pump(bluetooth, &seen, 2500, until_never, NULL);
	(void)find_device(bluetooth, MOUSE, &device);
	check(device.rssi == -40, "rssi-changed", "rssi=%d", device.rssi);
	(void)fake("invalidate", "address=" MOUSE, answer, sizeof(answer));
	(void)pump(bluetooth, &seen, 2500, until_never, NULL);
	(void)find_device(bluetooth, MOUSE, &device);
	check(device.rssi == 0, "rssi-invalidated", "rssi=%d", device.rssi);
	(void)fake("set", "address=" LASER ";name=Connected;value=1", answer, sizeof(answer));
	(void)pump(bluetooth, &seen, 500, until_never, NULL);
	(void)find_device(bluetooth, LASER, &device);
	check(device.connected == 1U, "connected-signal", "connected=%u", device.connected);
	(void)fake("set", "address=" LASER ";name=Connected;value=0", answer, sizeof(answer));
	(void)pump(bluetooth, &seen, 500, until_never, NULL);
	(void)find_device(bluetooth, LASER, &device);
	check(device.connected == 0U, "disconnected-signal", "connected=%u", device.connected);
	(void)fake("set", "address=" LASER ";name=Connected;value=1", answer, sizeof(answer));
	(void)pump(bluetooth, &seen, 500, until_never, NULL);

	/* 3. A device the backend never heard of changes: it is read whole. */
	(void)fake("add", "address=AA:00:00:00:00:05;name=Quiet Q;paired=0;rssi=-80;kind=bredr;announce=0", answer, sizeof(answer));
	(void)fake("rssi", "address=AA:00:00:00:00:05;value=-75", answer, sizeof(answer));
	found = pump(bluetooth, &seen, 3000, until_device, "AA:00:00:00:00:05");
	check(found, "unknown-device-read", "not listed");

	/* 4. A pairing with a number compared, answered yes: paired, trusted, connected. */
	(void)fake("script", "confirm", answer, sizeof(answer));
	error = request(bluetooth, &seen, KL_BACKEND_BT_PAIR, MOUSE, KL_BACKEND_BT_BREDR);
	found = pump(bluetooth, &seen, 3000, until_question, NULL);
	kl_backend_bluetooth_get_state(bluetooth, &state);
	check(error == 0 && found && seen.question.kind == KL_BACKEND_BT_ASK_CONFIRM && seen.question.number == 123456U && seen.question.own == 1U, "pair-question", "error=%d found=%d kind=%u number=%u own=%u", error, found, seen.question.kind, seen.question.number, seen.question.own);
	check(state.pairing == 1U, "pairing-state", "pairing=%u", state.pairing);
	check(strcmp(seen.question.address, MOUSE) == 0 && strcmp(seen.question.name, "Mouse M") == 0, "question-device", "%s %s", seen.question.address, seen.question.name);
	error = kl_backend_bluetooth_answer(bluetooth, seen.question.id, 1U);
	found = pump(bluetooth, &seen, 3000, until_result, NULL);
	check(error == 0 && found && seen.result_error == 0, "pair-yes", "answer=%d found=%d error=%d reason=%s", error, found, seen.result_error, seen.result_reason);
	(void)fake("query", "address=" MOUSE, answer, sizeof(answer));
	check(strcmp(answer, "paired=1 trusted=1 connected=1") == 0, "pair-trust-connect", "%s", answer);

	/* 5. Answered no. */
	(void)fake("add", "address=AA:00:00:00:00:06;name=Other O;paired=0;rssi=-55;kind=bredr;announce=1", answer, sizeof(answer));
	(void)pump(bluetooth, &seen, 2000, until_device, "AA:00:00:00:00:06");
	error = request(bluetooth, &seen, KL_BACKEND_BT_PAIR, "AA:00:00:00:00:06", KL_BACKEND_BT_BREDR);
	found = pump(bluetooth, &seen, 3000, until_question, NULL);
	(void)kl_backend_bluetooth_answer(bluetooth, seen.question.id, 0U);
	found = pump(bluetooth, &seen, 3000, until_result, NULL);
	check(error == 0 && found && seen.result_error == ECONNREFUSED, "pair-no", "error=%d found=%d result=%d", error, found, seen.result_error);

	/* 5a. No while BlueZ would wait for the device's side (btvirt, T1-604): the pairing ends at once, refused, and BlueZ is told to stop it. */
	(void)fake("script", "confirm-hold", answer, sizeof(answer));
	error = request(bluetooth, &seen, KL_BACKEND_BT_PAIR, "AA:00:00:00:00:06", KL_BACKEND_BT_BREDR);
	found = pump(bluetooth, &seen, 3000, until_question, NULL);
	(void)kl_backend_bluetooth_answer(bluetooth, seen.question.id, 0U);
	found = pump(bluetooth, &seen, 1000, until_result, NULL);
	kl_backend_bluetooth_get_state(bluetooth, &state);
	check(error == 0 && found && seen.result_error == ECONNREFUSED && strcmp(seen.result_reason, "rejected") == 0 && state.pairing == 0U, "no-ends-at-once", "error=%d found=%d result=%d reason=%s pairing=%u", error, found, seen.result_error, seen.result_reason, state.pairing);
	(void)fake("agent-result", "", answer, sizeof(answer));
	check(strcmp(answer, "org.bluez.Error.Rejected") == 0, "no-rejected", "%s", answer);
	results = seen.results;
	(void)pump(bluetooth, &seen, 500, until_never, NULL);
	check(calls_contain("CancelPairing AA:00:00:00:00:06") && seen.results == results, "no-cancels-pairing", "results=%u then %u", results, seen.results);

	/* 5b. Pair fails while its question is asked and BlueZ does not cancel the question: it ends with the result, and its call is let go. */
	(void)fake("script", "confirm-fail", answer, sizeof(answer));
	seen.ends = 0U;
	(void)request(bluetooth, &seen, KL_BACKEND_BT_PAIR, "AA:00:00:00:00:06", KL_BACKEND_BT_BREDR);
	found = pump(bluetooth, &seen, 3000, until_result, NULL);
	check(found && seen.result_error == ECONNREFUSED && seen.questions == 1U && seen.ends > 0U && seen.last_end == seen.question.id, "fail-ends-question", "result=%d questions=%u ends=%u end=%u question=%u", seen.result_error, seen.questions, seen.ends, seen.last_end, seen.question.id);
	(void)pump(bluetooth, &seen, 1000, until_agent_result, NULL);
	(void)fake("agent-result", "", answer, sizeof(answer));
	check(strcmp(answer, "org.bluez.Error.Canceled") == 0, "fail-lets-call-go", "%s", answer);

	/* 5c. The next pairing is asked at once (no call of the failed one still held). */
	(void)fake("script", "confirm", answer, sizeof(answer));
	(void)request(bluetooth, &seen, KL_BACKEND_BT_PAIR, "AA:00:00:00:00:06", KL_BACKEND_BT_BREDR);
	found = pump(bluetooth, &seen, 3000, until_question, NULL);
	check(found && seen.question.kind == KL_BACKEND_BT_ASK_CONFIRM && seen.question.number == 123456U, "next-pair-asked", "found=%d kind=%u number=%u", found, seen.question.kind, seen.question.number);
	(void)kl_backend_bluetooth_answer(bluetooth, seen.question.id, 0U);
	found = pump(bluetooth, &seen, 1000, until_result, NULL);
	(void)pump(bluetooth, &seen, 300, until_never, NULL);
	check(found && seen.result_error == ECONNREFUSED, "next-pair-no", "result=%d", seen.result_error);

	/* 6. BlueZ's plain Failed: its text is the reason, and a page timeout is a device that did not answer. */
	(void)fake("script", "fail:org.bluez.Error.Failed:br-connection-page-timeout", answer, sizeof(answer));
	(void)request(bluetooth, &seen, KL_BACKEND_BT_PAIR, "AA:00:00:00:00:06", KL_BACKEND_BT_BREDR);
	found = pump(bluetooth, &seen, 3000, until_result, NULL);
	check(found && seen.result_error == ETIMEDOUT && strcmp(seen.result_reason, "br-connection-page-timeout") == 0, "pair-failed-text", "result=%d reason=%s", seen.result_error, seen.result_reason);

	/* 7. A PIN to type: refused as what this computer cannot do, and the reason says so. */
	(void)fake("script", "pin", answer, sizeof(answer));
	(void)request(bluetooth, &seen, KL_BACKEND_BT_PAIR, "AA:00:00:00:00:06", KL_BACKEND_BT_BREDR);
	found = pump(bluetooth, &seen, 3000, until_result, NULL);
	(void)fake("agent-result", "", answer, sizeof(answer));
	check(found && seen.result_error == ENOTSUP && strcmp(seen.result_reason, "input-unsupported") == 0, "pin-refused", "result=%d reason=%s", seen.result_error, seen.result_reason);
	check(strcmp(answer, "org.bluez.Error.Rejected") == 0, "pin-rejected", "%s", answer);

	/* 8. A number to type on the device: shown, then ended when the pairing ends. */
	(void)fake("script", "passkey", answer, sizeof(answer));
	seen.ends = 0U;
	(void)request(bluetooth, &seen, KL_BACKEND_BT_PAIR, "AA:00:00:00:00:06", KL_BACKEND_BT_BREDR);
	found = pump(bluetooth, &seen, 3000, until_question, NULL);
	check(found && seen.question.kind == KL_BACKEND_BT_ASK_PASSKEY && seen.question.number == 654321U, "passkey-shown", "kind=%u number=%u", seen.question.kind, seen.question.number);
	found = pump(bluetooth, &seen, 3000, until_result, NULL);
	check(found && seen.result_error == 0 && seen.ends > 0U && seen.last_end == seen.question.id, "passkey-ended", "result=%d ends=%u", seen.result_error, seen.ends);

	/* 9. This program gives up its pairing: cancelled at BlueZ, ECANCELED at once. */
	(void)fake("script", "hang", answer, sizeof(answer));
	(void)request(bluetooth, &seen, KL_BACKEND_BT_PAIR, "AA:00:00:00:00:04", KL_BACKEND_BT_BREDR);
	(void)pump(bluetooth, &seen, 300, until_never, NULL);
	error = kl_backend_bluetooth_cancel(bluetooth);
	found = pump(bluetooth, &seen, 1000, until_result, NULL);
	(void)pump(bluetooth, &seen, 300, until_never, NULL);
	check(error == 0 && found && seen.result_error == ECANCELED, "cancel", "error=%d result=%d", error, seen.result_error);
	check(calls_contain("CancelPairing AA:00:00:00:00:04"), "cancel-pairing-sent", "no CancelPairing");

	/* 10. BlueZ gives up a question: it ends, and the pairing fails. */
	(void)fake("script", "confirm-cancel", answer, sizeof(answer));
	seen.ends = 0U;
	(void)request(bluetooth, &seen, KL_BACKEND_BT_PAIR, "AA:00:00:00:00:04", KL_BACKEND_BT_BREDR);
	found = pump(bluetooth, &seen, 3000, until_result, NULL);
	check(found && seen.result_error == ECONNREFUSED && seen.ends > 0U, "bluez-cancel", "result=%d ends=%u", seen.result_error, seen.ends);

	/* 11. Services: an unpaired device refused, HID of a paired untrusted one asked, another service refused, a trusted one allowed. */
	(void)fake("trigger", "what=authorize;address=AA:00:00:00:00:04;uuid=00001124-0000-1000-8000-00805f9b34fb", answer, sizeof(answer));
	found = pump(bluetooth, &seen, 2000, until_agent_result, NULL);
	(void)fake("agent-result", "", answer, sizeof(answer));
	check(strcmp(answer, "org.bluez.Error.Rejected") == 0, "authorize-unpaired", "%s", answer);
	seen.questions = 0U;
	(void)fake("trigger", "what=authorize;address=" KEYBOARD ";uuid=00001124-0000-1000-8000-00805f9b34fb", answer, sizeof(answer));
	found = pump(bluetooth, &seen, 2000, until_question, NULL);
	check(found && seen.question.kind == KL_BACKEND_BT_ASK_CONSENT && seen.question.own == 0U, "authorize-hid-asked", "kind=%u own=%u", seen.question.kind, seen.question.own);
	(void)kl_backend_bluetooth_answer(bluetooth, seen.question.id, 1U);
	found = pump(bluetooth, &seen, 2000, until_agent_result, NULL);
	(void)fake("agent-result", "", answer, sizeof(answer));
	check(strcmp(answer, "ok") == 0, "authorize-hid-yes", "%s", answer);
	(void)fake("trigger", "what=authorize;address=" KEYBOARD ";uuid=0000110b-0000-1000-8000-00805f9b34fb", answer, sizeof(answer));
	found = pump(bluetooth, &seen, 2000, until_agent_result, NULL);
	(void)fake("agent-result", "", answer, sizeof(answer));
	check(strcmp(answer, "org.bluez.Error.Rejected") == 0, "authorize-other-refused", "%s", answer);
	(void)fake("trigger", "what=authorize;address=" MOUSE ";uuid=0000110b-0000-1000-8000-00805f9b34fb", answer, sizeof(answer));
	found = pump(bluetooth, &seen, 2000, until_agent_result, NULL);
	(void)fake("agent-result", "", answer, sizeof(answer));
	check(strcmp(answer, "ok") == 0, "authorize-trusted", "%s", answer);

	/* 12. A code BlueZ asks this agent to type is refused. */
	(void)fake("trigger", "what=passkey-request;address=" MOUSE, answer, sizeof(answer));
	found = pump(bluetooth, &seen, 2000, until_agent_result, NULL);
	(void)fake("agent-result", "", answer, sizeof(answer));
	check(strcmp(answer, "org.bluez.Error.Rejected") == 0, "passkey-request-refused", "%s", answer);

	/* 13. Another program cannot speak as BlueZ to the agent. */
	found = foreign_agent_call(bluetooth, &seen);
	check(found, "foreign-caller-denied", "not AccessDenied");

	/* 14. The power off and on. */
	(void)request(bluetooth, &seen, KL_BACKEND_BT_POWER_OFF, NULL, 0U);
	found = pump(bluetooth, &seen, 2000, until_result, NULL);
	target = KL_BACKEND_BT_OFF;
	(void)pump(bluetooth, &seen, 2000, until_state, &target);
	kl_backend_bluetooth_get_state(bluetooth, &state);
	check(found && seen.result_error == 0 && state.state == KL_BACKEND_BT_OFF && state.power == 0U, "power-off", "result=%d state=%u", seen.result_error, state.state);
	(void)request(bluetooth, &seen, KL_BACKEND_BT_POWER_ON, NULL, 0U);
	found = pump(bluetooth, &seen, 2000, until_result, NULL);
	target = KL_BACKEND_BT_ON;
	(void)pump(bluetooth, &seen, 2000, until_state, &target);
	kl_backend_bluetooth_get_state(bluetooth, &state);
	check(found && state.state == KL_BACKEND_BT_ON, "power-on", "state=%u", state.state);

	/* 15. Connecting what is connected is done; disconnecting it too. */
	(void)fake("connect-error", "org.bluez.Error.AlreadyConnected", answer, sizeof(answer));
	(void)request(bluetooth, &seen, KL_BACKEND_BT_CONNECT, MOUSE, KL_BACKEND_BT_BREDR);
	found = pump(bluetooth, &seen, 2000, until_result, NULL);
	check(found && seen.result_error == 0, "already-connected", "result=%d", seen.result_error);

	/* 15a. Connect's failures are not a pairing refused: no profile, no answer, refused (T1-593). */
	(void)fake("connect-error", "org.bluez.Error.NotAvailable:br-connection-profile-unavailable", answer, sizeof(answer));
	(void)request(bluetooth, &seen, KL_BACKEND_BT_CONNECT, MOUSE, KL_BACKEND_BT_BREDR);
	found = pump(bluetooth, &seen, 2000, until_result, NULL);
	check(found && seen.result_error == ENOTSUP && strcmp(seen.result_reason, "NotAvailable") == 0, "connect-no-profile", "result=%d reason=%s", seen.result_error, seen.result_reason);
	(void)fake("connect-error", "org.bluez.Error.Failed:br-connection-page-timeout", answer, sizeof(answer));
	(void)request(bluetooth, &seen, KL_BACKEND_BT_CONNECT, MOUSE, KL_BACKEND_BT_BREDR);
	found = pump(bluetooth, &seen, 2000, until_result, NULL);
	check(found && seen.result_error == ETIMEDOUT && strcmp(seen.result_reason, "br-connection-page-timeout") == 0, "connect-no-answer", "result=%d reason=%s", seen.result_error, seen.result_reason);
	(void)fake("connect-error", "org.bluez.Error.Failed:br-connection-refused", answer, sizeof(answer));
	(void)request(bluetooth, &seen, KL_BACKEND_BT_CONNECT, MOUSE, KL_BACKEND_BT_BREDR);
	found = pump(bluetooth, &seen, 2000, until_result, NULL);
	check(found && seen.result_error == ECONNREFUSED, "connect-refused", "result=%d reason=%s", seen.result_error, seen.result_reason);
	(void)fake("connect-error", "org.bluez.Error.NotSupported", answer, sizeof(answer));
	(void)request(bluetooth, &seen, KL_BACKEND_BT_CONNECT, MOUSE, KL_BACKEND_BT_BREDR);
	found = pump(bluetooth, &seen, 2000, until_result, NULL);
	check(found && seen.result_error == ENOTSUP, "connect-not-supported", "result=%d reason=%s", seen.result_error, seen.result_reason);

	(void)fake("connect-error", "", answer, sizeof(answer));
	(void)request(bluetooth, &seen, KL_BACKEND_BT_DISCONNECT, MOUSE, KL_BACKEND_BT_BREDR);
	found = pump(bluetooth, &seen, 2000, until_result, NULL);
	(void)pump(bluetooth, &seen, 300, until_never, NULL);
	(void)find_device(bluetooth, MOUSE, &device);
	check(found && seen.result_error == 0 && device.connected == 0U, "disconnect", "result=%d connected=%u", seen.result_error, device.connected);
	(void)request(bluetooth, &seen, KL_BACKEND_BT_DISCONNECT, MOUSE, KL_BACKEND_BT_BREDR);
	found = pump(bluetooth, &seen, 2000, until_result, NULL);
	check(found && seen.result_error == 0, "not-connected-done", "result=%d", seen.result_error);
	error = kl_backend_bluetooth_request(bluetooth, KL_BACKEND_BT_CONNECT, "AB:CD:EF:00:00:00", KL_BACKEND_BT_BREDR, &target);
	check(error == EINVAL, "unknown-address", "error=%d", error);
	error = kl_backend_bluetooth_request(bluetooth, KL_BACKEND_BT_PAIR_PHONE, MOUSE, KL_BACKEND_BT_BREDR, &target);
	check(error == ENOTSUP, "phone-unsupported", "error=%d", error);

	/* 16. Forget: the device goes. */
	(void)request(bluetooth, &seen, KL_BACKEND_BT_FORGET, "AA:00:00:00:00:06", KL_BACKEND_BT_BREDR);
	found = pump(bluetooth, &seen, 2000, until_result, NULL);
	(void)pump(bluetooth, &seen, 300, until_never, NULL);
	check(found && seen.result_error == 0 && !find_device(bluetooth, "AA:00:00:00:00:06", &device), "forget", "result=%d", seen.result_error);

	/* 17. The scan: started for this program, then stopped. */
	kl_backend_bluetooth_set_scanning(bluetooth, 1U);
	(void)pump(bluetooth, &seen, 500, until_never, NULL);
	kl_backend_bluetooth_get_state(bluetooth, &state);
	check(calls_contain("StartDiscovery") && state.scanning == 1U, "scan-start", "scanning=%u", state.scanning);
	kl_backend_bluetooth_set_scanning(bluetooth, 0U);
	(void)pump(bluetooth, &seen, 500, until_never, NULL);
	kl_backend_bluetooth_get_state(bluetooth, &state);
	check(calls_contain("StopDiscovery") && state.scanning == 0U, "scan-stop", "scanning=%u", state.scanning);

	/* 18. Forty more devices: thirty-two shown, the paired among them. */
	for (index = 0; index < 40U; index++) {
		(void)snprintf(argument, sizeof(argument), "address=BB:00:00:00:00:%02zX;name=Crowd %02zu;paired=0;rssi=-%zu;kind=bredr;announce=1", index, index, 30U + index);
		(void)fake("add", argument, answer, sizeof(answer));
	}
	(void)pump(bluetooth, &seen, 3000, until_never, NULL);
	count = kl_backend_bluetooth_get_devices(bluetooth, devices, KL_BACKEND_BT_DEVICES_MAX);
	check(count == KL_BACKEND_BT_DEVICES_MAX && find_device(bluetooth, KEYBOARD, &device) && find_device(bluetooth, MOUSE, &device), "at-most-32", "count=%zu", count);
	check(find_device(bluetooth, "BB:00:00:00:00:00", &device) && !find_device(bluetooth, "BB:00:00:00:00:27", &device), "strongest-kept", "strongest or weakest misplaced");

	/* 19. org.bluez goes during a pairing: the request ends with ENOTCONN, the state is absent; it comes back. */
	(void)fake("script", "hang", answer, sizeof(answer));
	(void)request(bluetooth, &seen, KL_BACKEND_BT_PAIR, "AA:00:00:00:00:04", KL_BACKEND_BT_BREDR);
	(void)pump(bluetooth, &seen, 300, until_never, NULL);
	(void)fake("release-name", "", answer, sizeof(answer));
	found = pump(bluetooth, &seen, 2000, until_result, NULL);
	(void)pump(bluetooth, &seen, 2000, until_absent, NULL);
	kl_backend_bluetooth_get_state(bluetooth, &state);
	count = kl_backend_bluetooth_get_devices(bluetooth, devices, KL_BACKEND_BT_DEVICES_MAX);
	check(found && seen.result_error == ENOTCONN && state.state == KL_BACKEND_BT_ABSENT && state.reachable == 0U && count == 0U, "owner-gone", "result=%d state=%u count=%zu", seen.result_error, state.state, count);
	error = kl_backend_bluetooth_request(bluetooth, KL_BACKEND_BT_POWER_ON, NULL, 0U, &target);
	check(error == ENOTCONN, "absent-request", "error=%d", error);
	(void)fake("acquire-name", "", answer, sizeof(answer));
	found = pump(bluetooth, &seen, 8000, until_ready, NULL);
	kl_backend_bluetooth_get_state(bluetooth, &state);
	check(found && state.agent == 1U && find_device(bluetooth, KEYBOARD, &device), "owner-back", "state=%u agent=%u", state.state, state.agent);

	/* 20. A message over 64 KiB closes the connection: it is opened again after the wait, and BlueZ followed anew. */
	(void)fake("add", "address=DD:00:00:00:00:01;name=@huge;paired=0;rssi=-40;kind=bredr;announce=1", answer, sizeof(answer));
	found = pump(bluetooth, &seen, 3000, until_absent, NULL);
	check(found, "oversize-closes", "still reachable");
	(void)fake("remove", "address=DD:00:00:00:00:01", answer, sizeof(answer));
	found = pump(bluetooth, &seen, 10000, until_ready, NULL);
	kl_backend_bluetooth_get_state(bluetooth, &state);
	check(found && state.agent == 1U, "reopened", "state=%u agent=%u", state.state, state.agent);

	/* 21. BlueZ releases the agent. */
	(void)fake("trigger", "what=release", answer, sizeof(answer));
	(void)pump(bluetooth, &seen, 1000, until_agent_result, NULL);
	(void)pump(bluetooth, &seen, 200, until_never, NULL);
	kl_backend_bluetooth_get_state(bluetooth, &state);
	check(state.agent == 0U, "agent-released", "agent=%u", state.agent);

	/* 22. Closing withdraws the agent. */
	kl_backend_bluetooth_close(bluetooth);
	sleep_ms(300);
	check(calls_contain("UnregisterAgent"), "close-unregisters", "no UnregisterAgent");

	/* The verdict. */
	dbus_close(&control);
	if (failures != 0U) {
		printf("bt-linux-host-test: %u FAILED\n", failures);
		return 1;
	}

	/* Succeeded: every check passed. */
	printf("bt-linux-host-test: all ok\n");
	return 0;
}

/* Prints one check's verdict. */
static void
check(
	int condition,
	const char *name,
	const char *format,
	...)
{
	va_list arguments;

	/* A passed check, by its name. */
	if (condition) {
		printf("ok %s\n", name);
		return;
	}

	/* A failed one, with why. */
	failures++;
	printf("FAIL %s: ", name);
	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
	printf("\n");
}

/* Sends one command to the fake and copies its answer.  Returns 0 or the error. */
static int
fake(
	const char *command,
	const char *argument,
	char *answer,
	size_t size)
{
	struct dbus_arg arguments[2];
	struct dbus_reply *reply;
	const char *text;
	int error;

	/* The command and its argument. */
	memset(arguments, 0, sizeof(arguments));
	arguments[0].string = command;
	arguments[1].string = argument;
	answer[0] = '\0';
	error = dbus_call(&control, FAKE_NAME, FAKE_PATH, FAKE_INTERFACE, "Control", "ss", arguments, 2, &reply);
	if (error != 0) {
		printf("fake %s: error %d\n", command, error);
		return error;
	}

	/* Its answer. */
	error = dbus_read_string(reply, &text);
	if (error == 0)
		(void)snprintf(answer, size, "%s", text);
	dbus_reply_free(reply);

	/* Succeeded or not as the reading went. */
	return error;
}

/* Sleeps a few milliseconds. */
static void
sleep_ms(
	unsigned milliseconds)
{
	struct timespec wait;

	/* The wait. */
	wait.tv_sec = milliseconds / 1000U;
	wait.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
	(void)nanosleep(&wait, NULL);
}

/*
 * Updates the backend every ten milliseconds as the compositor does,
 * taking its results and questions, until the predicate holds or the time
 * is over.  Returns whether the predicate held.
 */
static int
pump(
	struct kl_backend_bluetooth *bluetooth,
	struct seen *seen,
	unsigned milliseconds,
	until_fn until,
	const void *argument)
{
	struct kl_backend_bluetooth_question question;
	unsigned changed;
	unsigned waited;
	int taken;

	/* Each pass. */
	for (waited = 0; waited <= milliseconds; waited += 10U) {
		(void)kl_backend_bluetooth_update(bluetooth, &changed);

		/* The results. */
		for (;;) {
			taken = kl_backend_bluetooth_take_result(bluetooth, &seen->result_id, &seen->result_error, seen->result_reason, sizeof(seen->result_reason));
			if (!taken)
				break;
			seen->results++;
		}

		/* The questions; an end is counted apart. */
		for (;;) {
			taken = kl_backend_bluetooth_take_question(bluetooth, &question);
			if (!taken)
				break;
			if (question.kind == KL_BACKEND_BT_ASK_END) {
				seen->ends++;
				seen->last_end = question.id;
				continue;
			}
			seen->question = question;
			seen->questions++;
		}

		/* Done when the predicate holds. */
		if (until(bluetooth, seen, argument))
			return 1;
		sleep_ms(10U);
	}

	/* The time is over. */
	return 0;
}

/* Never holds: pump() runs its whole time. */
static int
until_never(
	struct kl_backend_bluetooth *bluetooth,
	const struct seen *seen,
	const void *argument)
{
	(void)bluetooth;
	(void)seen;
	(void)argument;
	return 0;
}

/* Holds when the adapter is on, the agent held and at least three devices listed. */
static int
until_ready(
	struct kl_backend_bluetooth *bluetooth,
	const struct seen *seen,
	const void *argument)
{
	struct kl_backend_bluetooth_state state;
	struct kl_backend_bluetooth_device devices[1];
	size_t count;

	(void)seen;
	(void)argument;
	kl_backend_bluetooth_get_state(bluetooth, &state);
	count = kl_backend_bluetooth_get_devices(bluetooth, devices, 0);
	if (state.state != KL_BACKEND_BT_ON || state.agent != 1U)
		return 0;
	return count >= 3U;
}

/* Holds when a result was taken (the count is reset by request()). */
static int
until_result(
	struct kl_backend_bluetooth *bluetooth,
	const struct seen *seen,
	const void *argument)
{
	(void)bluetooth;
	(void)argument;
	return seen->results > 0U;
}

/* Holds when a question was taken (the count is reset by request()). */
static int
until_question(
	struct kl_backend_bluetooth *bluetooth,
	const struct seen *seen,
	const void *argument)
{
	(void)bluetooth;
	(void)argument;
	return seen->questions > 0U;
}

/* Holds when the device of an address is listed. */
static int
until_device(
	struct kl_backend_bluetooth *bluetooth,
	const struct seen *seen,
	const void *argument)
{
	struct kl_backend_bluetooth_device device;

	(void)seen;
	return find_device(bluetooth, argument, &device);
}

/* Holds when BlueZ is unreachable. */
static int
until_absent(
	struct kl_backend_bluetooth *bluetooth,
	const struct seen *seen,
	const void *argument)
{
	struct kl_backend_bluetooth_state state;

	(void)seen;
	(void)argument;
	kl_backend_bluetooth_get_state(bluetooth, &state);
	return state.state == KL_BACKEND_BT_ABSENT;
}

/* Holds when the state is the one pointed to. */
static int
until_state(
	struct kl_backend_bluetooth *bluetooth,
	const struct seen *seen,
	const void *argument)
{
	struct kl_backend_bluetooth_state state;

	(void)seen;
	kl_backend_bluetooth_get_state(bluetooth, &state);
	return state.state == *(const unsigned *)argument;
}

/* Holds when the fake has the answer of the agent call it made. */
static int
until_agent_result(
	struct kl_backend_bluetooth *bluetooth,
	const struct seen *seen,
	const void *argument)
{
	char answer[128];

	(void)bluetooth;
	(void)seen;
	(void)argument;
	(void)fake("agent-result", "", answer, sizeof(answer));
	return strcmp(answer, "pending") != 0;
}

/* Copies the listed device of an address.  Returns whether it is listed. */
static int
find_device(
	struct kl_backend_bluetooth *bluetooth,
	const char *address,
	struct kl_backend_bluetooth_device *device)
{
	struct kl_backend_bluetooth_device devices[KL_BACKEND_BT_DEVICES_MAX];
	size_t count;
	size_t index;

	/* Each listed device. */
	memset(device, 0, sizeof(*device));
	count = kl_backend_bluetooth_get_devices(bluetooth, devices, KL_BACKEND_BT_DEVICES_MAX);
	if (count > KL_BACKEND_BT_DEVICES_MAX)
		count = KL_BACKEND_BT_DEVICES_MAX;
	for (index = 0; index < count; index++) {
		if (strcmp(devices[index].address, address) != 0)
			continue;
		*device = devices[index];
		return 1;
	}

	/* Not listed. */
	return 0;
}

/* Sends a request with the counts of results and questions reset.  Returns the request's error. */
static int
request(
	struct kl_backend_bluetooth *bluetooth,
	struct seen *seen,
	unsigned kind,
	const char *address,
	unsigned type)
{
	uint32_t id;
	int error;

	/* Nothing seen of it yet. */
	seen->results = 0U;
	seen->questions = 0U;
	seen->result_error = -1;
	seen->result_reason[0] = '\0';
	error = kl_backend_bluetooth_request(bluetooth, kind, address, type, &id);
	if (error != 0)
		printf("request %u %s: error %d\n", kind, address != NULL ? address : "-", error);
	return error;
}

/* Says whether the fake's record of calls has a line starting with the text. */
static int
calls_contain(
	const char *text)
{
	char answer[8192];
	const char *line;
	size_t length;

	/* Each line of the record. */
	(void)fake("calls", "", answer, sizeof(answer));
	length = strlen(text);
	line = answer;
	while (line != NULL && *line != '\0') {
		if (strncmp(line, text, length) == 0)
			return 1;
		line = strchr(line, '\n');
		if (line != NULL)
			line++;
	}

	/* Not there. */
	return 0;
}

/* Takes the control connection's messages, keeping the answer to the foreign call. */
static int
foreign_message(
	struct dbus_reply *message,
	void *data)
{
	struct foreign *foreign;

	/* Only the answer to that call. */
	foreign = data;
	if (message->type != LINUX_DBUS_ERROR && message->type != LINUX_DBUS_METHOD_RETURN)
		return 0;
	if (message->reply_serial != foreign->serial)
		return 0;
	foreign->answered = 1;
	if (message->type == LINUX_DBUS_ERROR)
		(void)snprintf(foreign->error, sizeof(foreign->error), "%s", message->error);
	return 0;
}

/*
 * Calls the backend's agent from the control connection, as a program
 * that is not BlueZ, and says whether it was denied.
 */
static int
foreign_agent_call(
	struct kl_backend_bluetooth *bluetooth,
	struct seen *seen)
{
	struct dbus_arg arguments[2];
	struct foreign foreign;
	char owner[256];
	unsigned waited;
	int error;

	/* The backend's connection, as the fake saw it register. */
	(void)fake("agent-owner", "", owner, sizeof(owner));
	memset(arguments, 0, sizeof(arguments));
	arguments[0].string = "/org/bluez/hci0/dev_AA_00_00_00_00_01";
	arguments[1].number = 1U;
	memset(&foreign, 0, sizeof(foreign));
	error = dbus_send_call(&control, 0U, owner, "/org/keiland/bluetooth/agent", "org.bluez.Agent1", "RequestConfirmation", "ou", arguments, 2, &foreign.serial);
	if (error != 0)
		return 0;

	/* The backend runs while the answer is awaited. */
	for (waited = 0; waited < 2000U && !foreign.answered; waited += 10U) {
		(void)pump(bluetooth, seen, 0, until_never, NULL);
		(void)dbus_dispatch_messages(&control, foreign_message, &foreign);
		sleep_ms(10U);
	}

	/* Denied, and no question asked of the user. */
	return foreign.answered && strcmp(foreign.error, "org.freedesktop.DBus.Error.AccessDenied") == 0;
}
