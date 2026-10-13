/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of bluetoothd's Broadcom start on a UART (ws215-p005):
 * userland/base/bluetoothd/broadcom.c, session.c and hci.c built with the
 * host's compiler.
 *
 *   broadcom  the manufacturers, the chip's file (known, unknown, unprintable,
 *             too long), the .hcd checks (whole, empty, cut, not vendor,
 *             Launch RAM early or missing), the speed's parameters, the
 *             address's choice
 *   retry     failed starts in a row, the resets soon after a ready start
 *             (the third stops the tries), a recovery, a steady controller
 *   session   a scripted controller on a UART: the kernel's reset and its
 *             notice (packets before and after it dropped), the speed's
 *             changes in their order with the line's ends, the minidriver's
 *             trailing bytes, every record compared with the file, Launch
 *             RAM answered or not, the address written or kept, the fast
 *             line failing (tried again at 115200), a refused record, a
 *             reset during the load, a missing file, an unknown chip, a
 *             broken file, another manufacturer, a failed reset, no notice
 *   real      with the brcmbt-firmware package's verified cache (the
 *             second argument): BCM4345C0.hcd checked and loaded whole
 *
 *   plan/ws215/tests/bt-broadcom-host-test.sh
 */

#include "userland/base/bluetoothd/broadcom.h"
#include "userland/base/bluetoothd/hci.h"
#include "userland/base/bluetoothd/session.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

/* The speed of the ROM, the board's line, and a speed the scripted controller cannot keep up with. */
#define TEST_BOOT_BAUD		115200U
#define TEST_FAST_BAUD		3000000U

/* The most commands and calls of the node one test records. */
#define TEST_OPCODES		1024U
#define TEST_CALLS		64U

/* The scripted controller's LMP subversions in its ROM and patched. */
#define TEST_ROM_SUBVERSION	0x2209U
#define TEST_PATCH_SUBVERSION	0x6119U

/* The most bytes of a test's patch file. */
#define TEST_FILE_MAX		(128U * 1024U)

/*
 * The scripted Broadcom controller of one session test, on the far end of
 * a socket pair, with the kernel's ioctls standing in beside it: its ROM
 * or patched state, both ends of the line, the faults the test asks for,
 * and what it saw.  The controller's thread and the session's (in the
 * ioctls) share it under its lock; the test reads it after the thread
 * ended.
 */
struct fake {
	int descriptor;
	pthread_mutex_t lock;

	/* The line: the controller's end and the host's (BT_IOC_SET_SPEED). */
	uint32_t controller_speed;
	uint32_t host_speed;

	/* The controller: its manufacturer, its name, its addresses, whether the minidriver runs, whether it is patched. */
	uint16_t manufacturer;
	const char *name;
	size_t name_length;
	uint8_t rom_address[6];
	uint8_t address[6];
	int minidriver;
	int patched;

	/* The node: its information, and whether a reset fails or sends no notice, and sends a stale packet first. */
	struct bt_info info;
	int reset_fails;
	int no_notice;
	int stale_packets;

	/*
	 * The faults: Update UART Baud Rate unanswered; Set UART Clock
	 * refused (the clock asked for is kept); nothing answered
	 * above the ROM's speed; two bytes after the minidriver's answer that
	 * the host must drop (BT_IOC_SET_SPEED) before its next command, or
	 * the kernel resets the line; Launch RAM unanswered; a record refused;
	 * a reset (the kernel's own) at a record; each counted down so the
	 * test can ask for it once.
	 */
	int silent_baud;
	int refuse_clock;
	uint8_t clock;
	int fast_broken;
	int trailing;
	int trailing_pending;
	int silent_launch;
	unsigned refuse_record;
	uint8_t refuse_status;
	unsigned reset_at_record;
	int reset_once;

	/* The patch file the records are compared with, and how far the comparison got. */
	const uint8_t *file;
	size_t file_length;
	size_t file_offset;

	/*
	 * What the controller saw: the commands, the records (in all, and in
	 * this load), the mismatches, the resets (the program's and the
	 * kernel's own), the ioctls and their values.
	 */
	uint16_t opcodes[TEST_OPCODES];
	unsigned opcode_count;
	unsigned records;
	unsigned load_records;
	unsigned record_mismatch;
	unsigned resets;
	unsigned long calls[TEST_CALLS];
	uint32_t call_values[TEST_CALLS];
	unsigned call_count;
};

