/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws174-p002: end-to-end host test of the boot keys' record rewrite (O11).
 *
 * Feeds three configurations the build writes through the UEFI loader's
 * parser, the boot keys' rewrite and the kernel's boot parameter parser, for
 * no key, Ctrl, Shift and both, and checks that the kernel accepts every
 * record with the expected kmsg= and login= values and logo= present only
 * when Ctrl was not held.  Built and run by
 * run-boot-override-kernel-host-test.sh.
 */

#include <kern/boot.h>

#include "bootloader/common/boot-override.h"
#include "bootloader/uefi/zedbsd-config.h"

#include <stdio.h>
#include <string.h>

/* The FAT volume serial the loader would have found the configuration on. */
#define TEST_VOLUME_UUID	"1234-ABCD"

/*
 * One configuration file and the kmsg= and login= the kernel sees from it
 * with no key held (NULL: the parameter is absent).
 */
struct test_configuration {
	const char *name;
	const char *text;
	const char *kmsg;
	const char *login;
	unsigned unknown_without_logo;
	int logo;
};

/*
 * The configurations the build writes: graphical with quiet kernel
 * messages, text, and the text one with the GPU development lines.
 */
static const struct test_configuration configurations[] = {
	{
		"graphical",
		"kernel=vmunix\nrootpart=PARTLABEL=zedBSD-root\nswap0=PARTLABEL=zedBSD-swap\n"
		"logo=logo.ppm\nlogin=graphical\nkmsg=quiet\n",
		"quiet",
		"graphical",
		0U,
		1
	},
	{
		"text",
		"kernel=vmunix\nvideo=640x480\nrootpart=PARTLABEL=zedBSD-root\nswap0=PARTLABEL=zedBSD-swap\n",
		NULL,
		NULL,
		1U,
		0
	},
	{
		"gpu-development",
		"kernel=vmunix\nvideo=640x480\nrootpart=PARTLABEL=zedBSD-root\nswap0=PARTLABEL=zedBSD-swap\n"
		"display=edp\nlogin=graphical\n",
		NULL,
		"graphical",
		1U,
		0
	}
};

/*
 * The checks run and the checks failed.
 *
 * Every check counts itself; the tally is printed at the end and decides the
 * exit status.
 */
static unsigned checks;
static unsigned failures;

static void check(int ok, const char *name, unsigned keys, const char *what);
static int value_is(const struct kern_boot_parameters *parameters, enum kern_boot_parameter_key key, const char *word);
static void test_configuration(const struct test_configuration *configuration, unsigned keys);

/*
 * Runs every configuration with every key combination and reports the tally.
 */
int
main(void)
{
	size_t index;
	unsigned keys;

	/* Every configuration with no key, Ctrl, Shift and both. */
	for (index = 0; index < sizeof(configurations) / sizeof(configurations[0]); index++) {
		for (keys = 0U; keys <= (ZBL_BOOT_OVERRIDE_KMSG | ZBL_BOOT_OVERRIDE_LOGIN); keys++)
			test_configuration(&configurations[index], keys);
	}

	printf("boot-override-kernel-host-test: %u checks, %u failures\n", checks, failures);

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
	const char *name,
	unsigned keys,
	const char *what)
{
	/* Every check counts. */
	checks++;

	/* A failed check is named with its configuration and keys. */
	if (!ok) {
		failures++;
		printf("FAIL: %s keys=%u: %s\n", name, keys, what);
	}
}

/* Reports whether a parameter is the word, or absent when the word is NULL. */
static int
value_is(
	const struct kern_boot_parameters *parameters,
	enum kern_boot_parameter_key key,
	const char *word)
{
	const char *value;
	int different;

	/* Reads the parameter. */
	value = kern_boot_parameters_value(parameters, key);

	/* An absent parameter matches only an absent word. */
	if (value == NULL) {
		if (word == NULL)
			return 1;
		return 0;
	}

	/* A given parameter does not match an absent word. */
	if (word == NULL)
		return 0;

	/* Compares the given word. */
	different = strcmp(value, word);
	if (different != 0)
		return 0;

	/* Succeeded: the parameter is the word. */
	return 1;
}

/* Passes one configuration through the loader, the keys and the kernel. */
static void
test_configuration(
	const struct test_configuration *configuration,
	unsigned keys)
{
	static struct zbl_uefi_kern_config loader;
	static struct kern_boot_parameters parameters;
	static char text[KERN_BOOT_PARAMETERS_STORAGE_SIZE];
	enum zbl_uefi_kern_config_result parsed;
	const char *kmsg;
	const char *login;
	unsigned expected_unknown;
	unsigned unknown;
	int error;
	int same;

	/* Assembles the record the way the UEFI loader does. */
	parsed = zbl_uefi_kern_config_parse(
		&loader,
		configuration->text,
		strlen(configuration->text),
		TEST_VOLUME_UUID,
		sizeof(TEST_VOLUME_UUID));
	check(parsed == ZBL_UEFI_KERN_CONFIG_OK, configuration->name, keys, "loader parse");
	if (parsed != ZBL_UEFI_KERN_CONFIG_OK)
		return;

	/* Rewrites it for the keys. */
	error = zbl_boot_override_apply(&loader.parameter_record, keys);
	check(error == 0, configuration->name, keys, "rewrite");

	/* Hands the text to the kernel's parser as the handoff does. */
	memset(text, 0, sizeof(text));
	memcpy(text, loader.parameter_record.text, loader.parameter_record.length);
	error = kern_boot_parameters_parse(&parameters, text, sizeof(text));
	check(error == 0, configuration->name, keys, "kernel parse accepts the record");
	if (error != 0) {
		printf("  error %d on \"%s\"\n", error, text);
		return;
	}

	/* Ctrl makes kmsg= console; otherwise it is what the file said. */
	kmsg = configuration->kmsg;
	if ((keys & ZBL_BOOT_OVERRIDE_KMSG) != 0U)
		kmsg = "console";
	same = value_is(&parameters, KERN_BOOT_PARAMETER_KMSG, kmsg);
	check(same, configuration->name, keys, "kmsg value");

	/* Shift makes login= console; otherwise it is what the file said. */
	login = configuration->login;
	if ((keys & ZBL_BOOT_OVERRIDE_LOGIN) != 0U)
		login = "console";
	same = value_is(&parameters, KERN_BOOT_PARAMETER_LOGIN, login);
	check(same, configuration->name, keys, "login value");

	/*
	 * The kernel counts logo= (and video=) as names it does not know: the
	 * graphical file's logo= is gone only when Ctrl was held.
	 */
	expected_unknown = configuration->unknown_without_logo;
	if (configuration->logo && (keys & ZBL_BOOT_OVERRIDE_KMSG) == 0U)
		expected_unknown++;
	unknown = kern_boot_parameters_unknown_count(&parameters);
	check(unknown == expected_unknown, configuration->name, keys, "unknown names");
}
