/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws156-p006: the Linux backend's notification server
 * (userland/desktop/libkeiland-backend-linux/notify-linux.c with
 * dbus-linux.c) against a fake session bus on the host.
 *
 * The test listens on a Unix socket and names it in
 * DBUS_SESSION_BUS_ADDRESS, as a session's bus does.  A child process is
 * the bus and the programs on it: it answers the authentication, Hello and
 * RequestName, then sends the calls a program sends (with the SENDER field
 * the bus daemon adds) and checks the answers and the signals.  The parent
 * is the compositor: it opens the server through kl_backend_notify_open,
 * polls it, takes the posts and the withdrawals in its callbacks, and tells
 * closes and clicks back as the compositor does.  The child's checks print
 * "ok" or "FAIL" lines; the parent checks what reached its callbacks.
 */

#include "userland/desktop/libkeiland-backend/backend-private.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* A parameter a callback has to take but does not use. */
#define UNUSED_PARAMETER(name) ((void)(name))

/* The largest frame either side builds here. */
#define TEST_FRAME_MAX 8192U

/* How many posts the compositor's side remembers. */
#define TEST_POSTS 32U

/* How long the whole exchange may take, in milliseconds. */
#define TEST_TIMEOUT_MS 20000U

/* The notifications' object and interface, as a program names them. */
#define TEST_PATH "/org/freedesktop/Notifications"
#define TEST_INTERFACE "org.freedesktop.Notifications"

/* One frame being built by the child: its bytes and how many are used. */
struct test_frame {
	unsigned char bytes[TEST_FRAME_MAX];
	size_t used;
};

/* One frame the child received: its kind, the serial it answers, its error name and signature, and its body. */
struct test_message {
	unsigned char bytes[TEST_FRAME_MAX];
	size_t size;
	unsigned type;
	uint32_t reply_serial;
	char error_name[128];
	char member[64];
	char signature[64];
	size_t body;
	size_t cursor;
};

/* One post the compositor's side was given, as its callback saw it. */
struct test_post {
	uint32_t sender;
	uint32_t id;
	uint32_t replaces;
	unsigned flags;
	char app[KL_BACKEND_NOTIFY_APP_MAX + 1U];
	char title[KL_BACKEND_NOTIFY_TITLE_MAX + 1U];
	char body[KL_BACKEND_NOTIFY_BODY_MAX + 1U];
};

/* The checks' counts in this process. */
static int test_passed;
static int test_failed;

/* The compositor's side: the posts taken, the next number, the click and close it owes after the poll, the ones withdrawn. */
static struct test_post test_posts[TEST_POSTS];
static size_t test_post_count;
static uint32_t test_next_id;
static uint32_t test_activate_id;
static uint32_t test_withdrawn_count;

/* The child's connection and its serials. */
static int test_fd;
static uint32_t test_serial;

static void check(int ok, const char *what);
static int same(const char *text, const char *expected);
static int same_prefix(const char *text, const char *prefix);
static uint32_t test_posted(void *data, uint32_t sender, const char *app, const char *title, const char *body, uint32_t replaces, unsigned flags, int *error);
static int test_withdrawn(void *data, uint32_t sender, uint32_t id);
static int run_compositor(pid_t child);
static void check_posts(void);
static int run_bus(int listener);
static int bus_handshake(void);
static void bus_calls(void);
static void put8(struct test_frame *frame, unsigned value);
static void put32(struct test_frame *frame, uint32_t value);
static void pad(struct test_frame *frame, size_t alignment);
static void put_string(struct test_frame *frame, const char *text);
static void put_signature(struct test_frame *frame, const char *text);
static void put_field(struct test_frame *frame, unsigned code, char type, const char *text);
static void put_field_number(struct test_frame *frame, unsigned code, uint32_t number);
static size_t begin_array(struct test_frame *frame, size_t element_alignment, size_t *start);
static void end_array(struct test_frame *frame, size_t length_at, size_t start);
static void frame_header(struct test_frame *frame, unsigned type, uint32_t serial);
static size_t frame_fields_end(struct test_frame *frame);
static void frame_finish(struct test_frame *frame, size_t body);
static uint32_t send_call(const char *sender, const char *path, const char *interface, const char *member, const char *signature, struct test_frame *body);
static int send_raw(const void *bytes, size_t size);
static int read_exact(unsigned char *bytes, size_t size);
static int receive(struct test_message *message);
static int receive_reply(uint32_t serial, struct test_message *message);
static uint32_t get32(const unsigned char *bytes);
static int read_u32(struct test_message *message, uint32_t *value);
static int read_string(struct test_message *message, const char **text);
static void notify_body(struct test_frame *body, const char *app, uint32_t replaces, const char *summary, const char *text, int with_default, int urgency, int extra_hints);
static uint32_t call_notify(const char *sender, const char *app, uint32_t replaces, const char *summary, const char *text, int with_default, int urgency, int extra_hints, struct test_message *reply);
static int expect_signal(const char *member, uint32_t id, uint32_t reason);

