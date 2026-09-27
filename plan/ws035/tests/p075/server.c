/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The test compositor of ws035-p075, on the host's libwayland-server.
 *
 * It offers zed_generic_test_v1.  Each emit(round) is answered with every
 * test event: values (every argument type, with a pipe whose other end
 * holds "fd-ok"), a server-created child (new_id in an event), a reference
 * to it and a null reference, many (twelve arguments), and round_done.  A
 * child answers ping(value) with pong(value + 1).  The server ends when its
 * client goes, printing SERVER DONE with how many children the client
 * destroyed.
 *
 *   server SOCKET-NAME     (in XDG_RUNTIME_DIR)
 */

#include <wayland-server.h>

#include "generic-test-server-protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* How many children the client destroyed, and whether its client has gone. */
static unsigned server_children_destroyed;
static int server_client_gone;

static void child_destroy(struct wl_client *client, struct wl_resource *resource);
static void child_ping(struct wl_client *client, struct wl_resource *resource, uint32_t value);
static void child_gone(struct wl_resource *resource);
static void test_emit(struct wl_client *client, struct wl_resource *resource, uint32_t round);
static void test_destroy(struct wl_client *client, struct wl_resource *resource);
static void test_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id);
static void client_destroyed(struct wl_listener *listener, void *data);

/* A child's requests. */
static const struct zed_generic_child_v1_interface child_implementation = {
	child_destroy,
	child_ping
};

/* The test global's requests. */
static const struct zed_generic_test_v1_interface test_implementation = {
	test_emit,
	test_destroy
};

/* Tells the loop the client has gone. */
static struct wl_listener client_listener = {
	{ NULL, NULL },
	client_destroyed
};

/*
 * Runs the test compositor until its client disconnects.
 */
int
main(
	int argc,
	char **argv)
{
	struct wl_display *display;
	struct wl_event_loop *loop;
	struct wl_client *client;
	struct wl_list *clients;
	int empty;
	int status;

	/* The socket name is the only argument. */
	if (argc != 2) {
		fprintf(stderr, "usage: server SOCKET-NAME\n");
		return 2;
	}

	/* The display, its socket and the global. */
	display = wl_display_create();
	if (display == NULL)
		return 1;
	status = wl_display_add_socket(display, argv[1]);
	if (status != 0) {
		perror("wl_display_add_socket");
		return 1;
	}

	/* The global the client binds. */
	(void)wl_global_create(display, &zed_generic_test_v1_interface, 1, NULL, test_bind);
	printf("SERVER READY\n");
	fflush(stdout);

	/* Serves until the client that came has gone (at most 30 s). */
	loop = wl_display_get_event_loop(display);
	for (status = 0; status < 3000 && !server_client_gone; status++) {
		wl_display_flush_clients(display);
		(void)wl_event_loop_dispatch(loop, 10);

		/* The first client is watched for its end. */
		clients = wl_display_get_client_list(display);
		empty = wl_list_empty(clients);
		if (!empty && client_listener.link.next == NULL) {
			client = wl_client_from_link(clients->next);
			wl_client_add_destroy_listener(client, &client_listener);
		}
	}

	/* The end. */
	printf("SERVER DONE children_destroyed=%u client_gone=%d\n", server_children_destroyed, server_client_gone);
	wl_display_destroy(display);
	return 0;
}

/* A child is destroyed by its client. */
static void
child_destroy(
	struct wl_client *client,
	struct wl_resource *resource)
{
	(void)client;
	server_children_destroyed++;
	wl_resource_destroy(resource);
}

/* A child answers a ping with the value plus one, and a pipe. */
static void
child_ping(
	struct wl_client *client,
	struct wl_resource *resource,
	uint32_t value)
{
	int descriptors[2];
	int status;

	/* The answer, then a pipe whose read end holds "fd-ok". */
	(void)client;
	zed_generic_child_v1_send_pong(resource, value + 1U);
	status = pipe(descriptors);
	if (status != 0)
		return;
	(void)write(descriptors[1], "fd-ok", 5);
	close(descriptors[1]);
	zed_generic_child_v1_send_data(resource, descriptors[0]);
	close(descriptors[0]);
}

/* Nothing to free for a child. */
static void
child_gone(
	struct wl_resource *resource)
{
	(void)resource;
}

/* Sends every test event for one round. */
static void
test_emit(
	struct wl_client *client,
	struct wl_resource *resource,
	uint32_t round)
{
	struct wl_resource *child;
	struct wl_array array;
	unsigned char *bytes;
	int descriptors[2];
	int status;

	/* A pipe whose read end goes to the client with "fd-ok" in it. */
	status = pipe(descriptors);
	if (status != 0)
		return;
	(void)write(descriptors[1], "fd-ok", 5);
	close(descriptors[1]);

	/* Every argument type; negative numbers test the sign's extension. */
	wl_array_init(&array);
	bytes = wl_array_add(&array, 4);
	bytes[0] = 1;
	bytes[1] = 2;
	bytes[2] = 3;
	bytes[3] = 4;
	zed_generic_test_v1_send_values(resource, -7, 0xdeadbeefU, wl_fixed_from_double(-2.5), "hello", NULL, &array, descriptors[0]);
	wl_array_release(&array);
	close(descriptors[0]);

	/* A server-created child, then a reference to it and a null one. */
	child = wl_resource_create(client, &zed_generic_child_v1_interface, 1, 0);
	wl_resource_set_implementation(child, &child_implementation, NULL, child_gone);
	zed_generic_test_v1_send_child(resource, child, 40U + round);
	zed_generic_test_v1_send_reference(resource, child);
	zed_generic_test_v1_send_reference(resource, NULL);

	/* Twelve arguments: past the registers of every ABI, negative ones among the last. */
	zed_generic_test_v1_send_many(resource, -1, 1U, -2, 2U, -3, 3U, -4, wl_fixed_from_int(-5), "sixth", -100000, 0xfffffffeU, -123456789);

	/* The round is complete. */
	zed_generic_test_v1_send_round_done(resource, round);
}

/* The client destroys the global's object. */
static void
test_destroy(
	struct wl_client *client,
	struct wl_resource *resource)
{
	(void)client;
	wl_resource_destroy(resource);
}

/* Binds the test global. */
static void
test_bind(
	struct wl_client *client,
	void *data,
	uint32_t version,
	uint32_t id)
{
	struct wl_resource *resource;

	/* The object of the global for the client. */
	(void)data;
	resource = wl_resource_create(client, &zed_generic_test_v1_interface, (int)version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	/* Its requests. */
	wl_resource_set_implementation(resource, &test_implementation, NULL, NULL);
}

/* The client has gone. */
static void
client_destroyed(
	struct wl_listener *listener,
	void *data)
{
	(void)listener;
	(void)data;
	server_client_gone = 1;
}