static void expect(int condition, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void test_broadcom(void);
static void test_hcd(void);
static void test_retry(void);
static void test_session_load(void);
static void test_session_lines(void);
static void test_session_failures(void);
static void test_session_others(void);
static void test_real(const char *folder);
static size_t make_hcd(uint8_t *file, size_t size, unsigned records, int launch);
static void write_file(const char *name, const uint8_t *bytes, size_t length);
static void fake_init(struct fake *fake);
static void fake_start(struct fake *fake, struct btd_session *session, pthread_t *thread);
static void fake_stop(struct fake *fake, struct btd_session *session, pthread_t thread);
static void *fake_run(void *argument);
static void fake_answer(struct fake *fake, uint16_t opcode, const uint8_t *packet, size_t length);
static void fake_record(struct fake *fake, uint16_t opcode, const uint8_t *packet, size_t length);
static void fake_send(struct fake *fake, const uint8_t *packet, size_t length);
static void fake_complete(struct fake *fake, uint16_t opcode, const uint8_t *returned, size_t count);
static void fake_rom(struct fake *fake);
static int fake_control(void *context, unsigned long request, void *argument);
static unsigned fake_count(const struct fake *fake, uint16_t opcode);
static unsigned fake_calls(const struct fake *fake, unsigned long request);
static int fake_order(const struct fake *fake, const uint16_t *opcodes, unsigned count);

/* The checks that failed, and those that ran. */
static unsigned failures;
static unsigned checks;

/* The folder of the test's patch files (made by the script), and the package's verified cache (NULL: none). */
static const char *patch_folder;
static const char *real_folder;

/* The board's address the kernel gives, in the HCI's order. */
static const uint8_t board_address[6] = { 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U };

/* The ROM's default address of the BCM4345C0, in the HCI's order (broadcom.c's table). */
static const uint8_t default_address[6] = { 0xacU, 0x1fU, 0x00U, 0xc0U, 0x45U, 0x43U };

/* The synthetic patch file of the session tests: five Write RAM records and Launch RAM. */
static uint8_t synthetic[TEST_FILE_MAX];
static size_t synthetic_length;

/*
 * Runs every part; the exit status says whether every check held.
 */
int
main(
	int argc,
	char **argv)
{
	/* The patch folder the script made, and the package's cache when given. */
	patch_folder = "build/tmp";
	real_folder = NULL;
	if (argc > 1)
		patch_folder = argv[1];
	if (argc > 2)
		real_folder = argv[2];

	/* The synthetic patch, in the folder under the chip's name. */
	synthetic_length = make_hcd(synthetic, sizeof(synthetic), 5U, 1);
	write_file("BCM4345C0.hcd", synthetic, synthetic_length);

	/* The parts. */
	test_broadcom();
	test_hcd();
	test_retry();
	test_session_load();
	test_session_lines();
	test_session_failures();
	test_session_others();

	/* The real file, when the package's cache is at hand. */
	if (real_folder != NULL) {
		test_real(real_folder);
	} else {
		printf("bt-broadcom-host-test: the real patch file skipped (no cache given)\n");
	}

	/* The verdict. */
	if (failures != 0U) {
		printf("bt-broadcom-host-test: FAIL (%u of %u checks)\n", failures, checks);
		return 1;
	}

	/* Succeeded: every check held. */
	printf("bt-broadcom-host-test: PASS (%u checks)\n", checks);
	return 0;
}

/* Counts a check, and prints it when it failed. */
static void
expect(
	int condition,
	const char *format,
	...)
{
	va_list arguments;

	/* One more check. */
	checks++;
	if (condition)
		return;

	/* A failure, named. */
	failures++;
	printf("FAIL: ");
	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
	printf("\n");
}

/* The manufacturers, the chip's file, the speed's parameters and the address's choice. */
static void
test_broadcom(void)
{
	static const uint8_t own[6] = { 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U };
	static const uint8_t zero[6] = { 0U, 0U, 0U, 0U, 0U, 0U };
	uint8_t name[248];
	uint8_t parameters[BTD_BROADCOM_BAUD_BYTES];
	char path[64];
	char chip[BTD_BROADCOM_CHIP_MAX];
	int error;

	/* Broadcom's and Cypress's controllers, not Intel's. */
	expect(btd_broadcom_manufacturer(15U) == 1, "broadcom: manufacturer 15");
	expect(btd_broadcom_manufacturer(305U) == 1, "broadcom: manufacturer 305 (Cypress)");
	expect(btd_broadcom_manufacturer(2U) == 0, "broadcom: manufacturer 2 (Intel) is not");

	/* The known chip, its name padded with NULs as Read Local Name answers it. */
	memset(name, 0, sizeof(name));
	memcpy(name, "BCM4345C0", 9U);
	error = btd_broadcom_file_name(name, sizeof(name), "/lib/firmware/brcm", path, sizeof(path), chip, sizeof(chip));
	expect(error == 0 && strcmp(path, "/lib/firmware/brcm/BCM4345C0.hcd") == 0 && strcmp(chip, "BCM4345C0") == 0,
	       "broadcom: BCM4345C0's file (%d %s %s)", error, path, chip);

	/* A longer name that begins with a known one is another chip. */
	memcpy(name, "BCM4345C0X", 10U);
	error = btd_broadcom_file_name(name, sizeof(name), "/lib/firmware/brcm", path, sizeof(path), chip, sizeof(chip));
	expect(error == ENOENT && strcmp(chip, "BCM4345C0X") == 0, "broadcom: a longer name is unknown (%d %s)", error, chip);

	/* A name with bytes that cannot be printed shows them as '?' and is never a path. */
	memset(name, 0, sizeof(name));
	memcpy(name, "BCM/../\x01\xff", 9U);
	error = btd_broadcom_file_name(name, sizeof(name), "/lib/firmware/brcm", path, sizeof(path), chip, sizeof(chip));
	expect(error == ENOENT && strcmp(chip, "BCM/../??") == 0, "broadcom: an unprintable name (%d %s)", error, chip);

	/* A name as long as the field, with no NUL, is cut to the chip's room. */
	memset(name, 'A', sizeof(name));
	error = btd_broadcom_file_name(name, sizeof(name), "/x", path, sizeof(path), chip, sizeof(chip));
	expect(error == ENOENT && strlen(chip) == BTD_BROADCOM_CHIP_MAX - 1U, "broadcom: a full name is cut (%d %zu)", error, strlen(chip));

	/* A folder too long for the path. */
	memset(name, 0, sizeof(name));
	memcpy(name, "BCM4345C0", 9U);
	error = btd_broadcom_file_name(name, sizeof(name), "/a/folder/far/too/long/for/the/path's/room/of/sixty/four", path, sizeof(path), chip, sizeof(chip));
	expect(error == ENAMETOOLONG, "broadcom: a path too long (%d)", error);

	/* 3,000,000 baud: two zero bytes, then 0x002DC6C0 least significant first. */
	btd_broadcom_baud(3000000U, parameters);
	expect(memcmp(parameters, "\x00\x00\xc0\xc6\x2d\x00", 6U) == 0, "broadcom: the speed's parameters");

	/* The address: the controller's own stays, a default one is replaced by the board's, or kept without one. */
	expect(btd_broadcom_address_choice(own, board_address, 1) == BTD_BROADCOM_ADDRESS_KEEP, "broadcom: an own address stays");
	expect(btd_broadcom_address_choice(default_address, board_address, 1) == BTD_BROADCOM_ADDRESS_WRITE, "broadcom: the default address is replaced");
	expect(btd_broadcom_address_choice(zero, board_address, 1) == BTD_BROADCOM_ADDRESS_WRITE, "broadcom: no address is replaced");
	expect(btd_broadcom_address_choice(default_address, board_address, 0) == BTD_BROADCOM_ADDRESS_DEFAULT, "broadcom: no board's address keeps the default");
	expect(btd_broadcom_address_choice(board_address, board_address, 1) == BTD_BROADCOM_ADDRESS_KEEP, "broadcom: the board's already");
}

/* The .hcd checks and records. */
static void
test_hcd(void)
{
	static uint8_t file[4096];
	struct btd_broadcom_record record;
	const char *reason;
	size_t records;
	size_t length;
	size_t offset;
	int more;
	int error;

	/* A whole file: three records, the last Launch RAM. */
	length = make_hcd(file, sizeof(file), 2U, 1);
	error = btd_broadcom_hcd_check(file, length, &records, &reason);
	expect(error == 0 && records == 3U, "hcd: a whole file (%d %zu)", error, records);

	/* Its records in turn. */
	offset = 0U;
	more = btd_broadcom_hcd_next(file, length, &offset, &record);
	expect(more == 1 && record.opcode == BTD_BROADCOM_WRITE_RAM && record.length == 10U && record.parameters == file + 3,
	       "hcd: the first record");
	more = btd_broadcom_hcd_next(file, length, &offset, &record);
	more = btd_broadcom_hcd_next(file, length, &offset, &record);
	expect(more == 1 && record.opcode == BTD_BROADCOM_LAUNCH_RAM && record.length == 4U, "hcd: Launch RAM last");
	more = btd_broadcom_hcd_next(file, length, &offset, &record);
	expect(more == 0 && offset == length, "hcd: the end");

	/* Nothing. */
	error = btd_broadcom_hcd_check(file, 0U, &records, &reason);
	expect(error == EBADMSG && strcmp(reason, "an empty file") == 0, "hcd: an empty file");

	/* Cut in a header, and in the parameters. */
	error = btd_broadcom_hcd_check(file, 2U, &records, &reason);
	expect(error == EBADMSG && strcmp(reason, "a record cut short") == 0, "hcd: a cut header");
	error = btd_broadcom_hcd_check(file, length - 1U, &records, &reason);
	expect(error == EBADMSG && strcmp(reason, "a record cut short") == 0, "hcd: cut parameters");

	/* A record that is not a vendor command (HCI_Reset). */
	file[0] = 0x03U;
	file[1] = 0x0cU;
	error = btd_broadcom_hcd_check(file, length, &records, &reason);
	expect(error == EBADMSG && strcmp(reason, "a record that is not a vendor command") == 0, "hcd: not a vendor command");

	/* Launch RAM before the end. */
	file[0] = 0x4eU;
	file[1] = 0xfcU;
	error = btd_broadcom_hcd_check(file, length, &records, &reason);
	expect(error == EBADMSG && strcmp(reason, "launch-ram before the end") == 0, "hcd: Launch RAM early");

	/* No Launch RAM at the end. */
	length = make_hcd(file, sizeof(file), 3U, 0);
	error = btd_broadcom_hcd_check(file, length, &records, &reason);
	expect(error == EBADMSG && strcmp(reason, "no launch-ram at the end") == 0, "hcd: no Launch RAM");
}

/* The count of failed starts, and the resets soon after a ready start. */
static void
test_retry(void)
{
	struct btd_retry retry;
	int stopped;

	/* Three failed starts in a row stop the tries; a recovery clears them. */
	btd_retry_clear(&retry);
	expect(btd_retry_failed(&retry) == 0 && btd_retry_failed(&retry) == 0, "retry: two failures go on");
	stopped = btd_retry_failed(&retry);
	expect(stopped == 1 && retry.stopped, "retry: the third stops");
	btd_retry_clear(&retry);
	expect(!retry.stopped && retry.failures == 0U, "retry: a recovery clears");

	/* A controller ready again and again, each time reset a second later: the third reset stops the tries. */
	btd_retry_ready(&retry, 1000U);
	expect(btd_retry_reset(&retry, 2000U) == 0, "retry: the first reset goes on");
	btd_retry_ready(&retry, 4500U);
	expect(btd_retry_reset(&retry, 5500U) == 0 && retry.failures == 2U, "retry: the second reset goes on");
	btd_retry_ready(&retry, 8000U);
	stopped = btd_retry_reset(&retry, 9000U);
	expect(stopped == 1 && retry.stopped, "retry: the third reset stops");

	/* The recovery's reopen starts again from nothing. */
	btd_retry_clear(&retry);
	btd_retry_ready(&retry, 10000U);
	expect(!retry.stopped && btd_retry_reset(&retry, 11000U) == 0, "retry: after the recovery");

	/* A reset of a controller that stayed up 60 s is not one of a run. */
	btd_retry_clear(&retry);
	(void)btd_retry_failed(&retry);
	(void)btd_retry_failed(&retry);
	btd_retry_ready(&retry, 1000U);
	expect(btd_retry_reset(&retry, 1000U + BTD_RETRY_STEADY_MS) == 0 && retry.failures == 0U, "retry: a steady controller's reset");

	/* The failures before a ready start are kept for 60 s, then forgotten. */
	btd_retry_clear(&retry);
	(void)btd_retry_failed(&retry);
	(void)btd_retry_failed(&retry);
	btd_retry_ready(&retry, 1000U);
	btd_retry_tick(&retry, 30000U);
	expect(retry.failures == 2U, "retry: kept within 60 s");
	btd_retry_tick(&retry, 1000U + BTD_RETRY_STEADY_MS);
	expect(retry.failures == 0U, "retry: forgotten after 60 s");

	/* A reset during a start is not counted twice (the start's failure counts it). */
	btd_retry_clear(&retry);
	expect(btd_retry_reset(&retry, 5000U) == 0 && retry.failures == 0U, "retry: a reset with no ready controller");
}

/* The load at the fast speed: the order of the commands and of the line's changes, the records, the address. */
static void
test_session_load(void)
{
	static const uint16_t order[] = {
		0x0c03U, 0x1001U, 0x0c14U, 0xfc45U, 0xfc18U, 0xfc2eU, 0xfc4cU, 0xfc4cU, 0xfc4cU, 0xfc4cU, 0xfc4cU,
		0xfc4eU, 0x0c03U, 0xfc45U, 0xfc18U, 0x1001U, 0x1009U, 0xfc01U, 0x0c03U
	};
	struct btd_session session;
	struct fake fake;
	pthread_t thread;
	int error;

	/* The Pi's controller, its patch at hand, the board's address given; two stale packets before the reset's notice. */
	fake_init(&fake);
	fake.trailing = 1;
	fake.stale_packets = 1;
	fake_start(&fake, &session, &thread);
	error = btd_session_start(&session);
	expect(error == 0 && session.state == BTD_STATE_READY, "load: ready (%d %s)", error, session.reason);
	expect(session.broadcom && session.firmware_loaded && strcmp(session.chip, "BCM4345C0") == 0, "load: patched BCM4345C0");
	expect(session.rom_subversion == TEST_ROM_SUBVERSION && session.lmp_subversion == TEST_PATCH_SUBVERSION,
	       "load: the subversions (0x%04x 0x%04x)", session.rom_subversion, session.lmp_subversion);
	expect(session.line_speed == TEST_FAST_BAUD && !session.slow && !session.launch_unanswered, "load: the fast line (%u)", session.line_speed);

	/* The commands in their order, the core's set-up after them. */
	expect(fake_order(&fake, order, sizeof(order) / sizeof(order[0])), "load: the commands' order");
	expect(fake.records == 5U && fake.record_mismatch == 0U && fake.file_offset == synthetic_length - 7U,
	       "load: every record as the file has it (%u %u)", fake.records, fake.record_mismatch);

	/* The kernel's calls: the reset, the line fast, afresh after the minidriver, back for the restart, fast again. */
	expect(fake.call_count >= 6U &&
	       fake.calls[0] == BT_IOC_GET_INFO &&
	       fake.calls[1] == BT_IOC_RESET &&
	       fake.calls[2] == BT_IOC_SET_SPEED && fake.call_values[2] == TEST_FAST_BAUD &&
	       fake.calls[3] == BT_IOC_SET_SPEED && fake.call_values[3] == TEST_FAST_BAUD &&
	       fake.calls[4] == BT_IOC_SET_SPEED && fake.call_values[4] == TEST_BOOT_BAUD &&
	       fake.calls[5] == BT_IOC_SET_SPEED && fake.call_values[5] == TEST_FAST_BAUD,
	       "load: the kernel's calls in their order");
	expect(fake_calls(&fake, BT_IOC_RESET) == 1U && fake.resets == 1U, "load: one reset");
	expect(fake.clock == BTD_BROADCOM_CLOCK_48, "load: the 48 MHz clock for 3,000,000 baud");

	/* The board's address replaced the ROM's default one, and the core read it back. */
	expect(session.address_choice == BTD_BROADCOM_ADDRESS_WRITE && session.address_written, "load: the board's address written");
	expect(memcmp(session.address, board_address, 6U) == 0 && memcmp(fake.address, board_address, 6U) == 0, "load: the address read back");
	expect(memcmp(session.rom_address, default_address, 6U) == 0, "load: the default address kept for the log");
	fake_stop(&fake, &session, thread);
}

/* The lines without a speed, the fast line failing, Launch RAM unanswered, the addresses kept. */
static void
test_session_lines(void)
{
	struct btd_session session;
	struct fake fake;
	pthread_t thread;
	int error;

	/* A transport that cannot change its speed: everything at 115200, no Update UART Baud Rate. */
	fake_init(&fake);
	fake.info.flags = BT_INFO_ADDRESS;
	fake.info.speed_max = 0U;
	fake_start(&fake, &session, &thread);
	error = btd_session_start(&session);
	expect(error == 0 && session.state == BTD_STATE_READY && session.firmware_loaded, "line: no speed, ready (%d %s)", error, session.reason);
	expect(fake_count(&fake, 0xfc18U) == 0U && fake_calls(&fake, BT_IOC_SET_SPEED) == 0U && session.line_speed == TEST_BOOT_BAUD,
	       "line: no speed, never changed");
	fake_stop(&fake, &session, thread);

	/* Update UART Baud Rate unanswered: the whole start again at 115200. */
	fake_init(&fake);
	fake.silent_baud = 1;
	fake_start(&fake, &session, &thread);
	error = btd_session_start(&session);
	expect(error == 0 && session.state == BTD_STATE_READY && session.slow && session.line_speed == TEST_BOOT_BAUD,
	       "line: an unanswered speed, slow (%d %s)", error, session.reason);
	expect(fake.resets == 2U && fake_count(&fake, 0xfc18U) == 1U && fake.records == 5U, "line: an unanswered speed, reset and loaded once (%u %u)", fake.resets, fake.records);
	fake_stop(&fake, &session, thread);

	/* A controller that cannot keep up with the fast line: the minidriver goes unanswered, then the start at 115200. */
	fake_init(&fake);
	fake.fast_broken = 1;
	fake_start(&fake, &session, &thread);
	error = btd_session_start(&session);
	expect(error == 0 && session.state == BTD_STATE_READY && session.slow && fake.resets == 2U,
	       "line: a broken fast line, slow (%d %s %u)", error, session.reason, fake.resets);
	expect(fake.host_speed == TEST_BOOT_BAUD && fake.controller_speed == TEST_BOOT_BAUD, "line: both ends at 115200");
	fake_stop(&fake, &session, thread);

	/* Set UART Clock refused: the speed changes all the same. */
	fake_init(&fake);
	fake.refuse_clock = 1;
	fake_start(&fake, &session, &thread);
	error = btd_session_start(&session);
	expect(error == 0 && session.state == BTD_STATE_READY && !session.slow && session.line_speed == TEST_FAST_BAUD,
	       "line: a refused clock (%d %s)", error, session.reason);
	fake_stop(&fake, &session, thread);

	/* Launch RAM unanswered: the patch starts all the same. */
	fake_init(&fake);
	fake.silent_launch = 1;
	fake_start(&fake, &session, &thread);
	error = btd_session_start(&session);
	expect(error == 0 && session.state == BTD_STATE_READY && session.launch_unanswered && !session.slow,
	       "line: Launch RAM unanswered (%d %s)", error, session.reason);
	fake_stop(&fake, &session, thread);

	/* A controller with an address of its own keeps it. */
	fake_init(&fake);
	memcpy(fake.rom_address, "\x01\x02\x03\x04\x05\x06", 6U);
	fake_start(&fake, &session, &thread);
	error = btd_session_start(&session);
	expect(error == 0 && session.address_choice == BTD_BROADCOM_ADDRESS_KEEP && fake_count(&fake, 0xfc01U) == 0U,
	       "line: an own address kept (%d)", error);
	fake_stop(&fake, &session, thread);

	/* A board without an address: the default one is kept (D3). */
	fake_init(&fake);
	fake.info.flags = BT_INFO_SPEED;
	memset(fake.info.board_address, 0, sizeof(fake.info.board_address));
	fake_start(&fake, &session, &thread);
	error = btd_session_start(&session);
	expect(error == 0 && session.address_choice == BTD_BROADCOM_ADDRESS_DEFAULT && fake_count(&fake, 0xfc01U) == 0U,
	       "line: no board's address, the default kept (%d)", error);
	expect(memcmp(session.address, default_address, 6U) == 0, "line: the default address read");
	fake_stop(&fake, &session, thread);
}

/* A refused record, a reset during the load, a failed reset, no notice. */
static void
test_session_failures(void)
{
	struct btd_session session;
	struct fake fake;
	pthread_t thread;
	int error;

	/* A record refused every time: tried at 115200 too, then the firmware failed. */
	fake_init(&fake);
	fake.refuse_record = 3U;
	fake.refuse_status = 0x12U;
	fake_start(&fake, &session, &thread);
	error = btd_session_start(&session);
	expect(error == EIO && session.state == BTD_STATE_FIRMWARE_FAILED && strcmp(session.reason, "record 2 status 0x12") == 0,
	       "failure: a refused record (%d %s %s)", error, btd_state_name(session.state), session.reason);
	expect(fake.resets == 2U && session.slow, "failure: a refused record tried twice");
	fake_stop(&fake, &session, thread);

	/* The kernel reset the line during the load once: the second try loads it. */
	fake_init(&fake);
	fake.reset_at_record = 4U;
	fake.reset_once = 1;
	fake_start(&fake, &session, &thread);
	error = btd_session_start(&session);
	expect(error == 0 && session.state == BTD_STATE_READY && session.slow && fake.resets == 3U && fake_calls(&fake, BT_IOC_RESET) == 2U,
	       "failure: a reset during the load (%d %s %u)", error, session.reason, fake.resets);
	expect(fake.records == 8U && fake.record_mismatch == 0U, "failure: three records, then all five again (%u)", fake.records);
	fake_stop(&fake, &session, thread);

	/* The kernel's reset fails. */
	fake_init(&fake);
	fake.reset_fails = 1;
	fake_start(&fake, &session, &thread);
	error = btd_session_start(&session);
	expect(error == EIO && session.state == BTD_STATE_ERROR && strncmp(session.reason, "power-reset", 11U) == 0,
	       "failure: the reset fails (%d %s)", error, session.reason);
	expect(fake.opcode_count == 0U, "failure: nothing sent after a failed reset");
	fake_stop(&fake, &session, thread);

	/* The reset's notice never comes. */
	fake_init(&fake);
	fake.no_notice = 1;
	fake_start(&fake, &session, &thread);
	error = btd_session_start(&session);
	expect(error == ETIMEDOUT && session.state == BTD_STATE_ERROR && strcmp(session.reason, "no reset notice") == 0,
	       "failure: no notice (%d %s)", error, session.reason);
	fake_stop(&fake, &session, thread);
}

/* A missing file, an unknown chip, a broken file, another manufacturer, a reset of the ready controller. */
static void
test_session_others(void)
{
	static uint8_t broken[256];
	struct btd_session session;
	struct btd_retry retry;
	struct fake fake;
	pthread_t thread;
	char folder[256];
	char expected[300];
	size_t length;
	int error;

	/* No patch file: the firmware is needed, nothing of the load sent, the line not changed. */
	(void)snprintf(folder, sizeof(folder), "%s/empty", patch_folder);
	(void)mkdir(folder, 0755);
	fake_init(&fake);
	fake_start(&fake, &session, &thread);
	session.broadcom_folder = folder;
	error = btd_session_start(&session);
	(void)snprintf(expected, sizeof(expected), "%s/BCM4345C0.hcd", folder);
	expect(error == ENOENT && session.state == BTD_STATE_FIRMWARE_NEEDED && strcmp(session.reason, expected) == 0,
	       "other: a missing file (%d %s)", error, session.reason);
	expect(fake_count(&fake, 0xfc2eU) == 0U && fake_count(&fake, 0xfc18U) == 0U, "other: nothing loaded without a file");
	fake_stop(&fake, &session, thread);

	/* A chip with no file in the table: its name in the reason. */
	fake_init(&fake);
	fake.name = "BCM20702A1\x01";
	fake.name_length = 11U;
	fake_start(&fake, &session, &thread);
	error = btd_session_start(&session);
	expect(error == ENOENT && session.state == BTD_STATE_FIRMWARE_NEEDED && strcmp(session.reason, "chip \"BCM20702A1?\" has no patch file") == 0,
	       "other: an unknown chip (%d %s)", error, session.reason);
	fake_stop(&fake, &session, thread);

	/* A broken file: the firmware failed before anything of it was sent. */
	(void)snprintf(folder, sizeof(folder), "%s/broken", patch_folder);
	(void)mkdir(folder, 0755);
	length = make_hcd(broken, sizeof(broken), 2U, 0);
	(void)snprintf(expected, sizeof(expected), "broken/BCM4345C0.hcd");
	write_file(expected, broken, length);
	fake_init(&fake);
	fake_start(&fake, &session, &thread);
	session.broadcom_folder = folder;
	error = btd_session_start(&session);
	expect(error == EBADMSG && session.state == BTD_STATE_FIRMWARE_FAILED && strstr(session.reason, "no launch-ram at the end") != NULL,
	       "other: a broken file (%d %s)", error, session.reason);
	expect(fake_count(&fake, 0xfc2eU) == 0U, "other: nothing of a broken file sent");
	fake_stop(&fake, &session, thread);

	/* Another manufacturer's controller on a UART: reset, then the core's set-up as it is. */
	fake_init(&fake);
	fake.manufacturer = 2U;
	fake_start(&fake, &session, &thread);
	error = btd_session_start(&session);
	expect(error == 0 && session.state == BTD_STATE_READY && !session.broadcom && !session.firmware_loaded,
	       "other: not Broadcom's (%d %s)", error, session.reason);
	expect(fake_count(&fake, 0x0c14U) == 0U && fake_count(&fake, 0xfc18U) == 0U && fake.resets == 1U, "other: no vendor command");
	fake_stop(&fake, &session, thread);

	/* Cypress's controller is patched as Broadcom's. */
	fake_init(&fake);
	fake.manufacturer = 305U;
	fake_start(&fake, &session, &thread);
	error = btd_session_start(&session);
	expect(error == 0 && session.broadcom && session.firmware_loaded, "other: Cypress's (%d %s)", error, session.reason);

	/* The ready controller reset by the kernel: the session's state is an error, and the daemon counts it. */
	btd_retry_clear(&retry);
	btd_retry_ready(&retry, 1000U);
	fake_send(&fake, (const uint8_t *)"\x80", 1U);
	error = btd_session_input(&session);
	expect(error == 0 && session.state == BTD_STATE_ERROR && session.resets == 1U, "other: an unasked reset (%d)", error);
	expect(btd_retry_reset(&retry, 2000U) == 0 && retry.failures == 1U, "other: the unasked reset counted");
	fake_stop(&fake, &session, thread);
}

/* The package's BCM4345C0.hcd: checked, and loaded whole into the scripted controller. */
static void
test_real(
	const char *folder)
{
	struct btd_session session;
	struct fake fake;
	pthread_t thread;
	const char *reason;
	struct stat status;
	uint8_t *file;
	size_t records;
	ssize_t got;
	char path[512];
	int descriptor;
	int error;

	/* The file, read whole. */
	(void)snprintf(path, sizeof(path), "%s/BCM4345C0.hcd", folder);
	descriptor = open(path, O_RDONLY);
	expect(descriptor >= 0, "real: %s opens", path);
	if (descriptor < 0)
		return;
	(void)fstat(descriptor, &status);
	file = malloc((size_t)status.st_size);
	if (file == NULL) {
		(void)close(descriptor);
		expect(0, "real: memory");
		return;
	}
	got = read(descriptor, file, (size_t)status.st_size);
	(void)close(descriptor);
	expect(got == status.st_size && got == 63806, "real: 63806 bytes (%zd)", got);

	/* Its records: 322 Write RAM and Launch RAM. */
	error = btd_broadcom_hcd_check(file, (size_t)got, &records, &reason);
	expect(error == 0 && records == 323U, "real: checked (%d %zu)", error, records);

	/* Loaded into the scripted controller, every record compared. */
	fake_init(&fake);
	fake.file = file;
	fake.file_length = (size_t)got;
	fake_start(&fake, &session, &thread);
	session.broadcom_folder = folder;
	error = btd_session_start(&session);
	expect(error == 0 && session.state == BTD_STATE_READY && session.firmware_loaded, "real: ready (%d %s)", error, session.reason);
	expect(fake.records == 322U && fake.record_mismatch == 0U && fake_count(&fake, 0xfc4eU) == 1U,
	       "real: 322 records as the file has them (%u %u)", fake.records, fake.record_mismatch);
	fake_stop(&fake, &session, thread);
	free(file);
	printf("bt-broadcom-host-test: the real BCM4345C0.hcd loaded (%zu records)\n", records);
}

/* Writes a synthetic .hcd file: records Write RAM records of 10 bytes (a pattern), then Launch RAM's when asked. */
static size_t
make_hcd(
	uint8_t *file,
	size_t size,
	unsigned records,
	int launch)
{
	unsigned record;
	unsigned byte;
	size_t used;

	/* Each Write RAM record: its opcode, its length, an address and a pattern. */
	used = 0U;
	for (record = 0U; record < records && used + 13U <= size; record++) {
		file[used++] = 0x4cU;
		file[used++] = 0xfcU;
		file[used++] = 10U;
		for (byte = 0U; byte < 10U; byte++)
			file[used++] = (uint8_t)(record * 16U + byte);
	}

	/* Launch RAM, with its address. */
	if (launch && used + 7U <= size) {
		file[used++] = 0x4eU;
		file[used++] = 0xfcU;
		file[used++] = 4U;
		file[used++] = 0xffU;
		file[used++] = 0xffU;
		file[used++] = 0xffU;
		file[used++] = 0xffU;
	}

	/* The file's length. */
	return used;
}

/* Writes a file of the patch folder. */
static void
write_file(
	const char *name,
	const uint8_t *bytes,
	size_t length)
{
	char path[512];
	ssize_t written;
	int descriptor;

	/* The file, in place of an old one. */
	(void)snprintf(path, sizeof(path), "%s/%s", patch_folder, name);
	descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (descriptor < 0) {
		perror(path);
		exit(2);
	}
	written = write(descriptor, bytes, length);
	(void)close(descriptor);
	if (written != (ssize_t)length) {
		perror(path);
		exit(2);
	}
}

/* Sets up the Raspberry Pi 4's controller in its ROM: Broadcom's BCM4345C0, its default address, the synthetic patch, the board's line. */
static void
fake_init(
	struct fake *fake)
{
	/* Nothing seen, no fault. */
	memset(fake, 0, sizeof(*fake));
	(void)pthread_mutex_init(&fake->lock, NULL);

	/* The controller in its ROM. */
	fake->manufacturer = 15U;
	fake->name = "BCM4345C0";
	fake->name_length = 9U;
	memcpy(fake->rom_address, default_address, 6U);
	fake_rom(fake);
	fake->file = synthetic;
	fake->file_length = synthetic_length;

	/* The node as the kernel tells it. */
	fake->info.bus = BT_BUS_UART;
	fake->info.flags = BT_INFO_SPEED | BT_INFO_ADDRESS;
	fake->info.acl_data_max = BT_ACL_DATA_DEFAULT;
	fake->info.speed_max = TEST_FAST_BAUD;
	memcpy(fake->info.board_address, board_address, 6U);
	(void)snprintf(fake->info.name, sizeof(fake->info.name), "%s", "Raspberry Pi 4 Bluetooth");
	(void)snprintf(fake->info.physical_path, sizeof(fake->info.physical_path), "%s", "uart0");
}

/* Starts the scripted controller on a socket pair and the session on the other end, with short timing. */
static void
fake_start(
	struct fake *fake,
	struct btd_session *session,
	pthread_t *thread)
{
	int pair[2];
	int status;

	/* A socket pair that keeps packets apart, as the node does. */
	status = socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair);
	if (status != 0) {
		perror("socketpair");
		exit(2);
	}

	/* The session on one end, the controller on the other; the patch folder the script made. */
	fake->descriptor = pair[1];
	btd_session_init(session, pair[0], fake_control, fake, "fake-uart", patch_folder);
	session->broadcom_folder = patch_folder;
	session->timing.command_ms = 200U;
	session->timing.reset_ms = 200U;
	session->timing.minidriver_ms = 1U;
	session->timing.launch_ms = 100U;
	session->timing.settle_ms = 1U;

	/* The controller's thread. */
	status = pthread_create(thread, NULL, fake_run, fake);
	if (status != 0) {
		perror("pthread_create");
		exit(2);
	}
}

