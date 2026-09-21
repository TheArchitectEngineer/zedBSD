/* WS032: sys/queue.h, on the target.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib */
#include <sys/queue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(const char *what, int ok)
{
	printf("QUE %-4s %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

struct item {
	int value;
	SLIST_ENTRY(item) slist;
	LIST_ENTRY(item) list;
	SIMPLEQ_ENTRY(item) simpleq;
	TAILQ_ENTRY(item) tailq;
};

SLIST_HEAD(slist_head, item);
LIST_HEAD(list_head, item);
SIMPLEQ_HEAD(simpleq_head, item);
TAILQ_HEAD(tailq_head, item);

static struct item items[8];

/* Reads the values out in order, as one string. */
#define ORDER(kind, head, field, out)					\
do {									\
	struct item *_walk;						\
	char *_at = (out);						\
	*_at = '\0';							\
	kind##_FOREACH(_walk, head, field) {				\
		*_at++ = (char)('0' + _walk->value);			\
		*_at = '\0';						\
	}								\
} while (0)

int main(void)
{
	struct slist_head sl = SLIST_HEAD_INITIALIZER(sl);
	struct list_head ll;
	struct simpleq_head sq;
	struct tailq_head tq, tq2;
	struct item *walk, *next;
	char order[16];
	int i, count;

	for (i = 0; i < 8; i++)
		items[i].value = i;

	/* Singly-linked list. */
	SLIST_INIT(&sl);
	check("SLIST starts empty", SLIST_EMPTY(&sl));
	SLIST_INSERT_HEAD(&sl, &items[2], slist);
	SLIST_INSERT_HEAD(&sl, &items[1], slist);
	SLIST_INSERT_AFTER(&items[2], &items[3], slist);
	ORDER(SLIST, &sl, slist, order);
	check("SLIST inserts at the head and after", strcmp(order, "123") == 0);
	SLIST_REMOVE(&sl, &items[2], item, slist);
	ORDER(SLIST, &sl, slist, order);
	check("SLIST removes from the middle", strcmp(order, "13") == 0);
	SLIST_REMOVE_HEAD(&sl, slist);
	ORDER(SLIST, &sl, slist, order);
	check("SLIST removes the head", strcmp(order, "3") == 0);
	count = 0;
	SLIST_FOREACH_SAFE(walk, &sl, slist, next)
		count++;
	check("SLIST walks safely", count == 1);

	/* Doubly-linked list: removal must not need the head. */
	LIST_INIT(&ll);
	LIST_INSERT_HEAD(&ll, &items[2], list);
	LIST_INSERT_BEFORE(&items[2], &items[1], list);
	LIST_INSERT_AFTER(&items[2], &items[3], list);
	ORDER(LIST, &ll, list, order);
	check("LIST inserts before, after and at the head",
	      strcmp(order, "123") == 0);
	LIST_REMOVE(&items[1], list);
	ORDER(LIST, &ll, list, order);
	check("LIST removes the first without the head",
	      strcmp(order, "23") == 0);
	LIST_REPLACE(&items[2], &items[4], list);
	ORDER(LIST, &ll, list, order);
	check("LIST replaces in place", strcmp(order, "43") == 0);
	LIST_REMOVE(&items[3], list);
	LIST_REMOVE(&items[4], list);
	check("LIST empties", LIST_EMPTY(&ll));

	/* Simple queue: the tail pointer must survive every path. */
	SIMPLEQ_INIT(&sq);
	SIMPLEQ_INSERT_TAIL(&sq, &items[1], simpleq);
	SIMPLEQ_INSERT_TAIL(&sq, &items[2], simpleq);
	SIMPLEQ_INSERT_HEAD(&sq, &items[0], simpleq);
	SIMPLEQ_INSERT_AFTER(&sq, &items[2], &items[3], simpleq);
	ORDER(SIMPLEQ, &sq, simpleq, order);
	check("SIMPLEQ appends, prepends and inserts after",
	      strcmp(order, "0123") == 0);
	SIMPLEQ_REMOVE_HEAD(&sq, simpleq);
	SIMPLEQ_REMOVE_AFTER(&sq, &items[1], simpleq);
	ORDER(SIMPLEQ, &sq, simpleq, order);
	check("SIMPLEQ removes the head and after", strcmp(order, "13") == 0);

	/* Emptying and appending again proves the tail pointer was restored. */
	SIMPLEQ_REMOVE_HEAD(&sq, simpleq);
	SIMPLEQ_REMOVE_HEAD(&sq, simpleq);
	SIMPLEQ_INSERT_TAIL(&sq, &items[5], simpleq);
	ORDER(SIMPLEQ, &sq, simpleq, order);
	check("SIMPLEQ appends again after emptying", strcmp(order, "5") == 0);

	/* Tail queue, including the walk backwards. */
	TAILQ_INIT(&tq);
	TAILQ_INSERT_TAIL(&tq, &items[2], tailq);
	TAILQ_INSERT_HEAD(&tq, &items[1], tailq);
	TAILQ_INSERT_TAIL(&tq, &items[4], tailq);
	TAILQ_INSERT_BEFORE(&items[4], &items[3], tailq);
	ORDER(TAILQ, &tq, tailq, order);
	check("TAILQ inserts at both ends and before",
	      strcmp(order, "1234") == 0);
	check("TAILQ knows its last", TAILQ_LAST(&tq, tailq_head) == &items[4]);

	order[0] = '\0';
	{
		char *at = order;

		TAILQ_FOREACH_REVERSE(walk, &tq, tailq_head, tailq) {
			*at++ = (char)('0' + walk->value);
			*at = '\0';
		}
	}
	check("TAILQ walks backwards", strcmp(order, "4321") == 0);

	TAILQ_REMOVE(&tq, &items[4], tailq);
	ORDER(TAILQ, &tq, tailq, order);
	check("TAILQ removes the last", strcmp(order, "123") == 0);
	check("TAILQ knows its new last",
	      TAILQ_LAST(&tq, tailq_head) == &items[3]);
	TAILQ_INSERT_TAIL(&tq, &items[5], tailq);
	ORDER(TAILQ, &tq, tailq, order);
	check("TAILQ appends after removing the last",
	      strcmp(order, "1235") == 0);

	TAILQ_INIT(&tq2);
	TAILQ_INSERT_TAIL(&tq2, &items[6], tailq);
	TAILQ_INSERT_TAIL(&tq2, &items[7], tailq);
	TAILQ_CONCAT(&tq, &tq2, tailq);
	ORDER(TAILQ, &tq, tailq, order);
	check("TAILQ joins two queues",
	      strcmp(order, "123567") == 0 && TAILQ_EMPTY(&tq2));
	check("TAILQ walks the joined queue backwards",
	      TAILQ_LAST(&tq, tailq_head) == &items[7] &&
	      TAILQ_PREV(&items[7], tailq_head, tailq) == &items[6]);

	count = 0;
	TAILQ_FOREACH_SAFE(walk, &tq, tailq, next) {
		TAILQ_REMOVE(&tq, walk, tailq);
		count++;
	}
	check("TAILQ empties while being walked",
	      count == 6 && TAILQ_EMPTY(&tq));

	printf("QUE verdict: %s (%d failures)\n", failures ? "FAIL" : "PASS",
	       failures);
	return failures != 0;
}
