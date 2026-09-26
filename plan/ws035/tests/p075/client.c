/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The test client of ws035-p075, built with zedBSD's libwayland-client.
 *
 * It knows zed_generic_test_v1 only through the code wayland-scanner makes,
 * so libwayland has no typed table for it and every event goes through the
 * generic dispatch.  Two rounds: each checks every argument of values, the
 * server-created child (its listener, a ping answered by a pong, then its
 * destruction), the references to it and a null one, and the twelve
 * arguments of many.  Prints CLIENT DONE failures=N.
 *
 *   client SOCKET-NAME     (in XDG_RUNTIME_DIR)
 */

#include <wayland-client.h>

#include "generic-test-client-protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The rounds the client asks for. */
#define CLIENT_ROUNDS		2U

/*
 * What the client has seen: the globals, the current round's child and
 * whether its pong came, the rounds done, and the failed checks.
 */
struct client_state {
	struct zed_generic_test_v1 *test;
	struct zed_generic_child_v1 *child;
	uint32_t child_tag;
	uint32_t pong;
	unsigned references;
	unsigned null_references;
	unsigned rounds_done;
	unsigned failures;
};

static void check(struct client_state *state, int passed, const char *what);
static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name);
static void test_values(void *data, struct zed_generic_test_v1 *test, int32_t i, uint32_t u, wl_fixed_t f, const char *s, const char *n, struct wl_array *a, int32_t h);
static void test_child(void *data, struct zed_generic_test_v1 *test, struct zed_generic_child_v1 *child, uint32_t tag);
static void test_reference(void *data, struct zed_generic_test_v1 *test, struct zed_generic_child_v1 *child);
static void test_many(void *data, struct zed_generic_test_v1 *test, int32_t a0, uint32_t a1, int32_t a2, uint32_t a3, int32_t a4, uint32_t a5, int32_t a6, wl_fixed_t a7, const char *a8, int32_t a9, uint32_t a10, int32_t a11);
static void test_round_done(void *data, struct zed_generic_test_v1 *test, uint32_t round);
static void child_pong(void *data, struct zed_generic_child_v1 *child, uint32_t value);

/* The registry's callbacks (typed: wl_registry is one of libwayland's own). */
static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_global_remove
};

/* The test global's callbacks (generic: libwayland has no table for them). */
static const struct zed_generic_test_v1_listener test_listener = {
	test_values,
	test_child,
	test_reference,
	test_many,
	test_round_done
};

/* A child's callback (generic too). */
static const struct zed_generic_child_v1_listener child_listener = {
	child_pong
};

/*
 * Runs the rounds and reports the failed checks.
 */
int
main(
	int argc,
	char **argv)
{
	struct client_state state;
	struct wl_display *display;
	struct wl_registry *registry;
	unsigned round;
	int status;

	/* The socket name is the only argument. */
	if (argc != 2) {
		fprintf(stderr, "usage: client SOCKET-NAME\n");
		return 2;
	}

	/* The connection and the test global. */
	memset(&state, 0, sizeof(state));
	display = wl_display_connect(argv[1]);
	if (display == NULL) {
		perror("wl_display_connect");
		return 1;
	}

	/* The registry announces the globals in one round trip. */
	registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, &state);
	status = wl_display_roundtrip(display);
	check(&state, status >= 0 && state.test != NULL, "bind");
	if (state.test == NULL)
		return 1;
	zed_generic_test_v1_add_listener(state.test, &test_listener, &state);

	/* Each round: every event, then the child's ping and pong, then the child goes. */
	for (round = 1; round <= CLIENT_ROUNDS; round++) {
		state.child = NULL;
		state.pong = 0;
		zed_generic_test_v1_emit(state.test, round);
		status = wl_display_roundtrip(display);
		check(&state, status >= 0 && state.rounds_done == round, "round events");
		check(&state, state.child != NULL && state.child_tag == 40U + round, "child");
		if (state.child == NULL)
			break;

		/* The child is a proxy of its own: its requests and events work. */
		zed_generic_child_v1_add_listener(state.child, &child_listener, &state);
		zed_generic_child_v1_ping(state.child, 99U);
		status = wl_display_roundtrip(display);
		check(&state, status >= 0 && state.pong == 100U, "child pong");

		/* The client destroys it; the next round's child may reuse its identity. */
		zed_generic_child_v1_destroy(state.child);
		status = wl_display_roundtrip(display);
		check(&state, status >= 0, "child destroyed");
	}

	/* Two references per round, one of them null. */
	check(&state, state.references == CLIENT_ROUNDS && state.null_references == CLIENT_ROUNDS, "references");

	/* The end. */
	zed_generic_test_v1_destroy(state.test);
	wl_registry_destroy(registry);
	(void)wl_display_roundtrip(display);
	wl_display_disconnect(display);
	printf("CLIENT DONE failures=%u\n", state.failures);
	if (state.failures != 0U)
		return 1;
	return 0;
}