/* Ends the scripted controller: the session's end closes, the controller sees it and returns. */
static void
fake_stop(
	struct fake *fake,
	struct btd_session *session,
	pthread_t thread)
{
	/* The session's end, then the controller. */
	(void)shutdown(session->descriptor, SHUT_RDWR);
	(void)pthread_join(thread, NULL);
	(void)close(session->descriptor);
	(void)close(fake->descriptor);
	(void)pthread_mutex_destroy(&fake->lock);
}

/* The scripted controller: takes each command as the line's two ends allow, until its socket closes. */
static void *
fake_run(
	void *argument)
{
	struct fake *fake;
	uint8_t packet[300];
	uint16_t opcode;
	ssize_t got;
	int garbled;

	/* Plays until the session's end closes. */
	fake = argument;
	for (;;) {
		/* The next command (the session closing ends the play). */
		got = read(fake->descriptor, packet, sizeof(packet));
		if (got <= 0)
			return NULL;
		if (packet[0] != 0x01U || got < 4)
			continue;
		opcode = (uint16_t)(packet[1] | (packet[2] << 8));

		/* Under the lock: what the controller makes of it. */
		pthread_mutex_lock(&fake->lock);

		/* Seen. */
		if (fake->opcode_count < TEST_OPCODES)
			fake->opcodes[fake->opcode_count++] = opcode;

		/* The minidriver's trailing bytes still on the line: the kernel loses step and resets the controller. */
		if (fake->trailing_pending) {
			fake_rom(fake);
			fake->resets++;
			fake_send(fake, (const uint8_t *)"\x80", 1U);
			pthread_mutex_unlock(&fake->lock);
			continue;
		}

		/* Ends at different speeds, or a speed the controller cannot keep up with: nothing it can read. */
		garbled = 0;
		if (fake->controller_speed != fake->host_speed)
			garbled = 1;
		if (fake->fast_broken && fake->controller_speed > TEST_BOOT_BAUD)
			garbled = 1;
		if (!garbled)
			fake_answer(fake, opcode, packet, (size_t)got);

		pthread_mutex_unlock(&fake->lock);
	}
}

