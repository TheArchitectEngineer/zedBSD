/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Tests zdesktop's Titlebar Presentation protocol (WS070 p008) and
 * libzdesktop's checks.
 *
 * Each server case opens its own connection, makes a toplevel and its
 * zed_titlebar_v1 through the private protocol header, sends a few
 * requests, and checks the protocol error zdesktop answers with (its
 * interface and code), or that there is none.  The library case checks
 * that libzdesktop refuses the same mistakes itself and sends nothing that
 * would end the connection.  Every case prints TITLEBARPROBE case=NAME ok
 * or FAIL, and the run ends with TITLEBARPROBE DONE failures=N.
 */

#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include <zdesktop.h>

#include "userland/base/libwayland/zed-titlebar-v1-client-protocol.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The case expects no protocol error. */
#define PROBE_NO_ERROR		0xffffffffU

/* How many calls the library case checks. */
#define PROBE_CALLS		20

/*
 * One connection of a case: the display, the globals it bound, and a
 * window (surface, xdg_surface, toplevel) with its titlebar.
 */
struct probe_connection {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct xdg_wm_base *shell;
	struct zed_titlebar_manager_v1 *manager;
	struct wl_surface *surface;
	struct xdg_surface *role;
	struct xdg_toplevel *toplevel;
	struct zed_titlebar_v1 *titlebar;
};

/*
 * One server case: its name, the requests it sends, and the error it
 * expects (the interface's name and the code, or PROBE_NO_ERROR).
 */
struct probe_case {
	const char *name;
	void (*send)(struct probe_connection *connection);
	const char *interface;
	uint32_t code;
};

static void probe_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
static void probe_global_remove(void *data, struct wl_registry *registry, uint32_t name);
static int probe_server_case(const struct probe_case *test);
static int probe_connect(struct probe_connection *connection);
static void probe_disconnect(struct probe_connection *connection);
static int probe_library(void);
static int probe_library_calls(struct zdesktop_titlebar *titlebar);
static void send_outside(struct probe_connection *connection);
static void send_zero_id(struct probe_connection *connection);
static void send_duplicate(struct probe_connection *connection);
static void send_role(struct probe_connection *connection);
static void send_mode(struct probe_connection *connection);
static void send_serial(struct probe_connection *connection);
static void send_nested(struct probe_connection *connection);
static void send_breadcrumb_role(struct probe_connection *connection);
static void send_value_role(struct probe_connection *connection);
static void send_tab_flags(struct probe_connection *connection);
static void send_focus_uncommitted(struct probe_connection *connection);
static void send_exists(struct probe_connection *connection);
static void send_good(struct probe_connection *connection);

/* The registry's callbacks while a connection binds its globals. */
static const struct wl_registry_listener probe_registry_listener = {
	probe_global, probe_global_remove
};

/* The server cases, each on its own connection. */
static const struct probe_case probe_cases[] = {
	{ "outside", send_outside, "zed_titlebar_v1", 2U },
	{ "zero-id", send_zero_id, "zed_titlebar_v1", 0U },
	{ "duplicate", send_duplicate, "zed_titlebar_v1", 0U },
	{ "role", send_role, "zed_titlebar_v1", 1U },
	{ "mode", send_mode, "zed_titlebar_v1", 1U },
	{ "serial", send_serial, "zed_titlebar_v1", 4U },
	{ "nested", send_nested, "zed_titlebar_v1", 3U },
	{ "breadcrumb-role", send_breadcrumb_role, "zed_titlebar_v1", 1U },
	{ "value-role", send_value_role, "zed_titlebar_v1", 1U },
	{ "tab-flags", send_tab_flags, "zed_titlebar_v1", 1U },
	{ "focus-uncommitted", send_focus_uncommitted, "zed_titlebar_v1", 0U },
	{ "exists", send_exists, "zed_titlebar_manager_v1", 0U },
	{ "good", send_good, NULL, PROBE_NO_ERROR }
};

/*
 * Runs every case and reports how many failed.
 */
