/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the DC8200's flip bookkeeping (dc8200-flip.c): the
 * taken rule, the holds of the front and the pending commit, a second
 * present of the same buffer, a destroy while held, the loss and its
 * write-back, and the ring's mismatch count.
 *
 *   flip-host-test
 */

#include <stdint.h>
#include <stdio.h>

#include "drivers/gpu/powervr/dc8200-flip.h"

static int failures;
static unsigned passes;
static struct powervr_flip flip;

static void
check(
	int condition,
	const char *what)
{
	if (condition) {
		passes++;
		return;
	}
	printf("FAIL %s\n", what);
	failures++;
}

int
main(
	void)
{
	struct powervr_flip_hold a = { 0, 0 };
	struct powervr_flip_hold b = { 0, 0 };
	struct powervr_flip_hold c = { 0, 0 };
	struct powervr_flip_hold *freed[2];
	unsigned count;
	int now;

	/* The taken rule: two blanks after the commit's sequence. */
	check(!powervr_flip_due(10, 10), "not taken at the commit's sequence");
	check(!powervr_flip_due(10, 11), "not taken one blank after");
	check(powervr_flip_due(10, 12), "taken two blanks after");
	check(powervr_flip_due(10, 40), "taken later too");
	check(!powervr_flip_due(10, 3), "not taken behind the commit");

	/* A first commit holds its buffer; the framebuffer was the front. */
	powervr_flip_init(&flip);
	powervr_flip_commit(&flip, &a, 100);
	check(a.holds == 1 && flip.committing && flip.front == NULL, "commit holds the pending buffer");
	count = powervr_flip_taken(&flip, 102, freed);
	check(count == 0 && flip.front == &a && a.holds == 1 && !flip.committing, "taken: a is the front, held once");

	/* B replaces a: a keeps its hold until b is taken. */
	powervr_flip_commit(&flip, &b, 103);
	check(a.holds == 1 && b.holds == 1, "a stays held while b is pending");
	count = powervr_flip_taken(&flip, 105, freed);
	check(count == 0 && a.holds == 0 && b.holds == 1 && flip.front == &b, "a let go once b was taken");

	/* The same buffer twice in a row: held twice, then once. */
	powervr_flip_commit(&flip, &b, 106);
	check(b.holds == 2, "b held as front and as pending");
	count = powervr_flip_taken(&flip, 108, freed);
	check(count == 0 && b.holds == 1 && flip.front == &b, "a second present of b keeps one hold");

	/* Destroying the front defers its free to the drop that follows the next taken commit. */
	now = powervr_flip_destroy(&flip, &b, 108);
	check(!now && b.destroyed, "destroying the front leaves it to the last drop");
	now = powervr_flip_destroy(&flip, &a, 108);
	check(now, "destroying an unheld buffer frees it at once");
	powervr_flip_commit(&flip, &c, 109);
	count = powervr_flip_taken(&flip, 111, freed);
	check(count == 1 && freed[0] == &b && b.holds == 0, "b freed by the taken commit after it");

	/* Lost: c's successor a stays held as unsettled, the front c is written back. */
	a.destroyed = 0;
	powervr_flip_commit(&flip, &a, 112);
	powervr_flip_lose(&flip, 112);
	check(flip.lost && flip.unsettled == &a && a.holds == 1 && !flip.committing, "a lost commit keeps a held");
	powervr_flip_commit(&flip, flip.front, 113);
	check(c.holds == 2 && flip.committing, "the write-back of c holds it again");

	/* A second loss of the write-back drops only its extra hold. */
	powervr_flip_lose(&flip, 113);
	check(c.holds == 1 && flip.unsettled == &a && a.holds == 1, "second loss drops the write-back's extra hold");
	powervr_flip_commit(&flip, flip.front, 120);

	/* Destroying the unsettled a waits for the taken write-back. */
	now = powervr_flip_destroy(&flip, &a, 121);
	check(!now, "the unsettled buffer is not freed at destroy");
	count = powervr_flip_taken(&flip, 122, freed);
	check(count == 1 && freed[0] == &a && a.holds == 0, "a freed when the write-back was taken");
	check(!flip.lost && flip.front == &c && c.holds == 1 && flip.unsettled == NULL, "output back, c the front");

	/* Back to the framebuffer: c let go. */
	powervr_flip_commit(&flip, NULL, 130);
	count = powervr_flip_taken(&flip, 132, freed);
	check(count == 0 && c.holds == 0 && flip.front == NULL, "the framebuffer back, c unheld");
	check(flip.mismatches == 0, "no event out of order so far");

	/* An event out of order is counted: taken too early, a second commit while one pends. */
	powervr_flip_commit(&flip, &c, 140);
	powervr_flip_commit(&flip, &c, 140);
	(void)powervr_flip_taken(&flip, 141, freed);
	check(flip.mismatches == 2, "an early taken and a doubled commit are mismatches");
	check(flip.recorded > 0 && flip.ring[(flip.next + POWERVR_FLIP_RING - 1U) % POWERVR_FLIP_RING].event != 0, "the ring records");

	printf("%u passes %d failures\n", passes, failures);
	return failures != 0;
}
