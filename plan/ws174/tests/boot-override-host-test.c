/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws174-p002: host test of the boot keys' record rewrite (O1 to O10).
 *
 * Links bootloader/common/boot-override.c and checks the rewritten text and
 * length for Ctrl, Shift and both, the untouched record when no key is held,
 * idempotence, whole-name matching, the length limit, the refusal of a
 * malformed record, and a dropped first or last token.  Built and run by
 * run-boot-override-host-test.sh.
 */

#include "bootloader/common/boot-override.h"

#include <stdio.h>
#include <string.h>

/* The graphical configuration the build writes, as the loader assembles it. */
#define GRAPHICAL_RECORD	"boot0=UUID=1234-ABCD rootpart=PARTLABEL=zedBSD-root swap0=PARTLABEL=zedBSD-swap logo=logo.ppm login=graphical kmsg=quiet"

/* The prefix every graphical result keeps. */
#define GRAPHICAL_KEPT		"boot0=UUID=1234-ABCD rootpart=PARTLABEL=zedBSD-root swap0=PARTLABEL=zedBSD-swap"

/* The text configuration the build writes, with no logo, login or kmsg line. */
#define TEXT_RECORD		"boot0=UUID=1234-ABCD video=640x480 rootpart=PARTLABEL=zedBSD-root swap0=PARTLABEL=zedBSD-swap"

/*
 * The checks run and the checks failed.
 *
 * Every check counts itself; the tally is printed at the end and decides the
 * exit status.
 */
static unsigned checks;
static unsigned failures;

static void check(int ok, const char *what);
static void record_set(struct kern_boot_parameter_record *record, const char *text);
static int record_is(const struct kern_boot_parameter_record *record, const char *text);
static int record_tail_clear(const struct kern_boot_parameter_record *record);
static void test_keys(void);
static void test_unchanged(void);
static void test_names(void);
static void test_limit(void);
static void test_refusals(void);
static void test_ends(void);

/*
 * Runs the checks and reports the tally.
 */