int
main(
	int argc,
	char **argv)
{
	unsigned index;
	unsigned failures;
	int failed;

	/* No options. */
	(void)argc;
	(void)argv;

	/* Each server case. */
	failures = 0;
	for (index = 0; index < sizeof(probe_cases) / sizeof(probe_cases[0]); index++) {
		failed = probe_server_case(&probe_cases[index]);
		if (failed != 0)
			failures++;
	}

	/* The library's own checks. */
	failed = probe_library();
	if (failed != 0)
		failures++;

	/* One line for the whole run. */
	printf("TITLEBARPROBE DONE failures=%u\n", failures);
	fflush(stdout);

	/* Reports a case that failed. */
	if (failures != 0U)
		return 1;

	/* Succeeded: every case passed. */
	return 0;
}

/* Binds the compositor, the shell and the titlebar manager as the registry announces them. */
static void
probe_global(
	void *data,
	struct wl_registry *registry,
	uint32_t name,
	const char *interface,
	uint32_t version)
{
	struct probe_connection *connection;
	int same;

	/* The compositor, for a window. */
	(void)version;
	connection = data;
	same = strcmp(interface, "wl_compositor");
	if (same == 0) {
		connection->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4U);
		return;
	}

	/* The shell, for a toplevel. */
	same = strcmp(interface, "xdg_wm_base");
	if (same == 0) {
		connection->shell = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1U);
		return;
	}

	/* The Titlebar Presentation. */
	same = strcmp(interface, "zed_titlebar_manager_v1");
	if (same == 0)
		connection->manager = wl_registry_bind(registry, name, &zed_titlebar_manager_v1_interface, 1U);
}

/* A global going away does not matter to a short case. */
static void
probe_global_remove(
	void *data,
	struct wl_registry *registry,
	uint32_t name)
{
	/* Nothing to do. */
	(void)data;
	(void)registry;
	(void)name;
}

/* Runs one server case and reports whether it failed (1) or passed (0). */
static int
probe_server_case(
	const struct probe_case *test)
{
	struct probe_connection connection;
	const struct wl_interface *interface;
	uint32_t code;
	uint32_t id;
	int status;
	int same;

	/* A connection of its own, with a window and its titlebar. */
	status = probe_connect(&connection);
	if (status != 0) {
		printf("TITLEBARPROBE case=%s FAIL connect errno=%d\n", test->name, errno);
		probe_disconnect(&connection);
		return 1;
	}

	/* The requests, then a round trip that meets the answer. */
	test->send(&connection);
	status = wl_display_roundtrip(connection.display);

	/* A case that expects no error must see the round trip succeed. */
	if (test->code == PROBE_NO_ERROR) {
		code = wl_display_get_protocol_error(connection.display, NULL, NULL);
		probe_disconnect(&connection);
		if (status < 0) {
			printf("TITLEBARPROBE case=%s FAIL code=%u\n", test->name, code);
			return 1;
		}

		/* Succeeded: the model was taken without an error. */
		printf("TITLEBARPROBE case=%s ok\n", test->name);
		return 0;
	}

	/* Otherwise the connection ends with the expected interface's error code. */
	interface = NULL;
	id = 0;
	code = wl_display_get_protocol_error(connection.display, &interface, &id);

	/* The interface the error names, compared with the case's. */
	same = 1;
	if (interface != NULL)
		same = strcmp(interface->name, test->interface);

	/* A round trip that succeeded, no error, or another interface's or another code is a failure. */
	if (status >= 0 ||
	    interface == NULL ||
	    same != 0 ||
	    code != test->code) {
		printf("TITLEBARPROBE case=%s FAIL status=%d code=%u want=%u\n", test->name, status, code, test->code);
		probe_disconnect(&connection);
		return 1;
	}

	/* Succeeded: the case's error came back. */
	printf("TITLEBARPROBE case=%s ok interface=%s object=%u code=%u\n", test->name, interface->name, id, code);
	probe_disconnect(&connection);
	return 0;
}