/* Runs the exchange: the bus in a child, the compositor here. */
int
main(int argc, char **argv)
{
	struct sockaddr_un address;
	char variable[sizeof(address.sun_path) + 16U];
	size_t path_length;
	pid_t child;
	int listener;
	int status;
	int error;

	/* The socket's path, given by the script. */
	if (argc != 2) {
		fprintf(stderr, "usage: host-notify-dbus SOCKET\n");
		return 2;
	}

	/* The address, when the path fits. */
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	path_length = strlen(argv[1]);
	if (path_length >= sizeof(address.sun_path))
		return 2;
	strcpy(address.sun_path, argv[1]);

	/* The bus's socket, listening before the server connects. */
	listener = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listener < 0)
		return 2;
	error = bind(listener, (struct sockaddr *)&address, sizeof(address));
	if (error != 0) {
		perror("bind");
		return 2;
	}

	/* Listening. */
	error = listen(listener, 4);
	if (error != 0)
		return 2;

	/* The session's bus, as the server finds it. */
	snprintf(variable, sizeof(variable), "unix:path=%s,guid=0123456789abcdef", argv[1]);
	setenv("DBUS_SESSION_BUS_ADDRESS", variable, 1);

	/* The bus and its programs, in a child. */
	child = fork();
	if (child < 0)
		return 2;
	if (child == 0) {
		status = run_bus(listener);
		_exit(status);
	}

	/* The parent does not accept. */
	close(listener);

	/* The compositor's side, until the child is done. */
	status = run_compositor(child);

	/* The result of both. */
	printf("host-notify-dbus: compositor passed=%d failed=%d\n", test_passed, test_failed);
	if (status != 0 || test_failed != 0)
		return 1;
	return 0;
}

/* Prints one check and counts it. */
static void
check(
	int ok,
	const char *what)
{
	/* Counted, and printed for the log. */
	if (ok) {
		test_passed++;
		printf("ok %s\n", what);
	} else {
		test_failed++;
		printf("FAIL %s\n", what);
	}

	/* Printed at once, before a child's lines. */
	fflush(stdout);
}

/* Tells whether a text is the expected one. */
static int
same(
	const char *text,
	const char *expected)
{
	int order;

	/* The same characters. */
	order = strcmp(text, expected);
	if (order != 0)
		return 0;
	return 1;
}

/* Tells whether a text starts with a prefix. */
static int
same_prefix(
	const char *text,
	const char *prefix)
{
	int order;

	/* The prefix's characters. */
	order = strncmp(text, prefix, strlen(prefix));
	if (order != 0)
		return 0;
	return 1;
}

/* The compositor's posted callback: refusals by title, a replacement keeps its number, a click is owed for "activate-me". */
static uint32_t
test_posted(
	void *data,
	uint32_t sender,
	const char *app,
	const char *title,
	const char *body,
	uint32_t replaces,
	unsigned flags,
	int *error)
{
	struct test_post *post;
	size_t index;
	uint32_t id;
	int busy;
	int denied;
	int activate;

	UNUSED_PARAMETER(data);

	/* Refusals the child asks for by title. */
	busy = same(title, "refuse-busy");
	denied = same(title, "refuse-denied");
	if (busy) {
		*error = EBUSY;
		return 0;
	}

	/* The user's refusal. */
	if (denied) {
		*error = EACCES;
		return 0;
	}

	/* A replacement of a known one of the same sender keeps its number. */
	id = 0;
	for (index = 0; index < test_post_count; index++) {
		if (replaces != 0 && test_posts[index].id == replaces && test_posts[index].sender == sender)
			id = replaces;
	}

	/* A new one: the next number. */
	if (id == 0) {
		test_next_id++;
		id = test_next_id;
	}

	/* Remembered for the checks after the exchange. */
	if (test_post_count < TEST_POSTS) {
		post = &test_posts[test_post_count];
		test_post_count++;
		post->sender = sender;
		post->id = id;
		post->replaces = replaces;
		post->flags = flags;
		snprintf(post->app, sizeof(post->app), "%s", app);
		snprintf(post->title, sizeof(post->title), "%s", title);
		snprintf(post->body, sizeof(post->body), "%s", body);
	}

	/* A click owed after the poll (a callback never calls back into the backend). */
	activate = same(title, "activate-me");
	if (activate)
		test_activate_id = id;
	return id;
}

/* The compositor's withdrawn callback: a known one goes, told as closed by its sender. */
static int
test_withdrawn(
	void *data,
	uint32_t sender,
	uint32_t id)
{
	struct kl_backend *backend;
	size_t index;

	/* The sender's own, posted before. */
	backend = data;
	for (index = 0; index < test_post_count; index++) {
		if (test_posts[index].id != id || test_posts[index].sender != sender)
			continue;
		test_withdrawn_count++;
		kl_backend_notify_closed(backend, id, KL_BACKEND_NOTIFY_CLOSED);
		return 0;
	}

	/* Not the sender's. */
	return ENOENT;
}

