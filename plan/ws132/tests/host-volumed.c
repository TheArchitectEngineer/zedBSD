/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws132-p004: host test of volumed's parts without system calls
 * (userland/base/volumed/names.c): the folder names, the escaped words, a
 * client's lines, the permission rule and the VOLUME line.
 */

#include "userland/base/volumed/volumed.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static unsigned failures;

static void check(int condition, const char *what);
static void name_is(const char *label, const char *id, const char *expected);

int
main(
	void)
{
	struct volumed_volume volume;
	char text[256];
	char id[VOLUMED_NAME_MAX];
	unsigned request;
	int ask;
	int error;

	/* Folder names. */
	name_is("USB STICK", "da0s1", "USB STICK");
	name_is("", "da0s1", "da0s1");
	name_is(NULL, "da0", "da0");
	name_is("   ", "da0s1", "da0s1");
	name_is("  PHOTOS  ", "da0s1", "PHOTOS");
	name_is("a/b", "da0", "a_b");
	name_is("..", "da0", "_.");
	name_is(".hidden", "da0", "_hidden");
	name_is("tab\there", "da0", "tab_here");
	name_is("\xe5\x86\x99\xe7\x9c\x9f", "da0", "\xe5\x86\x99\xe7\x9c\x9f");

	/* Escaped words. */
	error = volumed_escape("USB STICK", text, sizeof(text));
	check(error == 0 && strcmp(text, "USB%20STICK") == 0, "a space is escaped");
	error = volumed_escape("a=b%c", text, sizeof(text));
	check(error == 0 && strcmp(text, "a%3db%25c") == 0, "= and % are escaped");
	error = volumed_escape("/media/x-y_z.1", text, sizeof(text));
	check(error == 0 && strcmp(text, "/media/x-y_z.1") == 0, "a path stays as it is");
	error = volumed_escape("abcdef", text, 4U);
	check(error == ENAMETOOLONG && text[0] == '\0', "a word that does not fit is refused, empty");

	/* A client's lines. */
	error = volumed_parse("HELLO 1", &ask, &request, id, sizeof(id));
	check(error == 0 && ask == VOLUMED_ASK_HELLO, "HELLO 1");
	error = volumed_parse("MOUNT 7 da0s1", &ask, &request, id, sizeof(id));
	check(error == 0 && ask == VOLUMED_ASK_MOUNT && request == 7U && strcmp(id, "da0s1") == 0, "MOUNT 7 da0s1");
	error = volumed_parse("EJECT 4294967295 da1", &ask, &request, id, sizeof(id));
	check(error == 0 && ask == VOLUMED_ASK_EJECT && request == 4294967295U && strcmp(id, "da1") == 0, "EJECT with the largest request");
	error = volumed_parse("EJECT 4294967296 da1", &ask, &request, id, sizeof(id));
	check(error == EINVAL, "a request number too large is refused");
	error = volumed_parse("FORMAT 1 da0", &ask, &request, id, sizeof(id));
	check(error == EINVAL, "an unknown word is refused");
	error = volumed_parse("MOUNT 1 da0 extra", &ask, &request, id, sizeof(id));
	check(error == EINVAL, "words after the volume are refused");
	error = volumed_parse("MOUNT 1", &ask, &request, id, sizeof(id));
	check(error == EINVAL, "a request without a volume is refused");
	error = volumed_parse("HELLO 2", &ask, &request, id, sizeof(id));
	check(error == EINVAL, "another protocol version is refused");

	/* Who may mount and eject. */
	check(volumed_permitted(0, 1000, 78) == 1, "root may");
	check(volumed_permitted(1000, 1000, 78) == 1, "the seat's user may");
	check(volumed_permitted(1001, 1000, 78) == 0, "another user may not");
	check(volumed_permitted(78, 78, 78) == 0, "the login screen may not");
	check(volumed_permitted(1000, 0, 78) == 0, "a user without the seat (root's) may not");

	/* The VOLUME line. */
	memset(&volume, 0, sizeof(volume));
	snprintf(volume.id, sizeof(volume.id), "da0s1");
	snprintf(volume.fs, sizeof(volume.fs), "fat");
	snprintf(volume.label, sizeof(volume.label), "MY STICK");
	volume.bytes = 1048576U;
	volume.fresh = 1U;
	error = volumed_format_volume(&volume, text, sizeof(text));
	check(error == 0 && strcmp(text, "VOLUME id=da0s1 state=available fs=fat size=1048576 label=MY%20STICK path=- new=1") == 0, "an available volume's line");
	snprintf(volume.path, sizeof(volume.path), "/media/MY STICK");
	volume.fresh = 0U;
	error = volumed_format_volume(&volume, text, sizeof(text));
	check(error == 0 && strcmp(text, "VOLUME id=da0s1 state=mounted fs=fat size=1048576 label=MY%20STICK path=/media/MY%20STICK new=0") == 0, "a mounted volume's line");

	/* The verdict. */
	if (failures != 0U) {
		printf("host-volumed: FAIL (%u)\n", failures);
		return 1;
	}
	printf("host-volumed: PASS\n");
	return 0;
}

/* Checks one folder name. */
static void
name_is(
	const char *label,
	const char *id,
	const char *expected)
{
	char name[VOLUMED_LABEL_MAX];
	char what[160];
	int error;

	/* The name, and the check. */
	error = volumed_mount_name(label, id, name, sizeof(name));
	snprintf(what, sizeof(what), "name of [%s] on %s is [%s] (got [%s])", label != NULL ? label : "(none)", id, expected, name);
	check(error == 0 && strcmp(name, expected) == 0, what);
}

/* Counts a failed check and names it. */
static void
check(
	int condition,
	const char *what)
{
	/* Each check is reported. */
	if (condition) {
		printf("ok: %s\n", what);
	} else {
		printf("FAIL: %s\n", what);
		failures++;
	}
}
