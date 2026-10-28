/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of libkeiland's catalogs read on another thread
 * (translate.c's kl_tr_read, kl_tr_use and kl_tr_discard): a read on a
 * thread while the main thread keeps looking texts up leaves the texts in
 * use as they were (TSan sees no shared write), the use makes them the
 * language's, a refused name changes nothing, a read discarded changes
 * nothing, and kl_tr_open reads as before.
 */

#include <keiland/keiland.h>

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* How many lookups the main thread makes while the thread reads. */
#define TEST_LOOKUPS	20000U

/* A read on the thread: what it reads and what it got. */
struct test_read {
	const char *language;
	int error;
	struct kl_tr_catalogs *catalogs;
};

static unsigned test_failures;

/* Records a failed check. */
static void
test_check(
	int condition,
	const char *what)
{
	/* A passed check says nothing. */
	if (condition)
		return;

	/* The failure, by name. */
	printf("FAIL %s\n", what);
	test_failures++;
}

/* Reads the catalogs on the thread. */
static void *
test_read_run(
	void *argument)
{
	struct test_read *read;

	/* The compositor's domain, into a record of its own. */
	read = argument;
	read->error = kl_tr_read("wayland", read->language, &read->catalogs);

	/* The thread ends; its record is the main thread's. */
	return NULL;
}

/* Reads a language on a thread while the main thread looks texts up. */
static void
test_read_beside(
	struct test_read *read,
	const char *language,
	const char *seen)
{
	pthread_t thread;
	unsigned index;
	int error;
	int same;

	/* The thread reads. */
	memset(read, 0, sizeof(*read));
	read->language = language;
	error = pthread_create(&thread, NULL, test_read_run, read);
	test_check(error == 0, "thread");
	if (error != 0)
		return;

	/* The main thread keeps the texts in use meanwhile. */
	for (index = 0U; index < TEST_LOOKUPS; index++) {
		same = strcmp(kl_tr("Open"), seen);
		if (same != 0) {
			test_check(0, "text changed during the read");
			break;
		}
	}

	/* The read is the main thread's. */
	(void)pthread_join(thread, NULL);
}

int
main(
	void)
{
	struct test_read read;
	int error;
	int same;

	/* English first: the source's text. */
	error = kl_tr_open("wayland", "en");
	test_check(error == 0, "open en");
	same = strcmp(kl_tr("Open"), "Open");
	test_check(same == 0, "en text");

	/* Japanese read beside the lookups, not used yet. */
	test_read_beside(&read, "ja", "Open");
	test_check(read.error == 0, "read ja");
	test_check(read.catalogs != NULL, "read ja record");
	same = strcmp(kl_tr_language(), "en");
	test_check(same == 0, "language before use");

	/* Used: the texts are Japanese, the shared catalog's too. */
	kl_tr_use(read.catalogs);
	same = strcmp(kl_tr_language(), "ja");
	test_check(same == 0, "language after use");
	same = strcmp(kl_tr("Open"), "OPEN-JA");
	test_check(same == 0, "ja text");
	same = strcmp(kl_tr("Shared"), "SHARED-JA");
	test_check(same == 0, "ja shared text");

	/* A refused name: no record, the texts unchanged. */
	read.catalogs = (struct kl_tr_catalogs *)&read;
	error = kl_tr_read("wayland", "../ja", &read.catalogs);
	test_check(error == EINVAL, "refused name");
	test_check(read.catalogs == NULL, "refused record");
	same = strcmp(kl_tr("Open"), "OPEN-JA");
	test_check(same == 0, "text after refusal");

	/* English read and discarded: the texts unchanged. */
	test_read_beside(&read, "en", "OPEN-JA");
	test_check(read.error == 0, "read en");
	kl_tr_discard(read.catalogs);
	same = strcmp(kl_tr_language(), "ja");
	test_check(same == 0, "language after discard");

	/* Japanese read and discarded: its memory goes (ASan). */
	test_read_beside(&read, "ja", "OPEN-JA");
	test_check(read.error == 0, "read ja again");
	kl_tr_discard(read.catalogs);
	kl_tr_discard(NULL);
	kl_tr_use(NULL);

	/* English read and used. */
	test_read_beside(&read, "en", "OPEN-JA");
	kl_tr_use(read.catalogs);
	same = strcmp(kl_tr("Open"), "Open");
	test_check(same == 0, "en text after use");

	/* kl_tr_open as before. */
	error = kl_tr_open("wayland", "ja");
	test_check(error == 0, "open ja");
	same = strcmp(kl_tr("Open"), "OPEN-JA");
	test_check(same == 0, "open ja text");
	kl_tr_close();

	/* The summary the script reads. */
	if (test_failures != 0U) {
		printf("host-tr-read: FAIL %u\n", test_failures);
		return 1;
	}

	/* Succeeded: every check passed. */
	printf("host-tr-read: PASS\n");
	return 0;
}