/* Runs the compositor's side: opens the server, polls it until the child exits, and checks what came. */
static int
run_compositor(
	pid_t child)
{
	struct kl_backend_notify_callbacks callbacks;
	struct kl_backend *backend;
	struct pollfd descriptor;
	struct timespec now;
	struct timespec start;
	long elapsed;
	size_t count;
	pid_t done;
	int exited;
	int code;
	int status;
	int error;

	/* A backend of the notifications alone. */
	backend = calloc(1, sizeof(*backend));
	if (backend == NULL)
		return 1;
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.posted = test_posted;
	callbacks.withdrawn = test_withdrawn;

	/* The server on the fake session bus. */
	error = kl_backend_notify_open(backend, &callbacks, backend);
	check(error == 0, "kl_backend_notify_open on the session bus");
	if (error != 0) {
		kill(child, SIGKILL);
		waitpid(child, &status, 0);
		return 1;
	}

	/* The bus in the poll. */
	count = kl_backend_notify_poll_count(backend);
	check(count == 1, "the bus joins the poll");

	/* The loop, until the child ends or the time is up. */
	clock_gettime(CLOCK_MONOTONIC, &start);
	status = 0;
	for (;;) {
		/* The child's end. */
		done = waitpid(child, &status, WNOHANG);
		if (done == child)
			break;
		clock_gettime(CLOCK_MONOTONIC, &now);
		elapsed = (now.tv_sec - start.tv_sec) * 1000L + (now.tv_nsec - start.tv_nsec) / 1000000L;
		if (elapsed > (long)TEST_TIMEOUT_MS) {
			check(0, "the exchange ends in time");
			kill(child, SIGKILL);
			waitpid(child, &status, 0);
			break;
		}

		/* The backend's descriptor, while it has one. */
		count = kl_backend_notify_poll_count(backend);
		if (count == 0) {
			poll(NULL, 0, 10);
			continue;
		}

		/* One wait, then what came handled. */
		memset(&descriptor, 0, sizeof(descriptor));
		kl_backend_notify_poll_fill(backend, &descriptor);
		poll(&descriptor, 1, 50);
		kl_backend_notify_poll_done(backend, &descriptor);

		/* The click owed: told, then closed as the user's (the compositor's order). */
		if (test_activate_id != 0) {
			kl_backend_notify_activated(backend, test_activate_id);
			kl_backend_notify_closed(backend, test_activate_id, KL_BACKEND_NOTIFY_DISMISSED);
			test_activate_id = 0;
		}
	}

	/* The broken frame the child sent last closed the bus. */
	count = kl_backend_notify_poll_count(backend);
	check(count == 0, "a broken frame closes the bus and leaves the poll");

	/* What reached the callbacks. */
	check_posts();

	/* Closing frees it; the child's result counts too. */
	kl_backend_notify_close(backend);
	check(backend->notify == NULL, "kl_backend_notify_close frees the server");
	free(backend);
	exited = WIFEXITED(status);
	code = -1;
	if (exited)
		code = WEXITSTATUS(status);
	check(code == 0, "the bus's checks passed");
	if (code != 0)
		return 1;
	return 0;
}

/* Checks the posts the callbacks were given. */
static void
check_posts(void)
{
	const struct test_post *first;
	const struct test_post *replaced;
	const struct test_post *cut;
	const struct test_post *other;
	const char *title;
	size_t index;
	int is_hello;
	int is_again;
	int is_other;
	size_t length;
	int valid;

	/* The first: words as sent, the markup taken out of the body, urgent with a click. */
	first = NULL;
	replaced = NULL;
	cut = NULL;
	other = NULL;
	for (index = 0; index < test_post_count; index++) {
		/* Each post by its title. */
		title = test_posts[index].title;
		is_hello = same(title, "Hello");
		is_again = same(title, "Hello again");
		is_other = same(title, "other sender");
		if (first == NULL && is_hello)
			first = &test_posts[index];
		if (is_again)
			replaced = &test_posts[index];
		if (title[0] == '\xc3' && title[1] == '\xa9')
			cut = &test_posts[index];
		if (is_other)
			other = &test_posts[index];
	}

	/* Its words, its flags and its sender. */
	check(first != NULL && same(first->app, "test-app"), "posted: the application's name");
	check(first != NULL && same(first->body, "bold and a lone < sign"), "posted: tags taken out of the body, a lone < kept");
	check(first != NULL && first->flags == (KL_BACKEND_NOTIFY_URGENT | KL_BACKEND_NOTIFY_ACTION), "posted: urgency 2 is urgent, a default action is a click");
	check(first != NULL && first->sender != 0, "posted: the sender has a number");

	/* The replacement names the first and keeps its number; no action, no urgency. */
	check(replaced != NULL && first != NULL && replaced->replaces == first->id && replaced->id == first->id, "replaced: the same number");
	check(replaced != NULL && replaced->flags == 0, "replaced: no default action and urgency 1 give no flags");
	check(replaced != NULL && same(replaced->body, "x < y > z, <3"), "replaced: a '<' of plain words is kept");

	/* The long words, cut at a character's boundary. */
	length = 0;
	valid = 0;
	if (cut != NULL) {
		length = strlen(cut->title);
		valid = length % 2U == 0U;
	}

	/* The title, the body and the name each within its bound. */
	check(cut != NULL && length <= KL_BACKEND_NOTIFY_TITLE_MAX && length >= KL_BACKEND_NOTIFY_TITLE_MAX - 1U && valid, "cut: the title fits, whole characters");
	check(cut != NULL && strlen(cut->body) == KL_BACKEND_NOTIFY_BODY_MAX, "cut: the body fits");
	check(cut != NULL && strlen(cut->app) == KL_BACKEND_NOTIFY_APP_MAX, "cut: the application's name fits");

	/* Another connection is another sender. */
	check(other != NULL && first != NULL && other->sender != first->sender, "another connection has another sender number");
	check(test_withdrawn_count == 1, "CloseNotification withdrew the sender's own only");
}

