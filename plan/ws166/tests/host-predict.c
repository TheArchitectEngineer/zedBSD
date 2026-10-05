/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws166-p002: the host test of the predictions (userland/desktop/ime/
 * ja-predict.c): a dictionary made for the test, a user dictionary taught
 * in the test, and the words offered for readings, in order.
 *
 *     host-predict DICTIONARY WORKDIR [SYSTEM-DICTIONARY]
 *
 * Prints one line a case and host-predict: PASS or host-predict: FAIL.
 * With the image's Japanese dictionary named, the time of a thousand
 * predictions is printed too.
 */

#include "ja.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The longest list of words a case expects, joined by spaces. */
#define TEST_LIST_MAX		1024U

int main(int argc, char **argv);
static int expect(const char *name, const struct ja_user *user, const struct ja_predict_index *index, const char *reading, size_t max, const char *expected);
static int expect_keyboard(const char *name, const struct ja_user *user, const struct ja_predict_index *index, const char *reading, const char *expected);

/* Whether expect asks for the on-screen keyboard's order (ja_predict_keyboard, ws166-p002). */
static int test_keyboard;
static void timing(const char *path);

/*
 * Runs the cases.
 */
int
main(
	int argc,
	char **argv)
{
	struct ja_dict dict;
	struct ja_predict_index index;
	struct ja_user user;
	char path[1024];
	int failed;
	int error;

	/* The dictionary and the working directory. */
	if (argc < 3) {
		fprintf(stderr, "usage: host-predict DICTIONARY WORKDIR [SYSTEM-DICTIONARY]\n");
		return 2;
	}

	/* The test dictionary and its index. */
	error = ja_dict_load(&dict, argv[1], JA_DICT_SIZE_MAX);
	if (error != 0) {
		printf("host-predict: FAIL (dictionary %d)\n", error);
		return 1;
	}

	/* Its index. */
	error = ja_predict_index(&dict, &index);
	if (error != 0) {
		printf("host-predict: FAIL (index %d)\n", error);
		return 1;
	}

	/* The dictionary alone. */
	failed = 0;
	failed += !expect("longer readings first, the first candidates first", NULL, &index, "\xe3\x81\x8b\xe3\x82\x93", 9,
			  "\xe6\xbc\xa2\xe5\xad\x97 \xe9\x96\xa2\xe4\xbf\x82 \xe6\x82\xa3\xe8\x80\x85 \xe6\x84\x9f\xe6\x83\x85 \xe5\xb9\xb9\xe4\xba\x8b \xe6\x84\x9f\xe3\x81\x98 \xe5\x8b\x98\xe5\xae\x9a \xe7\xbc\xb6 \xe6\x84\x9f");
	failed += !expect("the reading itself last", NULL, &index, "\xe3\x81\x8b\xe3\x82\x93\xe3\x81\x98", 9,
			  "\xe6\x82\xa3\xe8\x80\x85 \xe6\x84\x9f\xe6\x83\x85 \xe5\x8b\x98\xe5\xae\x9a \xe6\xbc\xa2\xe5\xad\x97 \xe5\xb9\xb9\xe4\xba\x8b \xe6\x84\x9f\xe3\x81\x98");
	failed += !expect("no okurigana, nine at most", NULL, &index, "\xe3\x81\x8b", 9,
			  "\xe7\xbc\xb6 \xe6\xbc\xa2\xe5\xad\x97 \xe9\x96\xa2\xe4\xbf\x82 \xe6\x82\xa3\xe8\x80\x85 \xe6\x84\x9f\xe6\x83\x85 \xe6\x84\x9f \xe5\xb9\xb9\xe4\xba\x8b \xe6\x84\x9f\xe3\x81\x98 \xe5\x8b\x98\xe5\xae\x9a");
	failed += !expect("the limit", NULL, &index, "\xe3\x81\x8b\xe3\x82\x93", 3,
			  "\xe6\xbc\xa2\xe5\xad\x97 \xe9\x96\xa2\xe4\xbf\x82 \xe6\x82\xa3\xe8\x80\x85");
	failed += !expect("no reading starts with it", NULL, &index, "\xe3\x81\xac", 9, "");
	/* The on-screen keyboard's order: the reading's own words first. */
	failed += !expect_keyboard("keyboard: the reading itself first", NULL, &index, "\xe3\x81\x8b\xe3\x82\x93\xe3\x81\x98",
				   "\xe6\xbc\xa2\xe5\xad\x97 \xe5\xb9\xb9\xe4\xba\x8b \xe6\x84\x9f\xe3\x81\x98 \xe6\x82\xa3\xe8\x80\x85 \xe6\x84\x9f\xe6\x83\x85 \xe5\x8b\x98\xe5\xae\x9a");

	/* A user dictionary taught 勘定 for かんじょう, 感 for かん, and 書 for かk. */
	snprintf(path, sizeof(path), "%s/user-predict", argv[2]);
	(void)remove(path);
	error = ja_user_open(&user, path);
	if (error != 0) {
		printf("host-predict: FAIL (user %d)\n", error);
		return 1;
	}

	/* The three choices. */
	(void)ja_user_learn(&user, "\xe3\x81\x8b\xe3\x82\x93\xe3\x81\x98\xe3\x82\x87\xe3\x81\x86", 15, "\xe5\x8b\x98\xe5\xae\x9a");
	(void)ja_user_learn(&user, "\xe3\x81\x8b\xe3\x82\x93", 6, "\xe6\x84\x9f");
	(void)ja_user_learn(&user, "\xe3\x81\x8bk", 4, "\xe6\x9b\xb8");
	failed += !expect("the user's longer words first, the user's exact before the dictionary's", &user, &index, "\xe3\x81\x8b\xe3\x82\x93", 9,
			  "\xe5\x8b\x98\xe5\xae\x9a \xe6\xbc\xa2\xe5\xad\x97 \xe9\x96\xa2\xe4\xbf\x82 \xe6\x82\xa3\xe8\x80\x85 \xe6\x84\x9f\xe6\x83\x85 \xe5\xb9\xb9\xe4\xba\x8b \xe6\x84\x9f\xe3\x81\x98 \xe6\x84\x9f \xe7\xbc\xb6");
	failed += !expect("the user's okurigana are not predicted, the latest choice first", &user, &index, "\xe3\x81\x8b", 2,
			  "\xe6\x84\x9f \xe5\x8b\x98\xe5\xae\x9a");
	failed += !expect_keyboard("keyboard: the user's word of the reading first, then the dictionary's, then the longer", &user, &index, "\xe3\x81\x8b\xe3\x82\x93",
				   "\xe6\x84\x9f \xe7\xbc\xb6 \xe5\x8b\x98\xe5\xae\x9a \xe6\xbc\xa2\xe5\xad\x97 \xe9\x96\xa2\xe4\xbf\x82 \xe6\x82\xa3\xe8\x80\x85 \xe6\x84\x9f\xe6\x83\x85 \xe5\xb9\xb9\xe4\xba\x8b \xe6\x84\x9f\xe3\x81\x98");
	ja_user_free(&user);

	/* The image's dictionary, when named: the time. */
	if (argc >= 4)
		timing(argv[3]);

	/* The index and the dictionary go. */
	ja_predict_index_free(&index);
	ja_dict_free(&dict);

	/* The verdict. */
	if (failed != 0) {
		printf("host-predict: FAIL (%d)\n", failed);
		return 1;
	}

	/* Succeeded: every case. */
	printf("host-predict: PASS\n");
	return 0;
}