int
main(void)
{
	/* Runs every group of checks. */
	test_keys();
	test_unchanged();
	test_names();
	test_limit();
	test_refusals();
	test_ends();


	/* Prints the tally. */
	printf("boot-override-host-test: %u checks, %u failures\n", checks, failures);

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

/* Fills a record the way the loader's parser does, with the given text. */
static void
record_set(
	struct kern_boot_parameter_record *record,
	const char *text)
{
	size_t length;

	/* Starts from a cleared record. */
	memset(record, 0, sizeof(*record));

	/* The header the parser writes. */
	record->magic = KERN_BOOT_PARAMETER_RECORD_MAGIC;
	record->version = KERN_BOOT_PARAMETER_RECORD_VERSION;
	record->size = KERN_BOOT_PARAMETER_RECORD_SIZE;
	record->flags = KERN_BOOT_PARAMETER_RECORD_FLAG_TEXT;

	/* The text and its length. */
	length = strlen(text);
	memcpy(record->text, text, length);
	record->length = (uint16_t)length;
}

/* Reports whether the record holds exactly the text, length included. */
static int
record_is(
	const struct kern_boot_parameter_record *record,
	const char *text)
{
	size_t length;
	int different;

	/* The length must match the text's. */
	length = strlen(text);
	if (record->length != length) {
		printf("  length %u, expected %zu\n", (unsigned)record->length, length);
		return 0;
	}

	/* The text must match byte for byte and be terminated. */
	different = memcmp(record->text, text, length);
	if (different != 0 || record->text[length] != '\0') {
		printf("  text \"%.*s\"\n  want \"%s\"\n", (int)record->length, record->text, text);
		return 0;
	}

	/* Succeeded: the record is the text. */
	return 1;
}

/* Reports whether every byte after the terminator is zero. */
static int
record_tail_clear(
	const struct kern_boot_parameter_record *record)
{
	size_t index;

	/* Looks at every byte past the text. */
	for (index = record->length; index < sizeof(record->text); index++) {
		if (record->text[index] != '\0')
			return 0;
	}

	/* Succeeded: nothing stale is left. */
	return 1;
}

/* O1 to O4 and O6: Ctrl, Shift, both, a text record, and applying twice. */
static void
test_keys(void)
{
	struct kern_boot_parameter_record record;
	struct kern_boot_parameter_record again;
	int different;
	int error;
	int same;

	/* O1: Ctrl drops logo= and kmsg=quiet and appends kmsg=console. */
	record_set(&record, GRAPHICAL_RECORD);
	error = zbl_boot_override_apply(&record, ZBL_BOOT_OVERRIDE_KMSG);
	check(error == 0, "O1 Ctrl returns 0");
	same = record_is(&record, GRAPHICAL_KEPT " login=graphical kmsg=console");
	check(same, "O1 Ctrl record");
	same = record_tail_clear(&record);
	check(same, "O1 Ctrl leaves no stale text");
	check(record.magic == KERN_BOOT_PARAMETER_RECORD_MAGIC, "O1 header kept");

	/* O2: Shift replaces login=graphical with login=console. */
	record_set(&record, GRAPHICAL_RECORD);
	error = zbl_boot_override_apply(&record, ZBL_BOOT_OVERRIDE_LOGIN);
	check(error == 0, "O2 Shift returns 0");
	same = record_is(&record, GRAPHICAL_KEPT " logo=logo.ppm kmsg=quiet login=console");
	check(same, "O2 Shift record");

	/* O3: both keys. */
	record_set(&record, GRAPHICAL_RECORD);
	error = zbl_boot_override_apply(&record, ZBL_BOOT_OVERRIDE_KMSG | ZBL_BOOT_OVERRIDE_LOGIN);
	check(error == 0, "O3 both return 0");
	same = record_is(&record, GRAPHICAL_KEPT " kmsg=console login=console");
	check(same, "O3 both record");
	same = record_tail_clear(&record);
	check(same, "O3 both leave no stale text");

	/* O6: applying both again changes nothing. */
	memcpy(&again, &record, sizeof(again));
	error = zbl_boot_override_apply(&again, ZBL_BOOT_OVERRIDE_KMSG | ZBL_BOOT_OVERRIDE_LOGIN);
	check(error == 0, "O6 second apply returns 0");
	different = memcmp(&again, &record, sizeof(again));
	check(different == 0, "O6 second apply gives the same record");

	/* O4: a text record only gains the two tokens. */
	record_set(&record, TEXT_RECORD);
	error = zbl_boot_override_apply(&record, ZBL_BOOT_OVERRIDE_KMSG | ZBL_BOOT_OVERRIDE_LOGIN);
	check(error == 0, "O4 text returns 0");
	same = record_is(&record, TEXT_RECORD " kmsg=console login=console");
	check(same, "O4 text record");
}

/* O5: no key, or only unknown bits, leaves the record byte for byte. */
static void
test_unchanged(void)
{
	struct kern_boot_parameter_record record;
	struct kern_boot_parameter_record original;
	int different;
	int error;

	/* No key held. */
	record_set(&original, GRAPHICAL_RECORD);
	original.reserved = 0x5a5a5a5aU;
	original.text[sizeof(original.text) - 1U] = 'z';
	memcpy(&record, &original, sizeof(record));
	error = zbl_boot_override_apply(&record, 0U);
	check(error == 0, "O5 no key returns 0");
	different = memcmp(&record, &original, sizeof(record));
	check(different == 0, "O5 no key leaves the record");

	/* Only bits the loader does not know. */
	memcpy(&record, &original, sizeof(record));
	error = zbl_boot_override_apply(&record, 0xf0U);
	check(error == 0, "O5 unknown bits return 0");
	different = memcmp(&record, &original, sizeof(record));
	check(different == 0, "O5 unknown bits leave the record");
}

/* O7: only whole names are dropped, never a value that holds kmsg=. */
static void
test_names(void)
{
	struct kern_boot_parameter_record record;
	int error;
	int same;

	/* Names that start like login, and a value that holds kmsg=quiet. */
	record_set(&record, "display=edp login=graphical logout=x login2=y rootpart=PARTLABEL=kmsg=quiet");
	error = zbl_boot_override_apply(&record, ZBL_BOOT_OVERRIDE_KMSG | ZBL_BOOT_OVERRIDE_LOGIN);
	check(error == 0, "O7 returns 0");
	same = record_is(&record, "display=edp logout=x login2=y rootpart=PARTLABEL=kmsg=quiet kmsg=console login=console");
	check(same, "O7 record");

	/* A token with no '=' is kept as it is. */
	record_set(&record, "single logo=x");
	error = zbl_boot_override_apply(&record, ZBL_BOOT_OVERRIDE_KMSG);
	check(error == 0, "O7 bare token returns 0");
	same = record_is(&record, "single kmsg=console");
	check(same, "O7 bare token kept");
}

/* O8: a replacement that would pass 3071 bytes is left out. */
static void
test_limit(void)
{
	static char text[KERN_BOOT_PARAMETERS_STORAGE_SIZE];
	static char expected[KERN_BOOT_PARAMETERS_STORAGE_SIZE + 16U];
	struct kern_boot_parameter_record record;
	size_t filler;
	int error;
	int same;

	/*
	 * A record 3071 bytes long: "a=" and 3058 x, then " kmsg=quiet".
	 * Dropping kmsg=quiet leaves 3060 bytes, and " kmsg=console" would
	 * make 3073, which does not fit.
	 */
	filler = 3058U;
	memset(text, 0, sizeof(text));
	text[0] = 'a';
	text[1] = '=';
	memset(&text[2], 'x', filler);
	strcpy(&text[2U + filler], " kmsg=quiet");
	check(strlen(text) == 3071U, "O8 first record is 3071 bytes");

	/* Ctrl on the full record. */
	record_set(&record, text);
	error = zbl_boot_override_apply(&record, ZBL_BOOT_OVERRIDE_KMSG);
	check(error == 0, "O8 full record returns 0");

	/* Only the filler is left. */
	text[2U + filler] = '\0';
	same = record_is(&record, text);
	check(same, "O8 full record leaves kmsg= out");
	check(record.length == 3060U, "O8 full record length 3060");
	same = record_tail_clear(&record);
	check(same, "O8 full record leaves no stale text");

	/* A record 3069 bytes long: the replacement makes exactly 3071. */
	filler = 3056U;
	memset(text, 0, sizeof(text));
	text[0] = 'a';
	text[1] = '=';
	memset(&text[2], 'x', filler);
	strcpy(&text[2U + filler], " kmsg=quiet");
	check(strlen(text) == 3069U, "O8 second record is 3069 bytes");

	/* Ctrl on it. */
	record_set(&record, text);
	error = zbl_boot_override_apply(&record, ZBL_BOOT_OVERRIDE_KMSG);
	check(error == 0, "O8 second record returns 0");

	/* The filler and kmsg=console. */
	memset(expected, 0, sizeof(expected));
	memcpy(expected, text, 2U + filler);
	strcpy(&expected[2U + filler], " kmsg=console");
	same = record_is(&record, expected);
	check(same, "O8 second record takes kmsg=console");
	check(record.length == 3071U, "O8 second record length 3071");
}

/* O9: a record with a bad length or no terminator is refused untouched. */
static void
test_refusals(void)
{
	struct kern_boot_parameter_record record;
	struct kern_boot_parameter_record original;
	int different;
	int error;

	/* A length past the kernel's limit. */
	record_set(&original, GRAPHICAL_RECORD);
	original.length = 3072U;
	memcpy(&record, &original, sizeof(record));
	error = zbl_boot_override_apply(&record, ZBL_BOOT_OVERRIDE_KMSG);
	check(error == -1, "O9 long length returns -1");
	different = memcmp(&record, &original, sizeof(record));
	check(different == 0, "O9 long length leaves the record");

	/* A text that does not end at its length. */
	record_set(&original, GRAPHICAL_RECORD);
	original.length = 5U;
	memcpy(&record, &original, sizeof(record));
	error = zbl_boot_override_apply(&record, ZBL_BOOT_OVERRIDE_LOGIN);
	check(error == -1, "O9 unterminated returns -1");
	different = memcmp(&record, &original, sizeof(record));
	check(different == 0, "O9 unterminated leaves the record");

	/* A malformed record is refused even when no key is held. */
	memcpy(&record, &original, sizeof(record));
	error = zbl_boot_override_apply(&record, 0U);
	check(error == -1, "O9 unterminated without keys returns -1");
}

/* O10: dropping the first or the last token leaves no stray space. */
static void
test_ends(void)
{
	struct kern_boot_parameter_record record;
	int error;
	int same;

	/* The only token is dropped. */
	record_set(&record, "kmsg=quiet");
	error = zbl_boot_override_apply(&record, ZBL_BOOT_OVERRIDE_KMSG);
	check(error == 0, "O10 only token returns 0");
	same = record_is(&record, "kmsg=console");
	check(same, "O10 only token record");

	/* The first token is dropped. */
	record_set(&record, "login=graphical a=1");
	error = zbl_boot_override_apply(&record, ZBL_BOOT_OVERRIDE_LOGIN);
	check(error == 0, "O10 first token returns 0");
	same = record_is(&record, "a=1 login=console");
	check(same, "O10 first token record");

	/* The last token is dropped. */
	record_set(&record, "a=1 logo=x");
	error = zbl_boot_override_apply(&record, ZBL_BOOT_OVERRIDE_KMSG);
	check(error == 0, "O10 last token returns 0");
	same = record_is(&record, "a=1 kmsg=console");
	check(same, "O10 last token record");

	/* An empty record only gains the tokens. */
	record_set(&record, "");
	error = zbl_boot_override_apply(&record, ZBL_BOOT_OVERRIDE_KMSG | ZBL_BOOT_OVERRIDE_LOGIN);
	check(error == 0, "O10 empty returns 0");
	same = record_is(&record, "kmsg=console login=console");
	check(same, "O10 empty record");
}