/* Runs the bus's side in the child: accepts the server, answers its open, then sends the programs' calls. */
static int
run_bus(
	int listener)
{
	int error;

	/* The server's connection. */
	test_fd = accept(listener, NULL, NULL);
	if (test_fd < 0)
		return 2;
	close(listener);

	/* Its authentication, Hello and RequestName. */
	error = bus_handshake();
	check(error == 0, "bus: authentication, Hello and RequestName");
	if (error != 0)
		return 1;

	/* The programs' calls. */
	bus_calls();

	/* The child's result. */
	printf("host-notify-dbus: bus passed=%d failed=%d\n", test_passed, test_failed);
	fflush(stdout);
	if (test_failed != 0)
		return 1;
	return 0;
}

/* Answers the server's EXTERNAL authentication, BEGIN, Hello and RequestName. */
static int
bus_handshake(void)
{
	struct test_message message;
	struct test_frame frame;
	char line[256];
	size_t used;
	ssize_t got;
	char byte;
	uint32_t answered;
	size_t body;
	int auth_seen;
	int is_auth;
	int is_begin;
	int expected;
	int error;

	/* The credentials' NUL, then lines until BEGIN. */
	got = read(test_fd, &byte, 1);
	if (got != 1 || byte != '\0')
		return EPROTO;
	auth_seen = 0;
	for (;;) {
		/* One line. */
		used = 0;
		while (used + 1U < sizeof(line)) {
			got = read(test_fd, line + used, 1);
			if (got != 1)
				return EPIPE;
			used++;
			if (used >= 2 && line[used - 2] == '\r' && line[used - 1] == '\n')
				break;
		}

		/* The line, ended. */
		line[used] = '\0';
		is_auth = 0;
		if (used > 14)
			is_auth = same_prefix(line, "AUTH EXTERNAL ");
		is_begin = same(line, "BEGIN\r\n");

		/* EXTERNAL is accepted; a descriptor negotiation is not expected on the session bus. */
		if (is_auth) {
			auth_seen = 1;
			error = send_raw("OK 0123456789abcdef0123456789abcdef\r\n", 37);
			if (error != 0)
				return error;
			continue;
		}

		/* BEGIN ends the lines; anything else is not expected. */
		if (is_begin)
			break;
		return EPROTO;
	}

	/* The authentication came before BEGIN. */
	check(auth_seen, "bus: EXTERNAL authentication before BEGIN, no NEGOTIATE_UNIX_FD");

	/* Hello, answered with the unique name. */
	error = receive(&message);
	if (error != 0)
		return error;
	expected = same(message.member, "Hello");
	if (!expected)
		return EPROTO;
	answered = get32(message.bytes + 8);
	frame_header(&frame, 2, 1000);
	put_field_number(&frame, 5, answered);
	put_field(&frame, 8, 'g', "s");
	body = frame_fields_end(&frame);
	put_string(&frame, ":1.1");
	frame_finish(&frame, body);
	error = send_raw(frame.bytes, frame.used);
	if (error != 0)
		return error;

	/* RequestName(org.freedesktop.Notifications, 4), answered as the primary owner. */
	error = receive(&message);
	if (error != 0)
		return error;
	expected = same(message.member, "RequestName");
	if (expected)
		expected = same(message.signature, "su");
	if (!expected)
		return EPROTO;
	answered = get32(message.bytes + 8);
	frame_header(&frame, 2, 1001);
	put_field_number(&frame, 5, answered);
	put_field(&frame, 8, 'g', "u");
	body = frame_fields_end(&frame);
	put32(&frame, 1);
	frame_finish(&frame, body);
	error = send_raw(frame.bytes, frame.used);
	if (error != 0)
		return error;

	/* Succeeded: the server owns the name. */
	return 0;
}