/* Connects, binds the globals, makes a window (never committed) and its titlebar; returns 0 or -1 with errno set. */
static int
probe_connect(
	struct probe_connection *connection)
{
	int status;

	/* The connection and its registry. */
	memset(connection, 0, sizeof(*connection));
	connection->display = wl_display_connect(NULL);
	if (connection->display == NULL)
		return -1;
	connection->registry = wl_display_get_registry(connection->display);
	if (connection->registry == NULL)
		return -1;

	/* The globals, announced by one round trip. */
	status = wl_registry_add_listener(connection->registry, &probe_registry_listener, connection);
	if (status != 0)
		return -1;
	status = wl_display_roundtrip(connection->display);
	if (status < 0)
		return -1;

	/* The compositor must have the Titlebar Presentation. */
	if (connection->manager == NULL ||
	    connection->compositor == NULL ||
	    connection->shell == NULL) {
		errno = ENOTSUP;
		return -1;
	}

	/* A window, and its titlebar. */
	connection->surface = wl_compositor_create_surface(connection->compositor);
	connection->role = xdg_wm_base_get_xdg_surface(connection->shell, connection->surface);
	connection->toplevel = xdg_surface_get_toplevel(connection->role);
	connection->titlebar = zed_titlebar_manager_v1_get_titlebar(connection->manager, connection->toplevel);
	if (connection->titlebar == NULL)
		return -1;

	/* Succeeded: the case can send its requests. */
	return 0;
}

/* Disconnects (the objects go with the connection). */
static void
probe_disconnect(
	struct probe_connection *connection)
{
	/* The connection takes every object with it. */
	if (connection->display != NULL)
		wl_display_disconnect(connection->display);
	memset(connection, 0, sizeof(*connection));
}

/* A change outside a transaction. */
static void
send_outside(
	struct probe_connection *connection)
{
	/* No begin_update before it. */
	zed_titlebar_v1_set_mode(connection->titlebar, ZED_TITLEBAR_V1_MODE_CONTROLS);
}

/* A control ID of zero. */
static void
send_zero_id(
	struct probe_connection *connection)
{
	/* Zero is not a control's ID. */
	zed_titlebar_v1_begin_update(connection->titlebar, 1U);
	zed_titlebar_v1_add_control(connection->titlebar, 0U, ZDESKTOP_CONTROL_BACK, 0U, 0U, "Back");
}

/* The same control ID twice. */
static void
send_duplicate(
	struct probe_connection *connection)
{
	/* The second add reuses the first's ID. */
	zed_titlebar_v1_begin_update(connection->titlebar, 1U);
	zed_titlebar_v1_add_control(connection->titlebar, 5U, ZDESKTOP_CONTROL_BACK, 0U, 0U, "Back");
	zed_titlebar_v1_add_control(connection->titlebar, 5U, ZDESKTOP_CONTROL_FORWARD, 0U, 0U, "Forward");
}

/* A role the protocol does not name. */
static void
send_role(
	struct probe_connection *connection)
{
	/* Role 99. */
	zed_titlebar_v1_begin_update(connection->titlebar, 1U);
	zed_titlebar_v1_add_control(connection->titlebar, 1U, 99U, 0U, 0U, "What");
}

/* A mode the protocol does not name. */
static void
send_mode(
	struct probe_connection *connection)
{
	/* Mode 5. */
	zed_titlebar_v1_begin_update(connection->titlebar, 1U);
	zed_titlebar_v1_set_mode(connection->titlebar, 5U);
}

/* A commit that names another serial. */
static void
send_serial(
	struct probe_connection *connection)
{
	/* Begun as 7, committed as 8. */
	zed_titlebar_v1_begin_update(connection->titlebar, 7U);
	zed_titlebar_v1_commit(connection->titlebar, 8U);
}

/* A transaction inside a transaction. */
static void
send_nested(
	struct probe_connection *connection)
{
	/* Two begins. */
	zed_titlebar_v1_begin_update(connection->titlebar, 1U);
	zed_titlebar_v1_begin_update(connection->titlebar, 2U);
}

