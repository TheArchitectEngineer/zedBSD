/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of net.conf's interface MTU (userland/base/net/netconf.c
 * and reconcile.c with the host's headers): the key read, validated and
 * written back, the files refused, and the MTU operations the reconcile
 * emits between two configurations.
 */

#include "userland/base/net/netconf.h"
#include "userland/base/net/reconcile.h"

#include <stdio.h>
#include <string.h>

static int test_failures;

static void check(int condition, const char *what);
static int parse_text(const char *text, struct netconf *configuration, char *error, size_t size);
static int record(const char *operation, const char *operands, void *context);
static int reconcile_text(const char *previous, const char *target);

/* The operations the reconcile emitted, one a line. */
static char test_program[4096];

/* ue0 with DHCP and an MTU, ue1 static without one, ue2 disabled with one. */
static const char test_mtu[] =
	"version: 1\n"
	"\n"
	"interfaces:\n"
	"  ue0:\n"
	"    type: ethernet\n"
	"    enabled: true\n"
	"    mtu: 9000\n"
	"    ipv4:\n"
	"      dhcp: true\n"
	"  ue1:\n"
	"    type: ethernet\n"
	"    enabled: true\n"
	"    ipv4:\n"
	"      addresses:\n"
	"        - address: 192.168.10.2\n"
	"          prefix-length: 24\n"
	"  ue2:\n"
	"    type: ethernet\n"
	"    enabled: false\n"
	"    mtu: 1400\n"
	"\n"
	"dns:\n"
	"  mode: dhcp\n";

/* The same with ue0's MTU changed, ue1 given one and ue2 enabled. */
static const char test_changed[] =
	"version: 1\n"
	"\n"
	"interfaces:\n"
	"  ue0:\n"
	"    type: ethernet\n"
	"    enabled: true\n"
	"    mtu: 1500\n"
	"    ipv4:\n"
	"      dhcp: true\n"
	"  ue1:\n"
	"    type: ethernet\n"
	"    enabled: true\n"
	"    mtu: 1280\n"
	"    ipv4:\n"
	"      addresses:\n"
	"        - address: 192.168.10.2\n"
	"          prefix-length: 24\n"
	"  ue2:\n"
	"    type: ethernet\n"
	"    enabled: true\n"
	"    mtu: 1400\n"
	"\n"
	"dns:\n"
	"  mode: dhcp\n";

/* No MTU anywhere. */
static const char test_none[] =
	"version: 1\n"
	"\n"
	"interfaces:\n"
	"  ue0:\n"
	"    type: ethernet\n"
	"    enabled: true\n"
	"    ipv4:\n"
	"      dhcp: true\n"
	"  ue1:\n"
	"    type: ethernet\n"
	"    enabled: true\n"
	"    ipv4:\n"
	"      addresses:\n"
	"        - address: 192.168.10.2\n"
	"          prefix-length: 24\n"
	"  ue2:\n"
	"    type: ethernet\n"
	"    enabled: false\n"
	"\n"
	"dns:\n"
	"  mode: dhcp\n";