/* Sends the programs' calls and checks every answer and signal. */
static void
bus_calls(void)
{
	struct test_message reply;
	struct test_frame body;
	const char *text;
	const char *texts[4];
	char long_title[301];
	char long_body[1001];
	char long_app[200];
	size_t end;
	size_t index;
	uint32_t serial;
	uint32_t first;
	uint32_t again;
	uint32_t id;
	uint32_t length;
	int error;

	/* GetCapabilities: body, actions, persistence. */
	body.used = 0;
	serial = send_call(":1.5", TEST_PATH, TEST_INTERFACE, "GetCapabilities", "", &body);
	error = receive_reply(serial, &reply);
	check(error == 0 && reply.type == 2 && strcmp(reply.signature, "as") == 0, "GetCapabilities answers as");
	length = 0;
	if (error == 0)
		read_u32(&reply, &length);
	end = reply.cursor + length;
	index = 0;
	while (error == 0 && reply.cursor < end && index < 4) {
		error = read_string(&reply, &texts[index]);
		index++;
	}

	/* The three, in order. */
	check(error == 0 && index == 3 && same(texts[0], "body") && same(texts[1], "actions") && same(texts[2], "persistence"),
	      "GetCapabilities: body, actions, persistence");

	/* GetServerInformation: Keiland, zedBSD, its version, 1.2. */
	serial = send_call(":1.5", TEST_PATH, TEST_INTERFACE, "GetServerInformation", "", &body);
	error = receive_reply(serial, &reply);
	for (index = 0; error == 0 && index < 4; index++)
		error = read_string(&reply, &texts[index]);
	check(error == 0 && strcmp(reply.signature, "ssss") == 0 && strcmp(texts[0], "Keiland") == 0 && strcmp(texts[1], "zedBSD") == 0 && strcmp(texts[3], "1.2") == 0,
	      "GetServerInformation: Keiland, zedBSD, 1.2");

	/* Notify with markup, a default action, urgency 2 and hints to step over. */
	first = call_notify(":1.5", "test-app", 0, "Hello", "<b>bold</b> and a lone < sign", 1, 2, 1, &reply);
	check(first != 0, "Notify answers a number");

	/* The same sender replaces it. */
	again = call_notify(":1.5", "test-app", first, "Hello again", "x < y > z, <3", 0, 1, 0, &reply);
	check(again == first, "Notify with replaces_id keeps the number");

	/* Words longer than the compositor keeps are cut, not refused. */
	for (index = 0; index + 2U <= 300U; index += 2U)
		memcpy(long_title + index, "\xc3\xa9", 2);
	long_title[300] = '\0';
	memset(long_body, 'x', 1000);
	long_body[1000] = '\0';
	memset(long_app, 'a', 199);
	long_app[199] = '\0';
	id = call_notify(":1.5", long_app, 0, long_title, long_body, 0, 1, 0, &reply);
	check(id != 0, "long words are taken");

	/* The compositor's refusals become the bus's errors. */
	call_notify(":1.5", "test-app", 0, "refuse-busy", "", 0, 1, 0, &reply);
	check(reply.type == 3 && strcmp(reply.error_name, "org.freedesktop.DBus.Error.LimitsExceeded") == 0, "too many or too fast: LimitsExceeded");
	call_notify(":1.5", "test-app", 0, "refuse-denied", "", 0, 1, 0, &reply);
	check(reply.type == 3 && strcmp(reply.error_name, "org.freedesktop.DBus.Error.AccessDenied") == 0, "turned off by the user: AccessDenied");

	/* Another connection is another sender, which cannot close the first's. */
	call_notify(":1.9", "other", 0, "other sender", "", 0, 1, 0, &reply);
	body.used = 0;
	put32(&body, first);
	serial = send_call(":1.9", TEST_PATH, TEST_INTERFACE, "CloseNotification", "u", &body);
	error = receive_reply(serial, &reply);
	check(error == 0 && reply.type == 2, "CloseNotification of another's: the empty answer, nothing closed");

	/* The sender closes its own: NotificationClosed(id, 3), then the empty answer. */
	body.used = 0;
	put32(&body, first);
	serial = send_call(":1.5", TEST_PATH, TEST_INTERFACE, "CloseNotification", "u", &body);
	error = expect_signal("NotificationClosed", first, 3);
	check(error == 0, "CloseNotification: NotificationClosed(id, 3)");
	error = receive_reply(serial, &reply);
	check(error == 0 && reply.type == 2, "CloseNotification: the empty answer");

	/* A click: ActionInvoked(id, default), then NotificationClosed(id, 2). */
	id = call_notify(":1.5", "test-app", 0, "activate-me", "", 1, 1, 0, &reply);
	error = expect_signal("ActionInvoked", id, 0);
	check(error == 0, "a click: ActionInvoked(id, default)");
	error = expect_signal("NotificationClosed", id, 2);
	check(error == 0, "a click: then NotificationClosed(id, 2)");

	/* Introspection of the object, and of the root naming its child. */
	body.used = 0;
	serial = send_call(":1.5", TEST_PATH, "org.freedesktop.DBus.Introspectable", "Introspect", "", &body);
	error = receive_reply(serial, &reply);
	if (error == 0)
		error = read_string(&reply, &text);
	check(error == 0 && strstr(text, "name=\"Notify\"") != NULL && strstr(text, "type=\"a{sv}\"") != NULL, "Introspect describes Notify");
	serial = send_call(":1.5", "/", "org.freedesktop.DBus.Introspectable", "Introspect", "", &body);
	error = receive_reply(serial, &reply);
	if (error == 0)
		error = read_string(&reply, &text);
	check(error == 0 && strstr(text, "<node name=\"org\"/>") != NULL, "Introspect of / names org");

	/* Ping, and the refusals of an unknown method and of wrong arguments. */
	serial = send_call(":1.5", "/anything", "org.freedesktop.DBus.Peer", "Ping", "", &body);
	error = receive_reply(serial, &reply);
	check(error == 0 && reply.type == 2, "Ping answers on any path");
	serial = send_call(":1.5", TEST_PATH, TEST_INTERFACE, "NoSuchMethod", "", &body);
	error = receive_reply(serial, &reply);
	check(error == 0 && reply.type == 3 && strcmp(reply.error_name, "org.freedesktop.DBus.Error.UnknownMethod") == 0, "an unknown method: UnknownMethod");
	body.used = 0;
	put_string(&body, "only one string");
	serial = send_call(":1.5", TEST_PATH, TEST_INTERFACE, "Notify", "s", &body);
	error = receive_reply(serial, &reply);
	check(error == 0 && reply.type == 3 && strcmp(reply.error_name, "org.freedesktop.DBus.Error.InvalidArgs") == 0, "Notify with another signature: InvalidArgs");

	/* A body shorter than its signature says: InvalidArgs, and the bus stays. */
	body.used = 0;
	put_string(&body, "app");
	put32(&body, 0);
	serial = send_call(":1.5", TEST_PATH, TEST_INTERFACE, "Notify", "susssasa{sv}i", &body);
	error = receive_reply(serial, &reply);
	check(error == 0 && reply.type == 3 && strcmp(reply.error_name, "org.freedesktop.DBus.Error.InvalidArgs") == 0, "a short Notify body: InvalidArgs");
	body.used = 0;
	serial = send_call(":1.5", "/", "org.freedesktop.DBus.Peer", "Ping", "", &body);
	error = receive_reply(serial, &reply);
	check(error == 0 && reply.type == 2, "the bus stays after a malformed body");

	/* Last, a frame that is not D-Bus's: the server closes the bus. */
	error = send_raw("Bxxxxxxxxxxxxxxx", 16);
	check(error == 0, "a broken frame is sent");
	error = receive(&reply);
	check(error != 0, "the server hangs up on a broken frame");
}