/* Breadcrumb parts for a search control. */
static void
send_breadcrumb_role(
	struct probe_connection *connection)
{
	struct wl_array parts;
	char *place;

	/* One part, "Home" and its NUL. */
	wl_array_init(&parts);
	place = wl_array_add(&parts, 5U);
	if (place != NULL)
		memcpy(place, "Home", 5U);

	/* The search is not a breadcrumb. */
	zed_titlebar_v1_begin_update(connection->titlebar, 1U);
	zed_titlebar_v1_add_control(connection->titlebar, 1U, ZDESKTOP_CONTROL_SEARCH, 0U, 0U, "Search");
	zed_titlebar_v1_set_breadcrumb(connection->titlebar, 1U, &parts);
	wl_array_release(&parts);
}

/* A value for a control that is not progress. */
static void
send_value_role(
	struct probe_connection *connection)
{
	/* A back button has no value. */
	zed_titlebar_v1_begin_update(connection->titlebar, 1U);
	zed_titlebar_v1_add_control(connection->titlebar, 1U, ZDESKTOP_CONTROL_BACK, 0U, 0U, "Back");
	zed_titlebar_v1_set_control_value(connection->titlebar, 1U, 500U);
}

/* Tab flags the protocol does not name. */
static void
send_tab_flags(
	struct probe_connection *connection)
{
	/* Flag 8. */
	zed_titlebar_v1_begin_update(connection->titlebar, 1U);
	zed_titlebar_v1_add_tab(connection->titlebar, 1U, "One");
	zed_titlebar_v1_set_tab(connection->titlebar, 1U, "One", 8U);
}

/* The keyboard asked for a control that is not shown yet. */
static void
send_focus_uncommitted(
	struct probe_connection *connection)
{
	/* The search is only in the open transaction. */
	zed_titlebar_v1_begin_update(connection->titlebar, 1U);
	zed_titlebar_v1_add_control(connection->titlebar, 1U, ZDESKTOP_CONTROL_SEARCH, 0U, 0U, "Search");
	zed_titlebar_v1_focus_control(connection->titlebar, 1U, 0U);
}

/* A second titlebar on one window. */
static void
send_exists(
	struct probe_connection *connection)
{
	/* The connection's window has one already. */
	(void)zed_titlebar_manager_v1_get_titlebar(connection->manager, connection->toplevel);
}

/*
 * A correct model: every control role a file manager uses with texts,
 * parts, a value and states; tabs with their flags; the mode switched in
 * one transaction; a control removed; the keyboard asked for the search.
 */
static void
send_good(
	struct probe_connection *connection)
{
	static const char parts_bytes[] = "Home\0Projects\0zedBSD";
	struct zed_titlebar_v1 *titlebar;
	struct wl_array parts;
	char *place;

	/* The breadcrumb's three parts. */
	wl_array_init(&parts);
	place = wl_array_add(&parts, sizeof(parts_bytes));
	if (place != NULL)
		memcpy(place, parts_bytes, sizeof(parts_bytes));

	/* The controls. */
	titlebar = connection->titlebar;
	zed_titlebar_v1_begin_update(titlebar, 1U);
	zed_titlebar_v1_set_mode(titlebar, ZED_TITLEBAR_V1_MODE_CONTROLS);
	zed_titlebar_v1_add_control(titlebar, 1U, ZDESKTOP_CONTROL_BACK, ZDESKTOP_PRIORITY_PRIMARY, 0U, "Back");
	zed_titlebar_v1_add_control(titlebar, 2U, ZDESKTOP_CONTROL_FORWARD, ZDESKTOP_PRIORITY_PRIMARY, 0U, "Forward");
	zed_titlebar_v1_add_control(titlebar, 3U, ZDESKTOP_CONTROL_BREADCRUMB, ZDESKTOP_PRIORITY_NORMAL, 0U, "Location");
	zed_titlebar_v1_add_control(titlebar, 4U, ZDESKTOP_CONTROL_SEARCH, ZDESKTOP_PRIORITY_NORMAL, 0U, "Search");
	zed_titlebar_v1_add_control(titlebar, 5U, ZDESKTOP_CONTROL_VIEW_GRID, ZDESKTOP_PRIORITY_SECONDARY, 1U, "Icons");
	zed_titlebar_v1_add_control(titlebar, 6U, ZDESKTOP_CONTROL_VIEW_LIST, ZDESKTOP_PRIORITY_SECONDARY, 1U, "List");
	zed_titlebar_v1_add_control(titlebar, 7U, ZDESKTOP_CONTROL_PROGRESS, ZDESKTOP_PRIORITY_NORMAL, 0U, "Copying");