/* Answers one command the controller could read (the lock held). */
static void
fake_answer(
	struct fake *fake,
	uint16_t opcode,
	const uint8_t *packet,
	size_t length)
{
	static const uint8_t ok[1] = { 0x00U };
	static const uint8_t features[9] = { 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x40U, 0x00U, 0x00U, 0x00U };
	static const uint8_t buffer_size[8] = { 0x00U, 0xfdU, 0x03U, 0x40U, 0x08U, 0x00U, 0x08U, 0x00U };
	uint8_t returned[260];
	uint32_t baud;
	uint16_t subversion;

	/* Each command's answer. */
	switch (opcode) {
	case 0xfc18U:
		/* Update UART Baud Rate: answered at the old speed, then the controller's end changes. */
		if (fake->silent_baud)
			return;
		fake_complete(fake, opcode, ok, sizeof(ok));
		baud = (uint32_t)packet[6] | ((uint32_t)packet[7] << 8) | ((uint32_t)packet[8] << 16) | ((uint32_t)packet[9] << 24);
		fake->controller_speed = baud;
		break;
	case 0xfc2eU:
		/* Download Minidriver, perhaps followed by the trailing bytes. */
		fake->minidriver = 1;
		fake_complete(fake, opcode, ok, sizeof(ok));
		if (fake->trailing)
			fake->trailing_pending = 1;
		break;
	case 0xfc4cU:
		fake_record(fake, opcode, packet, length);
		break;
	case 0xfc4eU:
		/* Launch RAM: the patch starts, and the controller restarts at the ROM's speed. */
		if (!fake->silent_launch)
			fake_complete(fake, opcode, ok, sizeof(ok));
		fake->patched = 1;
		fake->minidriver = 0;
		fake->controller_speed = TEST_BOOT_BAUD;
		break;
	case 0xfc45U:
		/* Set UART Clock, refused by a controller without it (Unknown HCI Command). */
		fake->clock = packet[4];
		returned[0] = 0x00U;
		if (fake->refuse_clock)
			returned[0] = 0x01U;
		fake_complete(fake, opcode, returned, 1U);
		break;
	case 0xfc01U:
		/* Write BD_ADDR. */
		memcpy(fake->address, packet + 4, 6U);
		fake_complete(fake, opcode, ok, sizeof(ok));
		break;
	case 0x1001U:
		/* Read Local Version: HCI 5.0, the manufacturer, the ROM's or the patch's subversion. */
		subversion = TEST_ROM_SUBVERSION;
		if (fake->patched)
			subversion = TEST_PATCH_SUBVERSION;
		returned[0] = 0x00U;
		returned[1] = 0x09U;
		returned[2] = 0x00U;
		returned[3] = 0x00U;
		returned[4] = 0x09U;
		returned[5] = (uint8_t)(fake->manufacturer & 0xffU);
		returned[6] = (uint8_t)(fake->manufacturer >> 8);
		returned[7] = (uint8_t)(subversion & 0xffU);
		returned[8] = (uint8_t)(subversion >> 8);
		fake_complete(fake, opcode, returned, 9U);
		break;
	case 0x0c14U:
		/* Read Local Name: the name in a field of 248 bytes. */
		memset(returned, 0, sizeof(returned));
		memcpy(returned + 1, fake->name, fake->name_length);
		fake_complete(fake, opcode, returned, 249U);
		break;
	case 0x1009U:
		/* Read BD_ADDR. */
		returned[0] = 0x00U;
		memcpy(returned + 1, fake->address, 6U);
		fake_complete(fake, opcode, returned, 7U);
		break;
	case 0x1002U:
		/* The supported commands: none of LE's. */
		memset(returned, 0, sizeof(returned));
		fake_complete(fake, opcode, returned, 65U);
		break;
	case 0x1003U:
		fake_complete(fake, opcode, features, sizeof(features));
		break;
	case 0x1005U:
		fake_complete(fake, opcode, buffer_size, sizeof(buffer_size));
		break;
	default:
		fake_complete(fake, opcode, ok, sizeof(ok));
		break;
	}
}