/* Appends one byte. */
static void
put8(
	struct test_frame *frame,
	unsigned value)
{
	/* The byte, when it fits. */
	if (frame->used < TEST_FRAME_MAX) {
		frame->bytes[frame->used] = (unsigned char)value;
		frame->used++;
	}
}

/* Appends an aligned little-endian 32-bit value. */
static void
put32(
	struct test_frame *frame,
	uint32_t value)
{
	/* Aligned, then its four bytes. */
	pad(frame, 4);
	put8(frame, value & 0xffU);
	put8(frame, (value >> 8) & 0xffU);
	put8(frame, (value >> 16) & 0xffU);
	put8(frame, (value >> 24) & 0xffU);
}

/* Pads to an alignment with zero bytes. */
static void
pad(
	struct test_frame *frame,
	size_t alignment)
{
	/* Zero bytes up to the boundary. */
	while (frame->used % alignment != 0)
		put8(frame, 0);
}

/* Appends a string: its length, its bytes and its NUL. */
static void
put_string(
	struct test_frame *frame,
	const char *text)
{
	size_t index;

	/* The length, then the bytes with the NUL. */
	put32(frame, (uint32_t)strlen(text));
	for (index = 0; text[index] != '\0'; index++)
		put8(frame, (unsigned char)text[index]);
	put8(frame, 0);
}

/* Appends a signature: its one-byte length, its bytes and its NUL. */
static void
put_signature(
	struct test_frame *frame,
	const char *text)
{
	size_t index;

	/* The length, then the bytes with the NUL. */
	put8(frame, (unsigned)strlen(text));
	for (index = 0; text[index] != '\0'; index++)
		put8(frame, (unsigned char)text[index]);
	put8(frame, 0);
}

/* Appends one header field with a string-like value. */
static void
put_field(
	struct test_frame *frame,
	unsigned code,
	char type,
	const char *text)
{
	char signature[2];

	/* The structure's alignment, the code, the variant's type and value. */
	pad(frame, 8);
	put8(frame, code);
	signature[0] = type;
	signature[1] = '\0';
	put_signature(frame, signature);
	if (type == 'g')
		put_signature(frame, text);
	else
		put_string(frame, text);
}

/* Appends one header field with an unsigned value. */
static void
put_field_number(
	struct test_frame *frame,
	unsigned code,
	uint32_t number)
{
	/* The structure's alignment, the code, the variant's type and value. */
	pad(frame, 8);
	put8(frame, code);
	put_signature(frame, "u");
	put32(frame, number);
}

/* Starts an array: its length's place, and its first element's start after the element's alignment. */
static size_t
begin_array(
	struct test_frame *frame,
	size_t element_alignment,
	size_t *start)
{
	size_t length_at;

	/* The length's place, then the padding before the first element. */
	put32(frame, 0);
	length_at = frame->used - 4U;
	pad(frame, element_alignment);
	*start = frame->used;
	return length_at;
}

/* Ends an array: its elements' byte length in its place. */
static void
end_array(
	struct test_frame *frame,
	size_t length_at,
	size_t start)
{
	uint32_t length;

	/* The length. */
	length = (uint32_t)(frame->used - start);
	frame->bytes[length_at] = (unsigned char)(length & 0xffU);
	frame->bytes[length_at + 1U] = (unsigned char)((length >> 8) & 0xffU);
	frame->bytes[length_at + 2U] = (unsigned char)((length >> 16) & 0xffU);
	frame->bytes[length_at + 3U] = (unsigned char)((length >> 24) & 0xffU);
}

/* Starts a frame: its fixed header (lengths filled by frame_finish). */
static void
frame_header(
	struct test_frame *frame,
	unsigned type,
	uint32_t serial)
{
	/* Little-endian, the kind, no flags, version 1, the lengths later, the serial. */
	frame->used = 0;
	put8(frame, 'l');
	put8(frame, type);
	put8(frame, 0);
	put8(frame, 1);
	put32(frame, 0);
	put32(frame, serial);
	put32(frame, 0);
}

/* Ends the header's fields: their length in place, and the body's start after its padding. */
static size_t
frame_fields_end(
	struct test_frame *frame)
{
	uint32_t length;