	/* Their states, texts, parts and value. */
	zed_titlebar_v1_set_control_state(titlebar, 2U, 0U, 0U);
	zed_titlebar_v1_set_control_state(titlebar, 5U, 1U, 1U);
	zed_titlebar_v1_set_control_text(titlebar, 4U, "", "Search");
	zed_titlebar_v1_set_breadcrumb(titlebar, 3U, &parts);
	zed_titlebar_v1_set_control_value(titlebar, 7U, 420U);
	zed_titlebar_v1_set_control_label(titlebar, 7U, "Copying 3 of 7 items");
	zed_titlebar_v1_remove_control(titlebar, 7U);

	/* Tabs, kept while the mode is controls. */
	zed_titlebar_v1_add_tab(titlebar, 1U, "README.md");
	zed_titlebar_v1_add_tab(titlebar, 2U, "main.c");
	zed_titlebar_v1_set_tab(titlebar, 2U, "main.c", ZDESKTOP_TAB_ACTIVE | ZDESKTOP_TAB_CLOSABLE);
	zed_titlebar_v1_set_tabs_options(titlebar, ZDESKTOP_TABS_NEW_BUTTON);
	zed_titlebar_v1_remove_tab(titlebar, 1U);
	zed_titlebar_v1_commit(titlebar, 1U);
	wl_array_release(&parts);

	/* The keyboard for the search, then the mode switched to tabs and back in transactions. */
	zed_titlebar_v1_focus_control(titlebar, 4U, 0U);
	zed_titlebar_v1_begin_update(titlebar, 2U);
	zed_titlebar_v1_set_mode(titlebar, ZED_TITLEBAR_V1_MODE_TABS);
	zed_titlebar_v1_commit(titlebar, 2U);
	zed_titlebar_v1_begin_update(titlebar, 3U);
	zed_titlebar_v1_set_mode(titlebar, ZED_TITLEBAR_V1_MODE_CONTROLS);
	zed_titlebar_v1_commit(titlebar, 3U);
}

/*
 * Checks that libzdesktop refuses what the compositor would, and sends
 * nothing of it: the connection survives a round trip.  Returns 1 when a
 * check failed.
 */
static int
probe_library(void)
{
	struct probe_connection connection;
	struct zdesktop_titlebar *titlebar;
	int status;
	int failed;
	int error;

	/* A connection with a window. */
	status = probe_connect(&connection);
	if (status != 0) {
		printf("TITLEBARPROBE case=library FAIL connect errno=%d\n", errno);
		probe_disconnect(&connection);
		return 1;
	}

	/* The window's titlebar through the library (the probe's own is let go first). */
	zed_titlebar_v1_destroy(connection.titlebar);
	connection.titlebar = NULL;
	titlebar = zdesktop_titlebar_create(connection.display, connection.toplevel, NULL, NULL);
	if (titlebar == NULL) {
		printf("TITLEBARPROBE case=library FAIL create errno=%d\n", errno);
		probe_disconnect(&connection);
		return 1;
	}

	/* The calls. */
	failed = probe_library_calls(titlebar);

	/* Nothing refused was sent: the connection is still well. */
	status = wl_display_roundtrip(connection.display);
	if (status < 0) {
		error = wl_display_get_error(connection.display);
		printf("TITLEBARPROBE case=library FAIL roundtrip error=%d\n", error);
		failed = 1;
	}

	/* The titlebar and the connection go. */
	zdesktop_titlebar_destroy(titlebar);
	probe_disconnect(&connection);

	/* Reports a check that failed. */
	if (failed != 0)
		return 1;

	/* Succeeded: every check answered as the compositor would. */
	printf("TITLEBARPROBE case=library ok\n");
	return 0;
}