/*
 * Predicts for a reading and compares the words, joined by spaces, with
 * what is expected.  Returns 1 when they agree.
 */
static int
expect(
	const char *name,
	const struct ja_user *user,
	const struct ja_predict_index *index,
	const char *reading,
	size_t max,
	const char *expected)
{
	struct ja_prediction predictions[JA_PREDICT_KEYBOARD_MAX];
	const struct ja_predict_index *indexes[1];
	char joined[TEST_LIST_MAX];
	size_t count;
	size_t i;
	size_t length;
	int same;

	/* The words, in the order asked for. */
	indexes[0] = index;
	if (test_keyboard) {
		count = ja_predict_keyboard(user, indexes, 1, reading, strlen(reading), predictions, max);
	} else {
		count = ja_predict(user, indexes, 1, reading, strlen(reading), predictions, max);
	}

	/* Joined by spaces. */
	joined[0] = '\0';
	length = 0;
	for (i = 0; i < count; i++) {
		/* A space between two. */
		if (i != 0)
			length += (size_t)snprintf(joined + length, sizeof(joined) - length, " ");

		/* The word. */
		length += (size_t)snprintf(joined + length, sizeof(joined) - length, "%s", predictions[i].text);
	}

	/* Compared. */
	same = strcmp(joined, expected);
	if (same != 0) {
		printf("%s: FAIL (\"%s\")\n", name, joined);
		return 0;
	}

	/* Succeeded: as expected. */
	printf("%s: ok\n", name);
	return 1;
}

