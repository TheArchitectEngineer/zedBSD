/* WS032: sys/tree.h, on the target.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib */
#include <sys/tree.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT 512

static int failures;

static void check(const char *what, int ok)
{
	printf("TRE %-4s %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

struct node {
	int key;
	RB_ENTRY(node) rb;
	SPLAY_ENTRY(node) sp;
};

RB_HEAD(rbtree, node);
SPLAY_HEAD(sptree, node);

static int node_cmp(struct node *a, struct node *b)
{
	return a->key < b->key ? -1 : (a->key > b->key ? 1 : 0);
}

RB_PROTOTYPE(rbtree, node, rb, node_cmp)
RB_GENERATE(rbtree, node, rb, node_cmp)
SPLAY_PROTOTYPE(sptree, node, sp, node_cmp)
SPLAY_GENERATE(sptree, node, sp, node_cmp)

static struct node nodes[COUNT];
static int order[COUNT];
static int removal[COUNT];

/* A deterministic shuffle, so a failure can be reproduced. */
static unsigned seed = 12345U;

static unsigned next_random(void)
{
	seed = seed * 1103515245U + 12345U;
	return (seed >> 16) & 0x7fffU;
}

static void shuffle(int *values)
{
	int i, j, t;

	for (i = 0; i < COUNT; i++)
		values[i] = i;
	for (i = COUNT - 1; i > 0; i--) {
		j = (int)(next_random() % (unsigned)(i + 1));
		t = values[i];
		values[i] = values[j];
		values[j] = t;
	}
}

/*
 * Counts the black records on every path below one, and reports -1 when
 * the tree is not a red-black tree: a red record under a red parent, two
 * paths of different black height, or a broken parent link.
 */
static int black_height(struct node *n, struct node *parent)
{
	int left, right;

	if (n == NULL)
		return 1;
	if (RB_PARENT(n, rb) != parent)
		return -1;
	if (RB_COLOR(n, rb) == RB_RED) {
		if (parent != NULL && RB_COLOR(parent, rb) == RB_RED)
			return -1;
	}
	left = black_height(RB_LEFT(n, rb), n);
	right = black_height(RB_RIGHT(n, rb), n);
	if (left < 0 || right < 0 || left != right)
		return -1;
	return left + (RB_COLOR(n, rb) == RB_BLACK ? 1 : 0);
}

static int valid(struct rbtree *head)
{
	if (RB_ROOT(head) != NULL &&
	    (RB_COLOR(RB_ROOT(head), rb) != RB_BLACK ||
	     RB_PARENT(RB_ROOT(head), rb) != NULL))
		return 0;
	return black_height(RB_ROOT(head), NULL) > 0;
}

/* Walks in order and reports whether the keys came out sorted. */
static int sorted(struct rbtree *head, int expected)
{
	struct node *walk;
	int last = -1;
	int seen = 0;

	RB_FOREACH(walk, rbtree, head) {
		if (walk->key <= last)
			return 0;
		last = walk->key;
		seen++;
	}
	return seen == expected;
}

int main(void)
{
	struct rbtree rb = RB_INITIALIZER(&rb);
	struct sptree sp;
	struct node key, *found, *walk, *next;
	int i, broken, count;

	RB_INIT(&rb);
	SPLAY_INIT(&sp);
	for (i = 0; i < COUNT; i++)
		nodes[i].key = i;
	shuffle(order);
	shuffle(removal);

	check("a new tree is empty", RB_EMPTY(&rb) && valid(&rb));

	/* Insertion must leave a red-black tree after every single step. */
	broken = 0;
	for (i = 0; i < COUNT; i++) {
		if (RB_INSERT(rbtree, &rb, &nodes[order[i]]) != NULL)
			broken = 1;
		if (!valid(&rb))
			broken = 1;
		if (broken) {
			printf("TRE info broke inserting %d (step %d)\n",
			       order[i], i);
			break;
		}
	}
	check("the rules hold after every insertion", !broken);
	check("every key is present and in order", sorted(&rb, COUNT));

	check("a duplicate key reports the record already there",
	      RB_INSERT(rbtree, &rb, &nodes[order[0]]) == &nodes[order[0]]);

	key.key = 200;
	check("find reaches a key", (found = RB_FIND(rbtree, &rb, &key)) != NULL &&
	      found->key == 200);
	check("min and max are the ends",
	      RB_MIN(rbtree, &rb)->key == 0 &&
	      RB_MAX(rbtree, &rb)->key == COUNT - 1);
	check("next and prev step by one",
	      RB_NEXT(rbtree, &rb, found)->key == 201 &&
	      RB_PREV(rbtree, &rb, found)->key == 199 &&
	      RB_PREV(rbtree, &rb, RB_MIN(rbtree, &rb)) == NULL &&
	      RB_NEXT(rbtree, &rb, RB_MAX(rbtree, &rb)) == NULL);

	/* Removal must also leave a red-black tree after every step. */
	broken = 0;
	for (i = 0; i < COUNT; i++) {
		if (RB_REMOVE(rbtree, &rb, &nodes[removal[i]]) !=
		    &nodes[removal[i]])
			broken = 1;
		if (!valid(&rb))
			broken = 1;
		if (!sorted(&rb, COUNT - i - 1))
			broken = 1;
		if (broken) {
			printf("TRE info broke removing %d (step %d)\n",
			       removal[i], i);
			break;
		}
	}
	check("the rules hold after every removal", !broken);
	check("the tree is empty again", RB_EMPTY(&rb));

	/* NFIND finds the first key not less than the one asked for. */
	RB_INIT(&rb);
	for (i = 0; i < COUNT; i += 2)
		(void)RB_INSERT(rbtree, &rb, &nodes[i]);
	key.key = 101;
	found = RB_NFIND(rbtree, &rb, &key);
	check("nfind steps up to the next key present",
	      found != NULL && found->key == 102);
	key.key = 102;
	found = RB_NFIND(rbtree, &rb, &key);
	check("nfind returns an exact match", found != NULL && found->key == 102);
	key.key = COUNT;
	check("nfind reports nothing past the end",
	      RB_NFIND(rbtree, &rb, &key) == NULL);

	/* The safe walk may take records out as it goes. */
	count = 0;
	RB_FOREACH_SAFE(walk, rbtree, &rb, next) {
		(void)RB_REMOVE(rbtree, &rb, walk);
		count++;
	}
	check("a safe walk can empty the tree",
	      count == COUNT / 2 && RB_EMPTY(&rb));

	/* Splay tree: the same keys, and the same order out. */
	for (i = 0; i < COUNT; i++)
		(void)SPLAY_INSERT(sptree, &sp, &nodes[order[i]]);
	key.key = 300;
	check("splay finds a key",
	      (found = SPLAY_FIND(sptree, &sp, &key)) != NULL &&
	      found->key == 300);
	check("splay moves what was found to the root",
	      SPLAY_ROOT(&sp) == found);
	check("splay reports the ends",
	      SPLAY_MIN(sptree, &sp)->key == 0 &&
	      SPLAY_MAX(sptree, &sp)->key == COUNT - 1);

	count = 0;
	broken = 0;
	{
		int last = -1;

		SPLAY_FOREACH(walk, sptree, &sp) {
			if (walk->key <= last)
				broken = 1;
			last = walk->key;
			count++;
		}
	}
	check("splay walks in order", !broken && count == COUNT);

	for (i = 0; i < COUNT; i++) {
		if (SPLAY_REMOVE(sptree, &sp, &nodes[removal[i]]) !=
		    &nodes[removal[i]]) {
			broken = 1;
			break;
		}
	}
	check("splay removes every key", !broken && SPLAY_EMPTY(&sp));

	printf("TRE verdict: %s (%d failures)\n", failures ? "FAIL" : "PASS",
	       failures);
	return failures != 0;
}