/* Runs the checks; the exit status is 1 when one failed. */
int
main(void)
{
	struct netconf configuration;
	struct netconf again;
	char error[256];
	char written[4096];
	FILE *stream;
	size_t length;
	int status;

	/* The key read. */
	status = parse_text(test_mtu, &configuration, error, sizeof(error));
	check(status == 0, "the file with MTUs is read");
	if (status != 0)
		printf("  error: %s\n", error);
	check(configuration.interfaces[0].mtu_set && configuration.interfaces[0].mtu == 9000U, "ue0: mtu 9000");
	check(!configuration.interfaces[1].mtu_set && configuration.interfaces[1].mtu == 0U, "ue1: no mtu");
	check(configuration.interfaces[2].mtu_set && configuration.interfaces[2].mtu == 1400U, "ue2: mtu 1400");

	/* Written and read again: the same, and written only where it was named. */
	stream = fmemopen(written, sizeof(written), "w");
	status = -1;
	if (stream != NULL) {
		status = netconf_write(stream, &configuration);
		fclose(stream);
	}
	check(status == 0, "written");
	length = strnlen(written, sizeof(written));
	status = length < sizeof(written) ? parse_text(written, &again, error, sizeof(error)) : -1;
	check(status == 0, "read again");
	check(again.interfaces[0].mtu_set && again.interfaces[0].mtu == 9000U, "ue0's mtu kept");
	check(!again.interfaces[1].mtu_set, "ue1 still without one");
	check(again.interfaces[2].mtu_set && again.interfaces[2].mtu == 1400U, "ue2's mtu kept");
	check(strstr(written, "    mtu: 9000\n") != NULL && strstr(written, "    mtu: 1400\n") != NULL, "the mtu lines written");

	/* A file without the key is written without it. */
	status = parse_text(test_none, &configuration, error, sizeof(error));
	check(status == 0, "the file without MTUs is read");
	stream = fmemopen(written, sizeof(written), "w");
	status = -1;
	if (stream != NULL) {
		status = netconf_write(stream, &configuration);
		fclose(stream);
	}
	check(status == 0 && strstr(written, "mtu") == NULL, "no mtu line written for none named");

	/* Refused: below 1280, above 65535, not a number, named twice. */
	status = parse_text("version: 1\ninterfaces:\n  ue0:\n    type: ethernet\n    enabled: true\n    mtu: 1279\ndns:\n  mode: dhcp\n",
	    &again, error, sizeof(error));
	check(status != 0, "refused: mtu 1279");
	status = parse_text("version: 1\ninterfaces:\n  ue0:\n    type: ethernet\n    enabled: true\n    mtu: 65536\ndns:\n  mode: dhcp\n",
	    &again, error, sizeof(error));
	check(status != 0, "refused: mtu 65536");
	status = parse_text("version: 1\ninterfaces:\n  ue0:\n    type: ethernet\n    enabled: true\n    mtu: 9k\ndns:\n  mode: dhcp\n",
	    &again, error, sizeof(error));
	check(status != 0, "refused: mtu 9k");
	status = parse_text("version: 1\ninterfaces:\n  ue0:\n    type: ethernet\n    enabled: true\n    mtu: 9000\n    mtu: 9000\ndns:\n  mode: dhcp\n",
	    &again, error, sizeof(error));
	check(status != 0, "refused: mtu named twice");
	status = parse_text("version: 1\ninterfaces:\n  ue0:\n    type: ethernet\n    enabled: true\n    mtu: 65535\ndns:\n  mode: dhcp\n",
	    &again, error, sizeof(error));
	check(status == 0, "read: mtu 65535 (the device's limit applies later)");

	/* The reconcile: an MTU named where there was none, before ue0 is configured. */
	status = reconcile_text(test_none, test_mtu);
	check(status == 0, "reconciled: none to MTUs");
	check(strstr(test_program, "MTU ue0 9000\nUP ue0\n") != NULL, "ue0: MTU 9000 before it is configured");
	check(strstr(test_program, "MTU ue1") == NULL, "ue1: no MTU (none named either side)");
	check(strstr(test_program, "MTU ue2") == NULL, "ue2: no MTU while disabled");
	check(strstr(test_program, "DOWN ue0") == NULL, "ue0: not taken down for it");

	/* The same on both sides: nothing. */
	status = reconcile_text(test_mtu, test_mtu);
	check(status == 0 && strstr(test_program, "MTU ") == NULL, "reconciled: the same MTUs emit nothing");

	/* Changed, newly named, and an interface enabled with one. */
	status = reconcile_text(test_mtu, test_changed);
	check(status == 0, "reconciled: changed MTUs");
	check(strstr(test_program, "MTU ue0 1500\n") != NULL, "ue0: MTU 1500");
	check(strstr(test_program, "MTU ue1 1280\n") != NULL, "ue1: MTU 1280");
	check(strstr(test_program, "MTU ue2 1400\nUP ue2\n") != NULL, "ue2: enabled, its MTU emitted though named before (a disabled one was given none)");

	/* Taken out of the file: back to the device's default. */
	status = reconcile_text(test_changed, test_none);
	check(status == 0, "reconciled: MTUs removed");
	check(strstr(test_program, "MTU ue0 0\n") != NULL && strstr(test_program, "MTU ue1 0\n") != NULL,
	    "ue0 and ue1: MTU 0 (the default)");
	check(strstr(test_program, "MTU ue2") == NULL, "ue2: disabled now, no MTU");

	/* Enabled again without one after it was disabled: the device's default. */
	status = reconcile_text(test_mtu, test_none);
	check(status == 0 && strstr(test_program, "MTU ue0 0\n") != NULL && strstr(test_program, "MTU ue2") == NULL,
	    "ue0: MTU 0, ue2 (disabled both sides): nothing");
	status = reconcile_text(test_none, test_changed);
	check(status == 0 && strstr(test_program, "MTU ue2 1400\n") != NULL, "ue2: enabled with an MTU after none");

	/* The result. */
	if (test_failures != 0) {
		printf("host-netconf-mtu: %d failed\n", test_failures);
		return 1;
	}

	/* Succeeded. */
	printf("host-netconf-mtu: PASS\n");
	return 0;
}

/* Counts a failed check, and prints each check's outcome. */
static void
check(
	int condition,
	const char *what)
{
	/* A failure. */
	if (!condition) {
		printf("FAIL: %s\n", what);
		test_failures++;
		return;
	}

	/* Succeeded. */
	printf("ok: %s\n", what);
}

/* Reads a net.conf text and validates it; 0, or -1 with the error. */
static int
parse_text(
	const char *text,
	struct netconf *configuration,
	char *error,
	size_t size)
{
	FILE *stream;
	int status;

	/* The text as a stream. */
	memset(configuration, 0, sizeof(*configuration));
	stream = fmemopen((void *)text, strlen(text), "r");
	if (stream == NULL)
		return -1;
	status = netconf_parse(stream, configuration, error, size);
	fclose(stream);
	if (status != 0)
		return status;

	/* Succeeded when it is valid. */
	status = netconf_validate(configuration, error, size);
	return status;
}

/* Reconciles one text to another into test_program; 0, or -1. */
static int
reconcile_text(
	const char *previous,
	const char *target)
{
	static struct netconf before;
	static struct netconf after;
	char error[256];
	int status;

	/* Both read. */
	test_program[0] = '\0';
	if (parse_text(previous, &before, error, sizeof(error)) != 0 ||
	    parse_text(target, &after, error, sizeof(error)) != 0) {
		printf("  error: %s\n", error);
		return -1;
	}

	/* The program between them. */
	status = netconf_reconcile(&before, &after, record, NULL, error, sizeof(error));
	if (status != 0)
		printf("  error: %s\n", error);
	return status;
}

/* Records one operation of the reconcile's program. */
static int
record(
	const char *operation,
	const char *operands,
	void *context)
{
	size_t used;

	/* After the ones before it. */
	(void)context;
	used = strlen(test_program);
	(void)snprintf(test_program + used, sizeof(test_program) - used, "%s%s%s\n", operation, operands != NULL ? " " : "",
	    operands != NULL ? operands : "");
	return 0;
}
