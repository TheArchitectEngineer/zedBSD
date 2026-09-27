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
 *
 *   titlebar-probe
 *   titlebar-probe --show=TITLE [--seconds=N] [--mode=menu|controls|tabs]
 *
 * --show instead shows a plain window with a title (the tests give it
 * Japanese to see the glyph cache) for some seconds, with a titlebar model
 * of the mode given (a file manager's controls, or three tabs), and prints
 * TITLEBARPROBE event lines for what zdesktop tells it.
 */

#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include <zdesktop.h>

#include "userland/base/libwayland/zed-titlebar-v1-client-protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* The case expects no protocol error. */
#define PROBE_NO_ERROR		0xffffffffU

/* How many calls the library case checks. */
#define PROBE_CALLS		20

/* The shown window's size. */
#define PROBE_WIDTH		800
#define PROBE_HEIGHT		480

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
	struct wl_shm *shm;
	struct wl_surface *surface;
	struct xdg_surface *role;
	struct xdg_toplevel *toplevel;
	struct zed_titlebar_v1 *titlebar;
	int configured;
	uint32_t configure_serial;
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
static int probe_show(const char *title, unsigned seconds, const char *mode);
static int probe_show_window(struct probe_connection *connection, const char *title);
static int probe_show_buffer(struct probe_connection *connection);
static void probe_show_model(struct zdesktop_titlebar *titlebar, const char *mode);
static void probe_configure(void *data, struct xdg_surface *role, uint32_t serial);
static void probe_activated(void *data, struct zdesktop_titlebar *titlebar, uint32_t id, uint32_t detail, struct wl_seat *seat, uint32_t serial);
static void probe_text_changed(void *data, struct zdesktop_titlebar *titlebar, uint32_t id, const char *text);
static void probe_text_done(void *data, struct zdesktop_titlebar *titlebar, uint32_t id, const char *text, unsigned how);
static void probe_tab_activated(void *data, struct zdesktop_titlebar *titlebar, uint32_t id, uint32_t serial);
static void probe_tab_close(void *data, struct zdesktop_titlebar *titlebar, uint32_t id);
static void probe_new_tab(void *data, struct zdesktop_titlebar *titlebar, uint32_t serial);
static void probe_overflow(void *data, struct zdesktop_titlebar *titlebar);

/* The registry's callbacks while a connection binds its globals. */
static const struct wl_registry_listener probe_registry_listener = {
	probe_global, probe_global_remove
};

/* The shown window's xdg_surface events: only its configure. */
static const struct xdg_surface_listener probe_role_listener = {
	probe_configure
};

/* What zdesktop tells the shown window's titlebar, printed. */
static const struct zdesktop_titlebar_listener probe_titlebar_listener = {
	probe_activated,
	probe_text_changed,
	probe_text_done,
	probe_tab_activated,
	probe_tab_close,
	probe_new_tab,
	probe_overflow
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
	const char *title;
	const char *mode;
	unsigned seconds;
	unsigned index;
	unsigned failures;
	int failed;
	int status;
	int match;

	/* The options of the shown window. */
	title = NULL;
	mode = "menu";
	seconds = 60;
	for (index = 1; index < (unsigned)argc; index++) {
		match = strncmp(argv[index], "--show=", 7);
		if (match == 0)
			title = argv[index] + 7;
		match = strncmp(argv[index], "--seconds=", 10);
		if (match == 0)
			seconds = (unsigned)atoi(argv[index] + 10);
		match = strncmp(argv[index], "--mode=", 7);
		if (match == 0)
			mode = argv[index] + 7;
	}

	/* A shown window instead of the cases. */
	if (title != NULL) {
		status = probe_show(title, seconds, mode);
		return status;
	}

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

	/* Shared memory, for the shown window's picture. */
	same = strcmp(interface, "wl_shm");
	if (same == 0) {
		connection->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1U);
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

/*
 * Shows a window with a title and a titlebar model of a mode for some
 * seconds, printing what zdesktop tells its titlebar.  Returns 0, or 1 when
 * the window could not be shown.
 */
static int
probe_show(
	const char *title,
	unsigned seconds,
	const char *mode)
{
	struct probe_connection connection;
	struct zdesktop_titlebar *titlebar;
	struct pollfd poll_entry;
	time_t end;
	time_t now;
	int status;

	/* The window. */
	status = probe_show_window(&connection, title);
	if (status != 0) {
		printf("TITLEBARPROBE show FAIL errno=%d\n", errno);
		probe_disconnect(&connection);
		return 1;
	}

	/* Its titlebar, through the library, with the model of the mode. */
	titlebar = zdesktop_titlebar_create(connection.display, connection.toplevel, &probe_titlebar_listener, NULL);
	if (titlebar == NULL) {
		printf("TITLEBARPROBE show FAIL titlebar errno=%d\n", errno);
		probe_disconnect(&connection);
		return 1;
	}

	/* The model. */
	probe_show_model(titlebar, mode);
	printf("TITLEBARPROBE show ready mode=%s\n", mode);
	fflush(stdout);

	/* The events, until the time is up. */
	end = time(NULL) + (time_t)seconds;
	for (;;) {
		now = time(NULL);
		if (now >= end)
			break;

		/* What was sent goes out; a second's wait for what comes in. */
		(void)wl_display_flush(connection.display);
		poll_entry.fd = wl_display_get_fd(connection.display);
		poll_entry.events = POLLIN;
		poll_entry.revents = 0;
		status = poll(&poll_entry, 1, 1000);
		if (status <= 0)
			continue;

		/* The events that came. */
		status = wl_display_dispatch(connection.display);
		if (status < 0)
			break;
	}

	/* The titlebar and the connection go. */
	zdesktop_titlebar_destroy(titlebar);
	probe_disconnect(&connection);
	printf("TITLEBARPROBE show done\n");
	return 0;
}

/* Connects and shows a window of the probe's size with a title; returns 0 or -1 with errno set. */
static int
probe_show_window(
	struct probe_connection *connection,
	const char *title)
{
	int status;

	/* The connection and its globals. */
	memset(connection, 0, sizeof(*connection));
	connection->display = wl_display_connect(NULL);
	if (connection->display == NULL)
		return -1;
	connection->registry = wl_display_get_registry(connection->display);
	if (connection->registry == NULL)
		return -1;
	status = wl_registry_add_listener(connection->registry, &probe_registry_listener, connection);
	if (status != 0)
		return -1;
	status = wl_display_roundtrip(connection->display);
	if (status < 0)
		return -1;
	if (connection->compositor == NULL || connection->shell == NULL || connection->shm == NULL) {
		errno = ENOTSUP;
		return -1;
	}

	/* The window, its title, and its first configure. */
	connection->surface = wl_compositor_create_surface(connection->compositor);
	connection->role = xdg_wm_base_get_xdg_surface(connection->shell, connection->surface);
	(void)xdg_surface_add_listener(connection->role, &probe_role_listener, connection);
	connection->toplevel = xdg_surface_get_toplevel(connection->role);
	xdg_toplevel_set_title(connection->toplevel, title);
	xdg_toplevel_set_app_id(connection->toplevel, "titlebar-probe");
	wl_surface_commit(connection->surface);
	while (connection->configured == 0) {
		status = wl_display_dispatch(connection->display);
		if (status < 0)
			return -1;
	}

	/* The configure answered, and the picture. */
	xdg_surface_ack_configure(connection->role, connection->configure_serial);
	status = probe_show_buffer(connection);
	if (status != 0)
		return -1;

	/* Succeeded: the window is shown. */
	return 0;
}

/* Attaches a pale picture of the probe's size to the window and commits it; returns 0 or -1. */
static int
probe_show_buffer(
	struct probe_connection *connection)
{
	struct wl_shm_pool *pool;
	struct wl_buffer *buffer;
	uint32_t *pixels;
	char name[64];
	size_t bytes;
	size_t index;
	int descriptor;
	int error;

	/* Shared memory for one picture, named only until it is unlinked. */
	bytes = (size_t)PROBE_WIDTH * PROBE_HEIGHT * 4U;
	snprintf(name, sizeof(name), "/titlebar-probe-%ld", (long)getpid());
	descriptor = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (descriptor < 0)
		return -1;
	(void)shm_unlink(name);
	error = ftruncate(descriptor, (off_t)bytes);
	if (error != 0) {
		close(descriptor);
		return -1;
	}

	/* Its mapping. */
	pixels = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
	if (pixels == MAP_FAILED) {
		close(descriptor);
		return -1;
	}

	/* A pale blue-grey picture. */
	for (index = 0; index < (size_t)PROBE_WIDTH * PROBE_HEIGHT; index++)
		pixels[index] = 0xffeef2f7U;

	/* The buffer, attached and shown. */
	pool = wl_shm_create_pool(connection->shm, descriptor, (int32_t)bytes);
	close(descriptor);
	buffer = wl_shm_pool_create_buffer(pool, 0, PROBE_WIDTH, PROBE_HEIGHT, PROBE_WIDTH * 4, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	wl_surface_attach(connection->surface, buffer, 0, 0);
	wl_surface_damage(connection->surface, 0, 0, PROBE_WIDTH, PROBE_HEIGHT);
	wl_surface_commit(connection->surface);

	/* Succeeded: the picture is on its way. */
	return 0;
}

/* Gives the shown window's titlebar a model: a file manager's controls, three tabs, or the menu mode alone. */
static void
probe_show_model(
	struct zdesktop_titlebar *titlebar,
	const char *mode)
{
	static const char *const parts[] = { "Home", "Projects", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e" };
	int controls;
	int tabs;

	/* Which mode. */
	controls = strcmp(mode, "controls");
	tabs = strcmp(mode, "tabs");

	/* One transaction for the whole model. */
	(void)zdesktop_titlebar_begin(titlebar);
	if (controls == 0) {
		(void)zdesktop_titlebar_set_mode(titlebar, ZDESKTOP_TITLEBAR_CONTROLS);
		(void)zdesktop_titlebar_add_control(titlebar, 1U, ZDESKTOP_CONTROL_BACK, ZDESKTOP_PRIORITY_PRIMARY, 0U, "Back");
		(void)zdesktop_titlebar_add_control(titlebar, 2U, ZDESKTOP_CONTROL_FORWARD, ZDESKTOP_PRIORITY_PRIMARY, 0U, "Forward");
		(void)zdesktop_titlebar_add_control(titlebar, 3U, ZDESKTOP_CONTROL_HOME, ZDESKTOP_PRIORITY_PRIMARY, 0U, "Home");
		(void)zdesktop_titlebar_add_control(titlebar, 4U, ZDESKTOP_CONTROL_BREADCRUMB, ZDESKTOP_PRIORITY_NORMAL, 0U, "Location");
		(void)zdesktop_titlebar_set_breadcrumb(titlebar, 4U, parts, 3U);
		(void)zdesktop_titlebar_add_control(titlebar, 5U, ZDESKTOP_CONTROL_SEARCH, ZDESKTOP_PRIORITY_NORMAL, 0U, "Search");
		(void)zdesktop_titlebar_set_control_text(titlebar, 5U, "", "Search");
		(void)zdesktop_titlebar_add_control(titlebar, 6U, ZDESKTOP_CONTROL_VIEW_GRID, ZDESKTOP_PRIORITY_SECONDARY, 1U, "Icons");
		(void)zdesktop_titlebar_set_control_state(titlebar, 6U, 1, 1);
		(void)zdesktop_titlebar_add_control(titlebar, 7U, ZDESKTOP_CONTROL_VIEW_LIST, ZDESKTOP_PRIORITY_SECONDARY, 1U, "List");
		(void)zdesktop_titlebar_add_control(titlebar, 8U, ZDESKTOP_CONTROL_PREVIEW, ZDESKTOP_PRIORITY_SECONDARY, 0U, "Preview");
		(void)zdesktop_titlebar_set_control_state(titlebar, 2U, 0, 0);
	} else if (tabs == 0) {
		(void)zdesktop_titlebar_set_mode(titlebar, ZDESKTOP_TITLEBAR_TABS);
		(void)zdesktop_titlebar_add_tab(titlebar, 1U, "README.md");
		(void)zdesktop_titlebar_add_tab(titlebar, 2U, "main.c");
		(void)zdesktop_titlebar_add_tab(titlebar, 3U, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e.txt");
		(void)zdesktop_titlebar_set_tab(titlebar, 2U, "main.c", ZDESKTOP_TAB_ACTIVE | ZDESKTOP_TAB_CLOSABLE);
		(void)zdesktop_titlebar_set_tab(titlebar, 3U, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e.txt", ZDESKTOP_TAB_ATTENTION | ZDESKTOP_TAB_CLOSABLE);
		(void)zdesktop_titlebar_set_tabs_options(titlebar, ZDESKTOP_TABS_NEW_BUTTON);
	}

	/* The model is shown at once. */
	(void)zdesktop_titlebar_commit(titlebar);
}

/* Notes the window's configure (answered once the window is set up). */
static void
probe_configure(
	void *data,
	struct xdg_surface *role,
	uint32_t serial)
{
	struct probe_connection *connection;

	/* The serial to answer, and that the first came. */
	(void)role;
	connection = data;
	connection->configure_serial = serial;
	connection->configured = 1;
}

/* Prints a chosen control. */
static void
probe_activated(
	void *data,
	struct zdesktop_titlebar *titlebar,
	uint32_t id,
	uint32_t detail,
	struct wl_seat *seat,
	uint32_t serial)
{
	/* The event's line. */
	(void)data;
	(void)titlebar;
	(void)seat;
	printf("TITLEBARPROBE event=activated id=%u detail=%u serial=%u\n", id, detail, serial);
	fflush(stdout);
}

/* Prints a text control's new text. */
static void
probe_text_changed(
	void *data,
	struct zdesktop_titlebar *titlebar,
	uint32_t id,
	const char *text)
{
	/* The event's line. */
	(void)data;
	(void)titlebar;
	printf("TITLEBARPROBE event=text id=%u text=%s\n", id, text);
	fflush(stdout);
}

/* Prints the end of a text control's editing. */
static void
probe_text_done(
	void *data,
	struct zdesktop_titlebar *titlebar,
	uint32_t id,
	const char *text,
	unsigned how)
{
	/* The event's line. */
	(void)data;
	(void)titlebar;
	printf("TITLEBARPROBE event=done id=%u how=%u text=%s\n", id, how, text);
	fflush(stdout);
}

/* Prints a chosen tab. */
static void
probe_tab_activated(
	void *data,
	struct zdesktop_titlebar *titlebar,
	uint32_t id,
	uint32_t serial)
{
	/* The event's line. */
	(void)data;
	(void)titlebar;
	printf("TITLEBARPROBE event=tab id=%u serial=%u\n", id, serial);
	fflush(stdout);
}

/* Prints a tab's close button. */
static void
probe_tab_close(
	void *data,
	struct zdesktop_titlebar *titlebar,
	uint32_t id)
{
	/* The event's line. */
	(void)data;
	(void)titlebar;
	printf("TITLEBARPROBE event=close id=%u\n", id);
	fflush(stdout);
}

/* Prints the new-tab button. */
static void
probe_new_tab(
	void *data,
	struct zdesktop_titlebar *titlebar,
	uint32_t serial)
{
	/* The event's line. */
	(void)data;
	(void)titlebar;
	printf("TITLEBARPROBE event=new serial=%u\n", serial);
	fflush(stdout);
}

/* Prints the overflow popup's opening. */
static void
probe_overflow(
	void *data,
	struct zdesktop_titlebar *titlebar)
{
	/* The event's line. */
	(void)data;
	(void)titlebar;
	printf("TITLEBARPROBE event=overflow\n");
	fflush(stdout);
}