/* Prints one check and counts a failure. */
static void
check(
	struct client_state *state,
	int passed,
	const char *what)
{
	/* A failed check is counted and named. */
	if (!passed) {
		state->failures++;
		printf("CLIENT %s FAIL\n", what);
		fflush(stdout);
		return;
	}

	/* A passed one is named too. */
	printf("CLIENT %s ok\n", what);
	fflush(stdout);
}

/* Binds the test global. */
static void
registry_global(
	void *data,
	struct wl_registry *registry,
	uint32_t name,
	const char *interface,
	uint32_t version)
{
	struct client_state *state;
	int same;

	/* Only the test global is bound. */
	(void)version;
	state = data;
	same = strcmp(interface, zed_generic_test_v1_interface.name);
	if (same == 0)
		state->test = wl_registry_bind(registry, name, &zed_generic_test_v1_interface, 1);
}

/* No global goes in the test. */
static void
registry_global_remove(
	void *data,
	struct wl_registry *registry,
	uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

/* Checks every argument of values, and reads the fd's text. */
static void
test_values(
	void *data,
	struct zed_generic_test_v1 *test,
	int32_t i,
	uint32_t u,
	wl_fixed_t f,
	const char *s,
	const char *n,
	struct wl_array *a,
	int32_t h)
{
	struct client_state *state;
	const unsigned char *bytes;
	char text[8];
	ssize_t count;
	int same;

	/* The state the callbacks share. */
	(void)test;
	state = data;
	check(state, i == -7, "values int");
	check(state, u == 0xdeadbeefU, "values uint");
	check(state, wl_fixed_to_double(f) == -2.5, "values fixed");
	same = strcmp(s, "hello");
	check(state, same == 0, "values string");
	check(state, n == NULL, "values null string");
	bytes = a->data;
	check(state, a->size == 4 && bytes[0] == 1 && bytes[3] == 4, "values array");

	/* The fd is the listener's: it reads it and closes it. */
	memset(text, 0, sizeof(text));
	count = read(h, text, sizeof(text) - 1);
	close(h);
	same = strcmp(text, "fd-ok");
	check(state, count == 5 && same == 0, "values fd");
}

/* Keeps the server-created child. */
static void
test_child(
	void *data,
	struct zed_generic_test_v1 *test,
	struct zed_generic_child_v1 *child,
	uint32_t tag)
{
	struct client_state *state;

	/* The state the callbacks share. */
	(void)test;
	state = data;
	state->child = child;
	state->child_tag = tag;
	check(state, wl_proxy_get_id((struct wl_proxy *)child) >= 0xff000000U, "child server identity");
	printf("CLIENT child id=%u tag=%u\n", wl_proxy_get_id((struct wl_proxy *)child), tag);
}

/* Counts the references: the child's, then a null one. */
static void
test_reference(
	void *data,
	struct zed_generic_test_v1 *test,
	struct zed_generic_child_v1 *child)
{
	struct client_state *state;

	/* The state the callbacks share. */
	(void)test;
	state = data;
	if (child == NULL) {
		state->null_references++;
		return;
	}

	/* Otherwise it names the round's child. */
	check(state, child == state->child, "reference names the child");
	state->references++;
}

/* Checks twelve arguments, the last ones passed on the stack. */
static void
test_many(
	void *data,
	struct zed_generic_test_v1 *test,
	int32_t a0,
	uint32_t a1,
	int32_t a2,
	uint32_t a3,
	int32_t a4,
	uint32_t a5,
	int32_t a6,
	wl_fixed_t a7,
	const char *a8,
	int32_t a9,
	uint32_t a10,
	int32_t a11)
{
	struct client_state *state;
	int same;

	/* The state the callbacks share. */
	(void)test;
	state = data;
	check(state, a0 == -1 && a1 == 1U && a2 == -2 && a3 == 2U && a4 == -3 && a5 == 3U, "many 0-5");
	check(state, a6 == -4 && wl_fixed_to_int(a7) == -5, "many 6-7");
	same = strcmp(a8, "sixth");
	check(state, same == 0 && a9 == -100000 && a10 == 0xfffffffeU && a11 == -123456789, "many 8-11");
}

/* Counts a finished round. */
static void
test_round_done(
	void *data,
	struct zed_generic_test_v1 *test,
	uint32_t round)
{
	struct client_state *state;

	/* The state the callbacks share. */
	(void)test;
	state = data;
	state->rounds_done = round;
}

/* Keeps the child's answer. */
static void
child_pong(
	void *data,
	struct zed_generic_child_v1 *child,
	uint32_t value)
{
	struct client_state *state;

	/* The state the callbacks share. */
	(void)child;
	state = data;
	state->pong = value;
}