/* Makes the library's calls and compares their answers; returns 1 when one was wrong. */
static int
probe_library_calls(
	struct zdesktop_titlebar *titlebar)
{
	static const char *const parts[] = { "Home", "Projects" };
	int results[PROBE_CALLS];
	int wanted[PROBE_CALLS];
	unsigned index;
	int failed;

	/* Each call and what it must answer. */
	results[0] = zdesktop_titlebar_set_mode(titlebar, ZDESKTOP_TITLEBAR_CONTROLS);
	wanted[0] = EINVAL;
	results[1] = zdesktop_titlebar_begin(titlebar);
	wanted[1] = 0;
	results[2] = zdesktop_titlebar_begin(titlebar);
	wanted[2] = EBUSY;
	results[3] = zdesktop_titlebar_add_control(titlebar, 0U, ZDESKTOP_CONTROL_BACK, 0U, 0U, "Zero");
	wanted[3] = EINVAL;
	results[4] = zdesktop_titlebar_add_control(titlebar, 1U, ZDESKTOP_CONTROL_SEARCH, 0U, 0U, "Search");
	wanted[4] = 0;
	results[5] = zdesktop_titlebar_add_control(titlebar, 1U, ZDESKTOP_CONTROL_BACK, 0U, 0U, "Again");
	wanted[5] = EEXIST;
	results[6] = zdesktop_titlebar_add_control(titlebar, 2U, 99U, 0U, 0U, "Role");
	wanted[6] = EINVAL;
	results[7] = zdesktop_titlebar_set_breadcrumb(titlebar, 1U, parts, 2U);
	wanted[7] = EINVAL;
	results[8] = zdesktop_titlebar_set_control_value(titlebar, 1U, 5U);
	wanted[8] = EINVAL;
	results[9] = zdesktop_titlebar_set_control_label(titlebar, 9U, "Nothing");
	wanted[9] = ENOENT;
	results[10] = zdesktop_titlebar_focus_control(titlebar, 1U, ZDESKTOP_FOCUS_FIELD);
	wanted[10] = EINVAL;
	results[11] = zdesktop_titlebar_add_tab(titlebar, 1U, "One");
	wanted[11] = 0;
	results[12] = zdesktop_titlebar_set_tab(titlebar, 1U, "One", 8U);
	wanted[12] = EINVAL;
	results[13] = zdesktop_titlebar_remove_tab(titlebar, 2U);
	wanted[13] = ENOENT;
	results[14] = zdesktop_titlebar_add_control(titlebar, 3U, ZDESKTOP_CONTROL_BREADCRUMB, 1U, 0U, "Location");
	wanted[14] = 0;
	results[15] = zdesktop_titlebar_set_breadcrumb(titlebar, 3U, parts, 2U);
	wanted[15] = 0;
	results[16] = zdesktop_titlebar_commit(titlebar);
	wanted[16] = 0;
	results[17] = zdesktop_titlebar_commit(titlebar);
	wanted[17] = EINVAL;
	results[18] = zdesktop_titlebar_focus_control(titlebar, 1U, ZDESKTOP_FOCUS_EDIT);
	wanted[18] = 0;
	results[19] = zdesktop_titlebar_focus_control(titlebar, 1U, 7U);
	wanted[19] = EINVAL;

	/* The answers. */
	failed = 0;
	for (index = 0; index < PROBE_CALLS; index++) {
		if (results[index] != wanted[index]) {
			printf("TITLEBARPROBE case=library FAIL call=%u result=%d want=%d\n", index, results[index], wanted[index]);
			failed = 1;
		}
	}

	/* Reports whether any answer was wrong. */
	return failed;
}
