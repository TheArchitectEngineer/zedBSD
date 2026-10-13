/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws143-p007 (T1-593): the words the Settings Bluetooth page shows for a
 * request's result, linked with the page's objects of a Linux build.  The
 * page sees the errno value of the compositor's result, not the backend's:
 * the chain is the backend's errno -> bt_applied() (wayland/bluetooth-shell.c)
 * -> KL_SYSTEM_RESULT_* -> system_view_error_of() (libkeiland), which the
 * table below follows.
 */

#include "userland/desktop/settings/settings.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static struct se_app test_app;
static int test_failures;

static void test_result(const char *doing, int error, const char *words);

/* The backend's errno through the compositor's result to the page's errno. */
#define PAGE_ECONNREFUSED	EACCES		/* KL_SYSTEM_RESULT_REFUSED */
#define PAGE_EACCES		EPERM		/* KL_SYSTEM_RESULT_DENIED */
#define PAGE_ETIMEDOUT		ENETUNREACH	/* KL_SYSTEM_RESULT_UNREACHABLE */
#define PAGE_ENOTSUP		ENOTSUP		/* KL_SYSTEM_RESULT_UNSUPPORTED */
#define PAGE_EIO		EIO		/* KL_SYSTEM_RESULT_FAILED */

int
main(
	void)
{
	/* A connection with no profile (BlueZ NotAvailable -> ENOTSUP): not a pairing refused. */
	test_result("connect", PAGE_ENOTSUP, "This computer cannot connect to this kind of device.");

	/* A connection refused, unanswered, or failed otherwise (zedBSD's no-hid is EIO). */
	test_result("connect", PAGE_ECONNREFUSED, "The device refused the connection.");
	test_result("connect", PAGE_ETIMEDOUT, "The device did not answer. Check that it is on and nearby.");
	test_result("connect", PAGE_EIO, "The device could not be connected.");
	test_result("connect", PAGE_EACCES, "This account may not change Bluetooth.");

	/* A pairing keeps its words: refused (Linux's Authentication*, zedBSD's rejected), a PIN to type, unanswered. */
	test_result("pair", PAGE_ECONNREFUSED, "The pairing was refused.");
	test_result("pair", PAGE_ENOTSUP, "Bluetooth cannot do that here yet.");
	test_result("pair", PAGE_ETIMEDOUT, "The device did not answer.");
	test_result("pair", 0, "The device is paired.");
	test_result("connect", 0, "The device is connected.");

	/* The verdict. */
	if (test_failures != 0) {
		printf("bt-page-result-host-test: FAIL (%d)\n", test_failures);
		return 1;
	}
	printf("bt-page-result-host-test: PASS\n");
	return 0;
}

/* Gives the page a result of a request it asked, and checks its words. */
static void
test_result(
	const char *doing,
	int error,
	const char *words)
{
	struct se_bluetooth *bluetooth;
	int taken;
	int same;

	/* The page's request outstanding. */
	bluetooth = &test_app.bluetooth;
	bluetooth->request = 7U;
	(void)snprintf(bluetooth->doing, sizeof(bluetooth->doing), "%s", doing);

	/* Its result, and the words shown. */
	taken = se_bluetooth_result(&test_app, 7U, error);
	same = strcmp(bluetooth->message, words);
	if (taken == 1 && same == 0) {
		printf("ok %s errno=%d: %s\n", doing, error, bluetooth->message);
		return;
	}

	/* Not as wanted. */
	printf("not ok %s errno=%d: taken=%d \"%s\" (wanted \"%s\")\n", doing, error, taken, bluetooth->message, words);
	test_failures++;
}