/*
 * Predicts in the on-screen keyboard's order (the reading's own words
 * first, JA_PREDICT_KEYBOARD_MAX at most) and compares as expect does.
 * Returns 1 when they agree.
 */
static int
expect_keyboard(
	const char *name,
	const struct ja_user *user,
	const struct ja_predict_index *index,
	const char *reading,
	const char *expected)
{
	int agreed;

	/* The keyboard's order, for this case alone. */
	test_keyboard = 1;
	agreed = expect(name, user, index, reading, JA_PREDICT_KEYBOARD_MAX, expected);
	test_keyboard = 0;
	return agreed;
}

/*
 * Times a thousand predictions of two-kana readings with the image's
 * dictionary, and prints the time of one.
 */
static void
timing(
	const char *path)
{
	static const char *const readings[] = {
		"\xe3\x81\x8b\xe3\x82\x93", "\xe3\x81\x97\xe3\x82\x87", "\xe3\x81\x8d\xe3\x82\x87", "\xe3\x81\x9b\xe3\x81\x84",
	};
	struct ja_prediction predictions[JA_PREDICT_MAX];
	const struct ja_predict_index *indexes[1];
	struct ja_dict first;
	struct ja_dict second;
	struct ja_predict_index index;
	struct timespec start;
	struct timespec end;
	bool split;
	long micro;
	size_t total;
	int i;
	int error;

	/* The dictionary (its two parts) and the index of the system part. */
	error = ja_dict_load_parts(&first, &second, path, JA_DICT_SIZE_MAX, &split);
	if (error != 0) {
		printf("timing: dictionary %d\n", error);
		return;
	}

	/* The index of the system part. */
	error = ja_predict_index(&second, &index);
	if (error != 0)
		return;

	/* A thousand predictions. */
	indexes[0] = &index;
	total = 0;
	clock_gettime(CLOCK_MONOTONIC, &start);
	for (i = 0; i < 1000; i++)
		total += ja_predict(NULL, indexes, 1, readings[i % 4], strlen(readings[i % 4]), predictions, JA_PREDICT_MAX);

	/* The end. */
	clock_gettime(CLOCK_MONOTONIC, &end);

	/* The time of one. */
	micro = (end.tv_sec - start.tv_sec) * 1000000L + (end.tv_nsec - start.tv_nsec) / 1000L;
	printf("timing: headwords=%zu predictions=%zu one=%ld us\n", index.count, total, micro / 1000L);
	ja_predict_index_free(&index);
	ja_dict_free(&first);
	ja_dict_free(&second);
}
