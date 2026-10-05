/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws174-p003: host test of the UEFI loader's boot key detection (K1, K2).
 *
 * Links bootloader/uefi/boot-keys.c against a mock system table whose boot
 * services and extended console input are host functions with the UEFI
 * calling convention, and checks the bits of one key event (K1) and the
 * protocol lookup, the exposed-modifier request and the bounded queue
 * drain (K2).  Built and run by run-boot-keys-host-test.sh.
 */

#include "bootloader/uefi/boot-keys.h"
#include "bootloader/common/boot-override.h"

#include <stdio.h>
#include <string.h>

/* The most events one scripted queue holds. */
#define MOCK_EVENT_MAX	8U

/*
 * What the mock firmware answers, and what it saw.
 *
 * One test sets the answers, runs the loader's functions, and reads the
 * counters; mock_reset() empties it before the next test.
 */
struct mock_firmware {
	EFI_STATUS handle_status;
	EFI_STATUS locate_status;
	EFI_STATUS set_state_status;
	UINT32 events[MOCK_EVENT_MAX];
	unsigned event_count;
	unsigned next_event;
	int endless;
	UINT32 endless_state;
	unsigned handle_calls;
	unsigned locate_calls;
	unsigned set_state_calls;
	unsigned reads;
	uint8_t toggle;
};

/*
 * The mock firmware's script and counters.
 *
 * File-scope because the UEFI callbacks receive no test context; every test
 * starts with mock_reset().
 */
static struct mock_firmware mock;

/* The mock extended console input the boot services hand out. */
static EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL mock_input;

/* The mock boot services and system table the loader's functions see. */
static EFI_BOOT_SERVICES mock_boot;
static EFI_SYSTEM_TABLE mock_system;

/*
 * The checks run and the checks failed.
 *
 * Every check counts itself; the tally is printed at the end and decides the
 * exit status.
 */
static unsigned checks;
static unsigned failures;

static void check(int ok, const char *what);
static void mock_reset(void);
static EFI_STATUS EFIAPI mock_handle_protocol(EFI_HANDLE handle, const EFI_GUID *guid, void **interface);
static EFI_STATUS EFIAPI mock_locate_protocol(const EFI_GUID *guid, void *registration, void **interface);
static EFI_STATUS EFIAPI mock_set_state(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *input, uint8_t *toggle);
static EFI_STATUS EFIAPI mock_read_key(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *input, EFI_KEY_DATA *data);
static unsigned state_bits(UINT32 shift_state);
static void test_from_state(void);
static void test_lookup(void);
static void test_drain(void);

/*
 * Runs the checks and reports the tally.
 */