/* Takes a Write RAM record: compared with the file, refused or reset under it when the test asks (the lock held). */
static void
fake_record(
	struct fake *fake,
	uint16_t opcode,
	const uint8_t *packet,
	size_t length)
{
	uint8_t status;
	size_t parameters;
	int differs;

	/* The kernel resets the line under this record once. */
	if (fake->reset_at_record != 0U && fake->load_records + 1U == fake->reset_at_record && fake->reset_once) {
		fake->reset_once = 0;
		fake_rom(fake);
		fake->resets++;
		fake_send(fake, (const uint8_t *)"\x80", 1U);
		return;
	}

	/* The record, compared with the file's next one (its header included). */
	parameters = length - 4U;
	differs = 1;
	if (fake->minidriver && fake->file_offset + 3U + parameters <= fake->file_length) {
		differs = memcmp(packet + 1, fake->file + fake->file_offset, 3U + parameters);
		fake->file_offset += 3U + parameters;
	}
	if (differs != 0)
		fake->record_mismatch++;
	fake->records++;
	fake->load_records++;

	/* Its answer: refused when the test asks. */
	status = 0x00U;
	if (fake->refuse_record != 0U && fake->load_records == fake->refuse_record)
		status = fake->refuse_status;
	fake_complete(fake, opcode, &status, 1U);
}