	/* The fields' length, then the body's alignment. */
	length = (uint32_t)(frame->used - 16U);
	frame->bytes[12] = (unsigned char)(length & 0xffU);
	frame->bytes[13] = (unsigned char)((length >> 8) & 0xffU);
	frame->bytes[14] = (unsigned char)((length >> 16) & 0xffU);
	frame->bytes[15] = (unsigned char)((length >> 24) & 0xffU);
	pad(frame, 8);
	return frame->used;
}

/* Ends a frame: its body's length in place. */
static void
frame_finish(
	struct test_frame *frame,
	size_t body)
{
	uint32_t length;

	/* The body's length. */
	length = (uint32_t)(frame->used - body);
	frame->bytes[4] = (unsigned char)(length & 0xffU);
	frame->bytes[5] = (unsigned char)((length >> 8) & 0xffU);
	frame->bytes[6] = (unsigned char)((length >> 16) & 0xffU);
	frame->bytes[7] = (unsigned char)((length >> 24) & 0xffU);
}

/* Sends a method call to the server as the bus delivers it (with SENDER); returns its serial. */
static uint32_t
send_call(
	const char *sender,
	const char *path,
	const char *interface,
	const char *member,
	const char *signature,
	struct test_frame *body)
{
	static struct test_frame frame;
	size_t start;
	size_t index;

	/* The header: the object, the destination, the interface, the member, the sender, the signature. */
	test_serial++;
	frame_header(&frame, 1, test_serial);
	put_field(&frame, 1, 'o', path);
	put_field(&frame, 6, 's', "org.freedesktop.Notifications");
	put_field(&frame, 2, 's', interface);
	put_field(&frame, 3, 's', member);
	put_field(&frame, 7, 's', sender);
	if (signature[0] != '\0')
		put_field(&frame, 8, 'g', signature);
	start = frame_fields_end(&frame);

	/* The body, built aligned from 0, which the body's start (a multiple of 8) keeps. */
	for (index = 0; index < body->used; index++)
		put8(&frame, body->bytes[index]);
	frame_finish(&frame, start);
	(void)send_raw(frame.bytes, frame.used);
	return test_serial;
}

/* Sends bytes whole. */
static int
send_raw(
	const void *bytes,
	size_t size)
{
	const unsigned char *at;
	ssize_t sent;

	/* Until every byte is written. */
	at = bytes;
	while (size != 0) {
		sent = write(test_fd, at, size);
		if (sent <= 0)
			return EPIPE;
		at += sent;
		size -= (size_t)sent;
	}

	/* Succeeded: sent. */
	return 0;
}

/* Reads exactly size bytes. */
static int
read_exact(
	unsigned char *bytes,
	size_t size)
{
	ssize_t got;

	/* Until every byte has come. */
	while (size != 0) {
		got = read(test_fd, bytes, size);
		if (got <= 0)
			return EPIPE;
		bytes += got;
		size -= (size_t)got;
	}

	/* Succeeded: read. */
	return 0;
}

/* Receives one frame from the server and decodes its header's fields. */
static int
receive(
	struct test_message *message)
{
	size_t fields;
	size_t body;
	size_t cursor;
	size_t end;
	size_t length;
	unsigned code;
	char type;
	int error;

	/* The fixed header, then the fields and the body it announces. */
	memset(message, 0, sizeof(*message));
	error = read_exact(message->bytes, 16);
	if (error != 0)
		return error;
	if (message->bytes[0] != 'l')
		return EPROTO;
	body = get32(message->bytes + 4);
	fields = get32(message->bytes + 12);
	message->size = ((16U + fields + 7U) & ~(size_t)7U) + body;
	if (message->size > TEST_FRAME_MAX)
		return EOVERFLOW;
	error = read_exact(message->bytes + 16, message->size - 16U);
	if (error != 0)
		return error;
	message->type = message->bytes[1];
	message->body = (16U + fields + 7U) & ~(size_t)7U;
	message->cursor = message->body;

	/* The fields this test looks at: the error's name, the member, the serial answered, the signature. */
	cursor = 16;
	end = 16U + fields;
	while (cursor < end) {
		cursor = (cursor + 7U) & ~(size_t)7U;
		code = message->bytes[cursor];
		type = (char)message->bytes[cursor + 2U];
		cursor += 4U;
		if (type == 'u') {
			cursor = (cursor + 3U) & ~(size_t)3U;
			if (code == 5)
				message->reply_serial = get32(message->bytes + cursor);
			cursor += 4U;
		} else if (type == 'g') {
			length = message->bytes[cursor];
			if (code == 8)
				snprintf(message->signature, sizeof(message->signature), "%s", (const char *)message->bytes + cursor + 1U);
			cursor += 1U + length + 1U;
		} else {
			cursor = (cursor + 3U) & ~(size_t)3U;
			length = get32(message->bytes + cursor);
			if (code == 4)
				snprintf(message->error_name, sizeof(message->error_name), "%s", (const char *)message->bytes + cursor + 4U);
			if (code == 3)
				snprintf(message->member, sizeof(message->member), "%s", (const char *)message->bytes + cursor + 4U);
			cursor += 4U + length + 1U;
		}
	}

	/* Succeeded: decoded. */
	return 0;
}

/* Receives frames until the answer of a serial, which signals before it may precede. */
static int
receive_reply(
	uint32_t serial,
	struct test_message *message)
{
	int error;

