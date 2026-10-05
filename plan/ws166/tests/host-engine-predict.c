/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws166-p002: the host test of the Japanese engine's predictions for the
 * on-screen keyboard, through the engine's functions as the input method
 * program calls them (engine.h: predict and learn): the list's lines
 * ("WORD\tREADING"), the reading's own words first, a choice learned that
 * comes first after, a list cut where it does not fit, and no words for a
 * reading nothing starts with.
 *
 *     host-engine-predict DICTIONARY WORKDIR
 *
 * Prints one line a case and host-engine-predict: PASS or FAIL.
 */

#include "ja.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The room of a list. */
#define TEST_LIST		3800U

int main(int argc, char **argv);
static int expect(const char *name, int agreed);

/*
 * Runs the cases.
 */
int
main(
	int argc,
	char **argv)
{
	struct ime_engine engine;
	struct ja_config config;
	char list[TEST_LIST];
	char user[1024];
	size_t length;
	int failed;
	int error;
	int same;

	/* The dictionary and the working directory. */
	if (argc < 3) {
		fprintf(stderr, "usage: host-engine-predict DICTIONARY WORKDIR\n");
		return 2;
	}

	/* The engine, with a new user dictionary. */
	snprintf(user, sizeof(user), "%s/engine-predict-user", argv[2]);
	(void)remove(user);
	config.system_dictionary = argv[1];
	config.supplement_dictionary = NULL;
	config.user_dictionary = user;
	error = ja_engine_create(&engine, &config);
	if (error != 0 || engine.ops->predict == NULL || engine.ops->learn == NULL) {
		printf("host-engine-predict: FAIL (engine %d)\n", error);
		return 1;
	}

	/* かんじ: its own words first, then the longer, one "WORD\tREADING" a line. */
	failed = 0;
	length = engine.ops->predict(&engine, "かんじ", list, sizeof(list));
	same = strcmp(list, "漢字\tかんじ\n幹事\tかんじ\n感じ\tかんじ\n患者\tかんじゃ\n感情\tかんじょう\n勘定\tかんじょう\n");
	failed += !expect("predict: the reading's words first, a line each", same == 0 && length == strlen(list));

	/* 感じ learned for かんじ comes first after. */
	engine.ops->learn(&engine, "かんじ", "感じ");
	(void)engine.ops->predict(&engine, "かんじ", list, sizeof(list));
	same = strncmp(list, "感じ\tかんじ\n漢字\tかんじ\n幹事\tかんじ\n", strlen("感じ\tかんじ\n漢字\tかんじ\n幹事\tかんじ\n"));
	failed += !expect("learn: the choice comes first", same == 0);

	/* A list cut where a line does not fit: whole lines only. */
	length = engine.ops->predict(&engine, "かんじ", list, 20);
	same = strcmp(list, "感じ\tかんじ\n");
	failed += !expect("predict: whole lines only in a small room", same == 0 && length == strlen("感じ\tかんじ\n"));

	/* Nothing starts with ぬ. */
	length = engine.ops->predict(&engine, "ぬ", list, sizeof(list));
	failed += !expect("predict: no words", length == 0 && list[0] == '\0');

	/* The engine goes (the choice is written by its thread). */
	engine.ops->destroy(&engine);

	/* The verdict. */
	if (failed != 0) {
		printf("host-engine-predict: FAIL (%d)\n", failed);
		return 1;
	}

	/* Succeeded: every case. */
	printf("host-engine-predict: PASS\n");
	return 0;
}

/* Prints a case's line.  Returns whether it agreed. */
static int
expect(
	const char *name,
	int agreed)
{
	/* A case that did not agree. */
	if (!agreed) {
		printf("%s: FAIL\n", name);
		return 0;
	}

	/* Succeeded. */
	printf("%s: ok\n", name);
	return 1;
}