/* Writes one H4 packet to the session. */
static void
fake_send(
	struct fake *fake,
	const uint8_t *packet,
	size_t length)
{
	/* One packet. */
	(void)write(fake->descriptor, packet, length);
}

/* Writes a Command Complete for an opcode with its return parameters. */
static void
fake_complete(
	struct fake *fake,
	uint16_t opcode,
	const uint8_t *returned,
	size_t count)
{
	uint8_t event[262];

	/* Type, code, length, window, opcode, return parameters. */
	event[0] = 0x04U;
	event[1] = 0x0eU;
	event[2] = (uint8_t)(3U + count);
	event[3] = 1U;
	event[4] = (uint8_t)(opcode & 0xffU);
	event[5] = (uint8_t)(opcode >> 8);
	memcpy(event + 6, returned, count);
	fake_send(fake, event, 6U + count);
}

/* Puts the controller back in its ROM: both ends at 115200, no minidriver, no patch, the ROM's address (the lock held, or not started). */
static void
fake_rom(
	struct fake *fake)
{
	/* The line. */
	fake->controller_speed = TEST_BOOT_BAUD;
	fake->host_speed = TEST_BOOT_BAUD;
	fake->trailing_pending = 0;

	/* The controller. */
	fake->minidriver = 0;
	fake->patched = 0;
	fake->file_offset = 0U;
	fake->load_records = 0U;
	memcpy(fake->address, fake->rom_address, 6U);
}