	/* The answer, skipping anything else. */
	for (;;) {
		error = receive(message);
		if (error != 0)
			return error;
		if ((message->type == 2 || message->type == 3) && message->reply_serial == serial)
			return 0;
	}
}

/* Reads a little-endian 32-bit value. */
static uint32_t
get32(
	const unsigned char *bytes)
{
	/* The four bytes. */
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

/* Reads an aligned 32-bit value from the body. */
static int
read_u32(
	struct test_message *message,
	uint32_t *value)
{
	/* Aligned, inside the frame. */
	message->cursor = (message->cursor + 3U) & ~(size_t)3U;
	if (message->cursor + 4U > message->size)
		return EPROTO;
	*value = get32(message->bytes + message->cursor);
	message->cursor += 4U;
	return 0;
}

/* Reads a string from the body. */
static int
read_string(
	struct test_message *message,
	const char **text)
{
	uint32_t length;
	int error;

	/* The length, then the bytes and the NUL inside the frame. */
	error = read_u32(message, &length);
	if (error != 0)
		return error;
	if (message->cursor + length + 1U > message->size)
		return EPROTO;
	*text = (const char *)message->bytes + message->cursor;
	message->cursor += length + 1U;
	return 0;
}

/* Builds Notify's body: susssasa{sv}i, with the default action, an urgency and other hints to step over when asked. */
static void
notify_body(
	struct test_frame *body,
	const char *app,
	uint32_t replaces,
	const char *summary,
	const char *text,
	int with_default,
	int urgency,
	int extra_hints)
{
	size_t length_at;
	size_t start;
	size_t inner_at;
	size_t inner_start;

	/* The application, the replaced number, the icon, the summary and the body. */
	body->used = 0;
	put_string(body, app);
	put32(body, replaces);
	put_string(body, "dialog-information");
	put_string(body, summary);
	put_string(body, text);

	/* The actions: "default" and its label, then another the server does not keep. */
	length_at = begin_array(body, 4, &start);
	if (with_default) {
		put_string(body, "default");
		put_string(body, "Open");
	}

	/* Another action, which the server leaves out. */
	put_string(body, "reply");
	put_string(body, "Reply");
	end_array(body, length_at, start);

	/* The hints. */
	length_at = begin_array(body, 8, &start);
	if (extra_hints) {
		/* A string hint. */
		pad(body, 8);
		put_string(body, "desktop-entry");
		put_signature(body, "s");
		put_string(body, "test");

		/* An image, (iiibiiay), stepped over whole. */
		pad(body, 8);
		put_string(body, "image-data");
		put_signature(body, "(iiibiiay)");
		pad(body, 8);
		put32(body, 1);
		put32(body, 1);
		put32(body, 4);
		put32(body, 1);
		put32(body, 8);
		put32(body, 4);
		inner_at = begin_array(body, 1, &inner_start);
		put8(body, 1);
		put8(body, 2);
		put8(body, 3);
		put8(body, 4);
		end_array(body, inner_at, inner_start);

		/* A nested variant. */
		pad(body, 8);
		put_string(body, "x-nested");
		put_signature(body, "v");
		put_signature(body, "u");
		put32(body, 7);
	}

	/* The urgency, a byte. */
	pad(body, 8);
	put_string(body, "urgency");
	put_signature(body, "y");
	put8(body, (unsigned)urgency);
	end_array(body, length_at, start);

	/* The timeout. */
	put32(body, 0xffffffffU);
}

/* Calls Notify and gives the number it answered (0 for an error, which reply holds). */
static uint32_t
call_notify(
	const char *sender,
	const char *app,
	uint32_t replaces,
	const char *summary,
	const char *text,
	int with_default,
	int urgency,
	int extra_hints,
	struct test_message *reply)
{
	static struct test_frame body;
	uint32_t serial;
	uint32_t id;
	int error;

	/* The call, then its answer. */
	notify_body(&body, app, replaces, summary, text, with_default, urgency, extra_hints);
	serial = send_call(sender, TEST_PATH, TEST_INTERFACE, "Notify", "susssasa{sv}i", &body);
	error = receive_reply(serial, reply);
	if (error != 0 || reply->type != 2)
		return 0;

	/* The number. */
	id = 0;
	error = read_u32(reply, &id);
	if (error != 0)
		return 0;
	return id;
}

/* Receives the next frame and checks it is a signal of the member with the id (and reason, or "default" for ActionInvoked). */
static int
expect_signal(
	const char *member,
	uint32_t id,
	uint32_t reason)
{
	struct test_message message;
	const char *key;
	uint32_t got_id;
	uint32_t got_reason;
	int is_action;
	int matched;
	int error;

	/* The next frame. */
	error = receive(&message);
	if (error != 0)
		return error;
	matched = same(message.member, member);
	if (message.type != 4 || !matched)
		return EPROTO;

	/* Its id. */
	error = read_u32(&message, &got_id);
	if (error != 0 || got_id != id)
		return EPROTO;

	/* ActionInvoked's key. */
	is_action = same(member, "ActionInvoked");
	if (is_action) {
		error = read_string(&message, &key);
		if (error != 0)
			return error;
		matched = same(key, "default");
		if (!matched)
			return EPROTO;
		return 0;
	}

	/* NotificationClosed's reason. */
	error = read_u32(&message, &got_reason);
	if (error != 0 || got_reason != reason)
		return EPROTO;
	return 0;
}