int
main(void)
{
	/* Runs the event classification, the lookup, and the queue drain. */
	test_from_state();
	test_lookup();
	test_drain();

	printf("boot-keys-host-test: %u checks, %u failures\n", checks, failures);

	/* Reports a failed check. */
	if (failures != 0U)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/* Counts one check and names it when it failed. */
static void
check(
	int ok,
	const char *what)
{
	/* Every check counts. */
	checks++;

	/* A failed check is named. */
	if (!ok) {
		failures++;
		printf("FAIL: %s\n", what);
	}
}

/* Empties the script and the counters and wires the mock tables. */
static void
mock_reset(void)
{
	/* A firmware that has the extended input and answers every call. */
	memset(&mock, 0, sizeof(mock));
	mock.handle_status = EFI_SUCCESS;
	mock.locate_status = EFI_SUCCESS;
	mock.set_state_status = EFI_SUCCESS;

	/* The extended input. */
	memset(&mock_input, 0, sizeof(mock_input));
	mock_input.ReadKeyStrokeEx = mock_read_key;
	mock_input.SetState = mock_set_state;

	/* The boot services and the system table with a console input handle. */
	memset(&mock_boot, 0, sizeof(mock_boot));
	mock_boot.HandleProtocol = mock_handle_protocol;
	mock_boot.LocateProtocol = mock_locate_protocol;
	memset(&mock_system, 0, sizeof(mock_system));
	mock_system.BootServices = &mock_boot;
	mock_system.ConsoleInHandle = &mock_system;
}

/* Answers HandleProtocol on the console input handle. */
static EFI_STATUS EFIAPI
mock_handle_protocol(
	EFI_HANDLE handle,
	const EFI_GUID *guid,
	void **interface)
{
	int different;

	/* Counts the call. */
	mock.handle_calls++;

	/* Only the extended input on the console handle is offered. */
	different = memcmp(guid, &EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID, sizeof(*guid));
	if (handle != mock_system.ConsoleInHandle || different != 0)
		return EFI_UNSUPPORTED;

	/* Refuses when the script says so. */
	if (mock.handle_status != EFI_SUCCESS)
		return mock.handle_status;

	/* Succeeded: hands out the mock input. */
	*interface = &mock_input;
	return EFI_SUCCESS;
}

/* Answers LocateProtocol for the extended input. */
static EFI_STATUS EFIAPI
mock_locate_protocol(
	const EFI_GUID *guid,
	void *registration,
	void **interface)
{
	int different;

	/* Counts the call. */
	mock.locate_calls++;

	/* Only the extended input is offered, with no registration. */
	different = memcmp(guid, &EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID, sizeof(*guid));
	if (registration != NULL || different != 0)
		return EFI_UNSUPPORTED;

	/* Refuses when the script says so. */
	if (mock.locate_status != EFI_SUCCESS)
		return mock.locate_status;

	/* Succeeded: hands out the mock input. */
	*interface = &mock_input;
	return EFI_SUCCESS;
}

/* Records the toggle state the loader asks for. */
static EFI_STATUS EFIAPI
mock_set_state(
	EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *input,
	uint8_t *toggle)
{
	/* Counts the call and keeps what was asked. */
	mock.set_state_calls++;
	mock.toggle = *toggle;

	/* Refuses a call on another protocol instance. */
	if (input != &mock_input)
		return EFI_INVALID_PARAMETER;

	/* Answers as the script says. */
	return mock.set_state_status;
}

/* Hands out the scripted key events, then reports an empty queue. */
static EFI_STATUS EFIAPI
mock_read_key(
	EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *input,
	EFI_KEY_DATA *data)
{
	/* Counts the call. */
	mock.reads++;

	/* Refuses a call on another protocol instance. */
	if (input != &mock_input)
		return EFI_INVALID_PARAMETER;

	/* A firmware that always answers with the same empty event. */
	if (mock.endless) {
		data->KeyState.KeyShiftState = mock.endless_state;
		return EFI_SUCCESS;
	}

	/* The queue is empty after the scripted events. */
	if (mock.next_event >= mock.event_count)
		return EFI_NOT_READY;

	/* Succeeded: the next scripted event, with Space as its key. */
	data->Key.UnicodeChar = ' ';
	data->KeyState.KeyShiftState = mock.events[mock.next_event];
	mock.next_event++;
	return EFI_SUCCESS;
}

/* Reports the boot key bits of one event with the given modifier state. */
static unsigned
state_bits(
	UINT32 shift_state)
{
	EFI_KEY_DATA data;
	unsigned bits;

	/* An event of Space with the modifier state. */
	memset(&data, 0, sizeof(data));
	data.Key.UnicodeChar = ' ';
	data.KeyState.KeyShiftState = shift_state;

	/* Classifies it. */
	bits = zbl_uefi_boot_keys_from_state(&data);

	/* Reports the bits. */
	return bits;
}

/* K1: the bits of one key event. */
static void
test_from_state(void)
{
	unsigned bits;

	/* Either Ctrl selects the kernel messages. */
	bits = state_bits(EFI_SHIFT_STATE_VALID | EFI_LEFT_CONTROL_PRESSED);
	check(bits == ZBL_BOOT_OVERRIDE_KMSG, "K1 left Ctrl");
	bits = state_bits(EFI_SHIFT_STATE_VALID | EFI_RIGHT_CONTROL_PRESSED);
	check(bits == ZBL_BOOT_OVERRIDE_KMSG, "K1 right Ctrl");

	/* Either Shift selects the console login. */
	bits = state_bits(EFI_SHIFT_STATE_VALID | EFI_LEFT_SHIFT_PRESSED);
	check(bits == ZBL_BOOT_OVERRIDE_LOGIN, "K1 left Shift");
	bits = state_bits(EFI_SHIFT_STATE_VALID | EFI_RIGHT_SHIFT_PRESSED);
	check(bits == ZBL_BOOT_OVERRIDE_LOGIN, "K1 right Shift");

	/* Both at once. */
	bits = state_bits(EFI_SHIFT_STATE_VALID | EFI_LEFT_CONTROL_PRESSED | EFI_RIGHT_SHIFT_PRESSED);
	check(bits == (ZBL_BOOT_OVERRIDE_KMSG | ZBL_BOOT_OVERRIDE_LOGIN), "K1 Ctrl and Shift");

	/* No modifier, a state not marked valid, and nothing at all. */
	bits = state_bits(EFI_SHIFT_STATE_VALID);
	check(bits == 0U, "K1 valid without modifiers");
	bits = state_bits(EFI_LEFT_CONTROL_PRESSED);
	check(bits == 0U, "K1 Ctrl without the valid bit");
	bits = state_bits(0U);
	check(bits == 0U, "K1 empty state");
}

/* K2: finding the extended input and asking for exposed modifier keys. */
static void
test_lookup(void)
{
	struct zbl_uefi_boot_keys keys;
	unsigned held;

	/* No console input handle and no instance: no keys, and no reads. */
	mock_reset();
	mock_system.ConsoleInHandle = 0;
	mock.locate_status = EFI_NOT_FOUND;
	zbl_uefi_boot_keys_open(&keys, &mock_system);
	check(keys.input == 0, "K2 no protocol leaves no input");
	check(mock.handle_calls == 0U, "K2 no handle is not asked");
	held = zbl_uefi_boot_keys_sample(&keys);
	check(held == 0U, "K2 no protocol samples nothing");
	check(mock.reads == 0U, "K2 no protocol reads nothing");

	/* The console input handle answers: exposed modifiers are asked for. */
	mock_reset();
	zbl_uefi_boot_keys_open(&keys, &mock_system);
	check(keys.input == &mock_input, "K2 console handle gives the input");
	check(mock.locate_calls == 0U, "K2 console handle needs no locate");
	check(mock.toggle == (EFI_TOGGLE_STATE_VALID | EFI_KEY_STATE_EXPOSED), "K2 asks for exposed modifiers");
	check(keys.exposed == 1, "K2 exposed accepted");
	check(keys.held == 0U, "K2 starts with no key");

	/* The console handle refuses: the instance the firmware locates is used. */
	mock_reset();
	mock.handle_status = EFI_UNSUPPORTED;
	zbl_uefi_boot_keys_open(&keys, &mock_system);
	check(keys.input == &mock_input, "K2 locate gives the input");
	check(mock.locate_calls == 1U, "K2 locate asked once");

	/* SetState refused: not exposed, yet a modifier with Space is still read. */
	mock_reset();
	mock.set_state_status = EFI_UNSUPPORTED;
	mock.events[0] = EFI_SHIFT_STATE_VALID | EFI_LEFT_CONTROL_PRESSED;
	mock.event_count = 1U;
	zbl_uefi_boot_keys_open(&keys, &mock_system);
	check(keys.input == &mock_input, "K2 SetState refused keeps the input");
	check(keys.exposed == 0, "K2 SetState refused is not exposed");
	held = zbl_uefi_boot_keys_sample(&keys);
	check(held == ZBL_BOOT_OVERRIDE_KMSG, "K2 SetState refused still reads Ctrl+Space");
}

/* K2: draining the queue, gathering across samples, and the bound. */
static void
test_drain(void)
{
	struct zbl_uefi_boot_keys keys;
	unsigned held;

	/* Ctrl on the third event is found, and the queue is read dry. */
	mock_reset();
	mock.events[0] = EFI_SHIFT_STATE_VALID;
	mock.events[1] = EFI_SHIFT_STATE_VALID;
	mock.events[2] = EFI_SHIFT_STATE_VALID | EFI_RIGHT_CONTROL_PRESSED;
	mock.event_count = 3U;
	zbl_uefi_boot_keys_open(&keys, &mock_system);
	held = zbl_uefi_boot_keys_sample(&keys);
	check(held == ZBL_BOOT_OVERRIDE_KMSG, "K2 Ctrl on the third event");
	check(mock.reads == 4U, "K2 reads until the queue is empty");

	/* Ctrl in the first sample and Shift in the second are both kept. */
	mock_reset();
	mock.events[0] = EFI_SHIFT_STATE_VALID | EFI_LEFT_CONTROL_PRESSED;
	mock.event_count = 1U;
	zbl_uefi_boot_keys_open(&keys, &mock_system);
	held = zbl_uefi_boot_keys_sample(&keys);
	check(held == ZBL_BOOT_OVERRIDE_KMSG, "K2 first sample Ctrl");
	mock.events[1] = EFI_SHIFT_STATE_VALID | EFI_LEFT_SHIFT_PRESSED;
	mock.event_count = 2U;
	held = zbl_uefi_boot_keys_sample(&keys);
	check(held == (ZBL_BOOT_OVERRIDE_KMSG | ZBL_BOOT_OVERRIDE_LOGIN), "K2 second sample keeps Ctrl and adds Shift");

	/* A firmware that always answers with an empty event: the bound ends it. */
	mock_reset();
	mock.endless = 1;
	mock.endless_state = EFI_SHIFT_STATE_VALID;
	zbl_uefi_boot_keys_open(&keys, &mock_system);
	held = zbl_uefi_boot_keys_sample(&keys);
	check(held == 0U, "K2 endless empty events give no key");
	check(mock.reads == 32U, "K2 endless empty events stop after 32 reads");

	/* The same with Ctrl held: the bound ends it and Ctrl is seen. */
	mock_reset();
	mock.endless = 1;
	mock.endless_state = EFI_SHIFT_STATE_VALID | EFI_LEFT_CONTROL_PRESSED;
	zbl_uefi_boot_keys_open(&keys, &mock_system);
	held = zbl_uefi_boot_keys_sample(&keys);
	check(held == ZBL_BOOT_OVERRIDE_KMSG, "K2 endless Ctrl events give Ctrl");
	check(mock.reads == 32U, "K2 endless Ctrl events stop after 32 reads");
}