/* Stands in for the node's ioctls: the information, the reset with its notice, the host's end of the line; every call recorded. */
static int
fake_control(
	void *context,
	unsigned long request,
	void *argument)
{
	struct fake *fake;
	int error;

	/* Recorded with its value. */
	fake = context;
	pthread_mutex_lock(&fake->lock);
	if (fake->call_count < TEST_CALLS) {
		fake->calls[fake->call_count] = request;
		fake->call_values[fake->call_count] = 0U;
		if (request == BT_IOC_SET_SPEED || request == BT_IOC_SET_ACL_MAX)
			fake->call_values[fake->call_count] = *(uint32_t *)argument;
		fake->call_count++;
	}

	/* Each request. */
	error = 0;
	if (request == BT_IOC_GET_INFO) {
		memcpy(argument, &fake->info, sizeof(fake->info));
	} else if (request == BT_IOC_RESET && fake->reset_fails) {
		error = EIO;
	} else if (request == BT_IOC_RESET) {
		/* The power cycled: the ROM, both ends at 115200, a stale answer of before, then the notice. */
		fake_rom(fake);
		fake->resets++;
		if (fake->stale_packets)
			fake_complete(fake, 0x1234U, (const uint8_t *)"\x00", 1U);
		if (!fake->no_notice)
			fake_send(fake, (const uint8_t *)"\x80", 1U);
	} else if (request == BT_IOC_SET_SPEED) {
		/* The host's end, which drops what the line carried meanwhile. */
		fake->host_speed = *(uint32_t *)argument;
		fake->trailing_pending = 0;
	}

	pthread_mutex_unlock(&fake->lock);

	/* A stale packet after the notice too, which the session drops with the rest. */
	if (request == BT_IOC_RESET && fake->stale_packets && !fake->reset_fails)
		fake_send(fake, (const uint8_t *)"\x04\x10\x01\x01", 4U);

	/* The request's answer. */
	return error;
}

/* Counts the commands of an opcode the controller saw. */
static unsigned
fake_count(
	const struct fake *fake,
	uint16_t opcode)
{
	unsigned index;
	unsigned count;

	/* Each command seen. */
	count = 0U;
	for (index = 0U; index < fake->opcode_count; index++) {
		if (fake->opcodes[index] == opcode)
			count++;
	}

	/* The count. */
	return count;
}

/* Counts the node's calls of a request. */
static unsigned
fake_calls(
	const struct fake *fake,
	unsigned long request)
{
	unsigned index;
	unsigned count;

	/* Each call. */
	count = 0U;
	for (index = 0U; index < fake->call_count; index++) {
		if (fake->calls[index] == request)
			count++;
	}

	/* The count. */
	return count;
}

/* Tells whether the controller's first commands are exactly the ones given, in their order. */
static int
fake_order(
	const struct fake *fake,
	const uint16_t *opcodes,
	unsigned count)
{
	unsigned index;

	/* Too few seen. */
	if (fake->opcode_count < count)
		return 0;

	/* Each in its place. */
	for (index = 0U; index < count; index++) {
		if (fake->opcodes[index] != opcodes[index]) {
			printf("order: command %u is 0x%04x, not 0x%04x\n", index, fake->opcodes[index], opcodes[index]);
			return 0;
		}
	}

	/* Succeeded: the order held. */
	return 1;
}
