/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises the independent client through real sockets and an independent peer.
 */

#include <wayland/wayland-client.h>
#include <wayland/zed-gpu-buffer-v1-client-protocol.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

/* Records externally visible callback counts for one application queue. */
struct peer_state {
	unsigned globals;
	unsigned done;
	unsigned descriptors;
	int survivor;
};

/* Coordinates a reader that must wait until the main thread cancels its hold. */
struct reader_state {
	struct wl_display *display;
	pthread_mutex_t mutex;
	pthread_cond_t condition;
	int entered;
	int done;
	int error;
};

static void global_event(void *data, struct wl_registry *registry, uint32_t name, const char *interface_name, uint32_t version);
static void removed_event(void *data, struct wl_registry *registry, uint32_t name);
static void done_event(void *data, struct wl_callback *callback, uint32_t serial);
static int fd_event(const void *implementation, void *target, uint32_t opcode, const struct wl_message *message, union wl_argument *arguments);
static void send_bytes(int fd, const void *bytes, size_t size);
static void recv_bytes(int fd, void *bytes, size_t size);
static void send_global(int fd, uint32_t object_id);
static void send_done(int fd, uint32_t object_id, uint32_t serial);
static void read_once(struct wl_display *display);
static void *reader_main(void *argument);
static void check_queues_and_lifetimes(void);
static void check_descriptor_transport(void);
static void check_reader_barrier(void);
static void check_errors(void);

/*
 * Runs meaningful client transport, ownership and event-loop acceptance cases.
 */
int
main(void)
{
	/* Allows pipe ownership checks to observe EPIPE rather than terminating. */
	signal(SIGPIPE, SIG_IGN);

	/* Exercises real production marshalling, parsing and callback behavior. */
	check_queues_and_lifetimes();
	check_descriptor_transport();
	check_reader_barrier();
	check_errors();

	/* Succeeded: reports the covered protocol and ownership boundaries. */
	puts("PASS Wayland client: queues, fragmented wire, tombstones, fd ownership, delayed rights, partial sends, reader barrier, protocol errors");
	return 0;
}

/* Receives one independently encoded registry advertisement. */
static void
global_event(
	void *data,
	struct wl_registry *registry,
	uint32_t name,
	const char *interface_name,
	uint32_t version)
{
	struct peer_state *state;

	(void)registry;

	/* Checks actual decoded string/scalar arguments from the peer's wire bytes. */
	state = data;
	assert(name == 41);
	assert(strcmp(interface_name, "wl_compositor") == 0);
	assert(version == 4);
	state->globals++;

	/* Succeeded: this queue alone observed the registry advertisement. */
	return;
}

/* Verifies that no unrequested global-removal event is delivered. */
static void
removed_event(
	void *data,
	struct wl_registry *registry,
	uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;

	/* No test peer emits a removal, so reaching this callback is a protocol defect. */
	assert(0);
	return;
}

/* Destroys a callback reentrantly after its wire map may already be retired. */
static void
done_event(
	void *data,
	struct wl_callback *callback,
	uint32_t serial)
{
	struct peer_state *state;

	/* Checks the independent peer's payload and records exactly-once delivery. */
	state = data;
	assert(serial == 912);
	state->done++;
	wl_callback_destroy(callback);

	/* Succeeded: event infrastructure may now release its final proxy hold. */
	return;
}

/* Receives a descriptor through the generic extension-dispatch contract. */
static int
fd_event(
	const void *implementation,
	void *target,
	uint32_t opcode,
	const struct wl_message *message,
	union wl_argument *arguments)
{
	struct peer_state *state;
	char byte;
	int flags;
	ssize_t got;

	(void)implementation;
	(void)message;

	/* Queries caller state through the public API to exercise reentrancy. */
	state = wl_proxy_get_user_data(target);
	assert(opcode == 0);
	assert(arguments[0].u == 77);
	flags = fcntl(arguments[1].h, F_GETFD);
	assert(flags >= 0);
	assert((flags & FD_CLOEXEC) != 0);
	got = read(arguments[1].h, &byte, 1);
	assert(got == 1);
	assert(byte == 'X');
	close(arguments[1].h);

	/* Reuses the closed numeric fd to detect a later double-close by dispatch. */
	state->survivor = open("/dev/null", O_RDONLY);
	assert(state->survivor >= 0);
	state->descriptors++;

	/* Succeeded: the callback consumed ownership of the received descriptor. */
	return 0;
}

/* Sends a complete independent peer byte range despite ordinary interruptions. */
static void
send_bytes(
	int fd,
	const void *bytes,
	size_t size)
{
	const unsigned char *cursor;
	ssize_t sent;

	/* Writes exactly the supplied fixture bytes without library marshalling. */
	cursor = bytes;
	while (size != 0) {
		sent = send(fd, cursor, size, MSG_NOSIGNAL);
		assert(sent > 0);
		cursor += sent;
		size -= (size_t)sent;
	}

	/* Succeeded: the peer has supplied every fixture byte. */
	return;
}

/* Receives the exact request extent expected by an independent wire fixture. */
static void
recv_bytes(
	int fd,
	void *bytes,
	size_t size)
{
	unsigned char *cursor;
	ssize_t got;

	/* Accumulates the requested fixture extent without assuming packet boundaries. */
	cursor = bytes;
	while (size != 0) {
		got = recv(fd, cursor, size, 0);
		assert(got > 0);
		cursor += got;
		size -= (size_t)got;
	}

	/* Succeeded: the peer owns the complete expected request bytes. */
	return;
}

/* Emits a registry global using protocol words and an independently padded string. */
static void
send_global(
	int fd,
	uint32_t object_id)
{
	uint32_t words[9];

	/* Encodes global(name=41, interface=wl_compositor, version=4) directly. */
	memset(words, 0, sizeof(words));
	words[0] = object_id;
	words[1] = 36U << 16;
	words[2] = 41;
	words[3] = 14;
	memcpy(&words[4], "wl_compositor", 14);
	words[8] = 4;
	send_bytes(fd, words, sizeof(words));

	/* Succeeded: no client encoder participated in constructing this event. */
	return;
}

/* Emits a callback and its display acknowledgement in one stream write. */
static void
send_done(
	int fd,
	uint32_t object_id,
	uint32_t serial)
{
	uint32_t words[6];

	/* Acknowledges deletion before the application dispatches the done callback. */
	words[0] = object_id;
	words[1] = 12U << 16;
	words[2] = serial;
	words[3] = 1;
	words[4] = (12U << 16) | 1U;
	words[5] = object_id;
	send_bytes(fd, words, sizeof(words));

	/* Succeeded: the event reference must outlive wire-map retirement. */
	return;
}

/* Reads available fixture bytes through the public reader preparation API. */
static void
read_once(
	struct wl_display *display)
{
	int error;

	/* Performs one complete prepared-reader cycle without dispatching events. */
	error = wl_display_prepare_read(display);
	assert(error == 0);
	error = wl_display_read_events(display);
	assert(error == 0);

	/* Succeeded: selected queues now own all complete decoded messages. */
	return;
}

/* Validates queue isolation, wrapper inheritance and delayed object retirement. */
static void
check_queues_and_lifetimes(void)
{
	static const struct wl_registry_listener registry_listener = {
		global_event, removed_event
	};
	static const struct wl_callback_listener callback_listener = {
		done_event
	};
	struct wl_display *display;
	struct wl_event_queue *queue;
	struct wl_proxy *wrapper;
	struct wl_registry *ordinary;
	struct wl_registry *private;
	struct wl_callback *callback;
	struct peer_state main_state;
	struct peer_state private_state;
	uint32_t requests[6];
	uint32_t event[3];
	uint32_t object_id;
	int sockets[2];
	int error;
	int count;
	unsigned index;

	/* Establishes two independently observed event queues on one real stream. */
	memset(&main_state, 0, sizeof(main_state));
	memset(&private_state, 0, sizeof(private_state));
	error = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
	assert(error == 0);
	display = wl_display_connect_to_fd(sockets[0]);
	assert(display != NULL);
	queue = wl_display_create_queue_with_name(display, "wsi-test");
	assert(queue != NULL);
	wrapper = wl_proxy_create_wrapper(display);
	assert(wrapper != NULL);
	wl_proxy_set_queue(wrapper, queue);
	ordinary = wl_display_get_registry(display);
	private = wl_display_get_registry((struct wl_display *)wrapper);
	assert(ordinary != NULL);
	assert(private != NULL);
	assert(wl_proxy_get_queue((struct wl_proxy *)private) == queue);
	assert(wl_proxy_get_queue((struct wl_proxy *)ordinary) != queue);
	wl_proxy_wrapper_destroy(wrapper);
	error = wl_registry_add_listener(ordinary, &registry_listener, &main_state);
	assert(error == 0);
	error = wl_registry_add_listener(private, &registry_listener, &private_state);
	assert(error == 0);
	count = wl_display_flush(display);
	assert(count == 24);
	recv_bytes(sockets[1], requests, sizeof(requests));
	assert(requests[0] == 1);
	assert(requests[1] == ((12U << 16) | 1U));
	assert(requests[3] == 1);
	assert(requests[4] == ((12U << 16) | 1U));

	/* Reading a private event must leave the application's queue callbacks idle. */
	send_global(sockets[1], requests[5]);
	send_global(sockets[1], requests[2]);
	read_once(display);
	count = wl_display_dispatch_queue_pending(display, queue);
	assert(count == 1);
	assert(private_state.globals == 1);
	assert(main_state.globals == 0);
	error = wl_display_prepare_read(display);
	assert(error == -1);
	assert(errno == EAGAIN);
	count = wl_display_dispatch_pending(display);
	assert(count == 1);
	assert(main_state.globals == 1);

	/* A destroyed callback suppresses an event already queued for its generation. */
	callback = wl_display_sync(display);
	assert(callback != NULL);
	error = wl_callback_add_listener(callback, &callback_listener, &main_state);
	assert(error == 0);
	count = wl_display_flush(display);
	assert(count == 12);
	recv_bytes(sockets[1], requests, 12);
	object_id = requests[2];
	send_done(sockets[1], object_id, 912);
	read_once(display);
	wl_callback_destroy(callback);
	count = wl_display_dispatch_pending(display);
	assert(count == 1);
	assert(main_state.done == 0);

	/* A callback received in fragments cannot dispatch before its final word. */
	callback = wl_display_sync(display);
	assert(callback != NULL);
	error = wl_callback_add_listener(callback, &callback_listener, &main_state);
	assert(error == 0);
	count = wl_display_flush(display);
	assert(count == 12);
	recv_bytes(sockets[1], requests, 12);
	event[0] = requests[2];
	event[1] = 12U << 16;
	event[2] = 912;
	send_bytes(sockets[1], event, 3);
	read_once(display);
	count = wl_display_dispatch_pending(display);
	assert(count == 0);
	send_bytes(sockets[1], (unsigned char *)event + 3, 9);
	read_once(display);
	count = wl_display_dispatch_pending(display);
	assert(count == 1);
	assert(main_state.done == 1);
	event[0] = 1;
	event[1] = (12U << 16) | 1U;
	event[2] = requests[2];
	send_bytes(sockets[1], event, sizeof(event));
	read_once(display);

	/* Exercises substantially more objects than a small fixed proxy array allows. */
	for (index = 0; index < 4096; index++) {
		callback = wl_display_sync(display);
		assert(callback != NULL);
		wl_callback_destroy(callback);
		count = wl_display_flush(display);
		assert(count == 12);
		recv_bytes(sockets[1], requests, 12);
		event[2] = requests[2];
		send_bytes(sockets[1], event, sizeof(event));
		read_once(display);
	}

	/* Ends every caller proxy before destroying queues and the connection. */
	wl_registry_destroy(private);
	wl_registry_destroy(ordinary);
	wl_event_queue_destroy(queue);
	wl_display_disconnect(display);
	close(sockets[1]);

	/* Succeeded: queue isolation and object-generation lifetimes held. */
	return;
}

/* Validates real fd duplication, transfer, partial sends and incoming ownership. */
static void
check_descriptor_transport(void)
{
	static const struct wl_message events[] = {
		{ "descriptor", "uh", NULL }
	};
	static const struct wl_interface descriptor_interface = {
		"q309_descriptor", 1, 0, NULL, 1, events
	};
	struct wl_display *display;
	struct wl_registry *registry;
	struct zed_gpu_buffer_v1 *factory;
	struct wl_buffer *buffer;
	struct wl_proxy *receiver;
	struct wl_array metadata;
	struct peer_state state;
	struct msghdr message;
	struct iovec vector;
	struct cmsghdr *control;
	union {
		struct cmsghdr alignment;
		unsigned char bytes[CMSG_SPACE(8 * sizeof(int))];
	} ancillary;
	unsigned char bytes[65536];
	uint32_t words[3];
	int sockets[2];
	int pipes[2];
	int received_fd;
	int copied_fd;
	int capacity;
	int error;
	int sent;
	int flags;
	ssize_t got;
	size_t total;
	size_t descriptors;
	char byte;

	/* Binds the private factory using the same standard registry operation as WSI. */
	memset(&state, 0, sizeof(state));
	error = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
	assert(error == 0);
	display = wl_display_connect_to_fd(sockets[0]);
	assert(display != NULL);
	registry = wl_display_get_registry(display);
	assert(registry != NULL);
	factory = wl_registry_bind(registry, 7, &zed_gpu_buffer_v1_interface, 1);
	assert(factory != NULL);
	receiver = wl_registry_bind(registry, 8, &descriptor_interface, 1);
	assert(receiver != NULL);
	error = wl_proxy_add_dispatcher(receiver, fd_event, NULL, &state);
	assert(error == 0);
	sent = wl_display_flush(display);
	assert(sent > 0);
	recv_bytes(sockets[1], bytes, (size_t)sent);

	/* Forces a rights-bearing large message to span several socket writes. */
	capacity = 1024;
	error = setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &capacity, sizeof(capacity));
	assert(error == 0);
	memset(bytes, 0x6b, 60000);
	metadata.size = 60000;
	metadata.alloc = 60000;
	metadata.data = bytes;
	error = pipe(pipes);
	assert(error == 0);
	buffer = zed_gpu_buffer_v1_create_buffer(factory, pipes[0], &metadata);
	assert(buffer != NULL);
	close(pipes[0]);
	got = write(pipes[1], "R", 1);
	assert(got == 1);
	sent = wl_display_flush(display);
	assert(sent == -1);
	assert(errno == EAGAIN || errno == EWOULDBLOCK);

	/* Receives bytes and counts actual delivered rights over all partial writes. */
	total = 0;
	descriptors = 0;
	received_fd = -1;
	while (total < 60016) {
		memset(&message, 0, sizeof(message));
		memset(&ancillary, 0, sizeof(ancillary));
		vector.iov_base = bytes + total;
		vector.iov_len = sizeof(bytes) - total;
		message.msg_iov = &vector;
		message.msg_iovlen = 1;
		message.msg_control = ancillary.bytes;
		message.msg_controllen = sizeof(ancillary.bytes);
		got = recvmsg(sockets[1], &message, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
		assert(got > 0);
		total += (size_t)got;

		/* A descriptor is attached exactly once, despite the packet's unsent suffix. */
		if (message.msg_controllen >= CMSG_LEN(sizeof(int))) {
			control = (struct cmsghdr *)ancillary.bytes;
			assert(control->cmsg_type == SCM_RIGHTS);
			memcpy(&received_fd, CMSG_DATA(control), sizeof(received_fd));
			descriptors++;
		}

		sent = wl_display_flush(display);
		assert(sent >= 0 || errno == EAGAIN || errno == EWOULDBLOCK);
	}

	assert(total == 60016);
	assert(descriptors == 1);
	memcpy(words, bytes, sizeof(words));
	assert(words[1] == ((60016U << 16) | 1U));
	got = read(received_fd, &byte, 1);
	assert(got == 1);
	assert(byte == 'R');
	close(received_fd);
	close(pipes[1]);

	/* Sends an fd-bearing event whose descriptor is consumed by a custom binding. */
	error = pipe(pipes);
	assert(error == 0);
	got = write(pipes[1], "X", 1);
	assert(got == 1);
	words[0] = wl_proxy_get_id(receiver);
	words[1] = 12U << 16;
	words[2] = 77;
	memset(&message, 0, sizeof(message));
	memset(&ancillary, 0, sizeof(ancillary));
	vector.iov_base = words;
	vector.iov_len = sizeof(words);
	message.msg_iov = &vector;
	message.msg_iovlen = 1;
	control = (struct cmsghdr *)ancillary.bytes;
	control->cmsg_level = SOL_SOCKET;
	control->cmsg_type = SCM_RIGHTS;
	control->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(control), &pipes[0], sizeof(int));
	message.msg_control = ancillary.bytes;
	message.msg_controllen = CMSG_SPACE(sizeof(int));
	got = sendmsg(sockets[1], &message, MSG_NOSIGNAL);
	assert(got == 12);
	close(pipes[0]);
	close(pipes[1]);
	read_once(display);
	sent = wl_display_dispatch_pending(display);
	assert(sent == 1);
	assert(state.descriptors == 1);
	flags = fcntl(state.survivor, F_GETFD);
	assert(flags >= 0);
	close(state.survivor);

	/* Complete message bytes wait for a later recvmsg carrying their ordered fd. */
	error = pipe(pipes);
	assert(error == 0);
	got = write(pipes[1], "X", 1);
	assert(got == 1);
	send_bytes(sockets[1], words, sizeof(words));
	read_once(display);
	sent = wl_display_dispatch_pending(display);
	assert(sent == 0);
	assert(state.descriptors == 1);
	assert(wl_display_get_error(display) == 0);

	/* Rights may travel with the first byte of a later, still incomplete message. */
	byte = 0;
	vector.iov_base = &byte;
	vector.iov_len = 1;
	memcpy(CMSG_DATA(control), &pipes[0], sizeof(int));
	got = sendmsg(sockets[1], &message, MSG_NOSIGNAL);
	assert(got == 1);
	close(pipes[0]);
	close(pipes[1]);
	read_once(display);
	sent = wl_display_dispatch_pending(display);
	assert(sent == 1);
	assert(state.descriptors == 2);
	flags = fcntl(state.survivor, F_GETFD);
	assert(flags >= 0);
	close(state.survivor);

	/* A queued but never-flushed request retains its fd until disconnect. */
	error = pipe(pipes);
	assert(error == 0);
	metadata.size = 64;
	buffer = zed_gpu_buffer_v1_create_buffer(factory, pipes[0], &metadata);
	assert(buffer != NULL);
	copied_fd = pipes[0];
	close(pipes[0]);
	flags = fcntl(copied_fd, F_GETFD);
	assert(flags == -1);
	wl_proxy_destroy(receiver);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
	got = write(pipes[1], "Y", 1);
	assert(got == -1);
	assert(errno == EPIPE);
	close(pipes[1]);
	close(sockets[1]);

	/* Succeeded: sender, queue, receiver and callback ownership are independent. */
	return;
}

/* Waits in the actual coordinated reader barrier until another reader cancels. */
static void *
reader_main(
	void *argument)
{
	struct reader_state *state;
	int error;

	/* Registers this thread before allowing the main thread to cancel its hold. */
	state = argument;
	error = wl_display_prepare_read(state->display);
	assert(error == 0);
	pthread_mutex_lock(&state->mutex);
	state->entered = 1;
	pthread_cond_broadcast(&state->condition);
	pthread_mutex_unlock(&state->mutex);
	error = wl_display_read_events(state->display);
	pthread_mutex_lock(&state->mutex);
	state->error = error;
	state->done = 1;
	pthread_cond_broadcast(&state->condition);
	pthread_mutex_unlock(&state->mutex);

	/* Succeeded: the canceled generation required no socket readiness. */
	return NULL;
}

/* Validates read/cancel coordination without busy waiting or socket traffic. */
static void
check_reader_barrier(void)
{
	struct reader_state state;
	pthread_t thread;
	int sockets[2];
	int error;

	/* Keeps one preparation outstanding while a second thread enters read_events. */
	memset(&state, 0, sizeof(state));
	error = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
	assert(error == 0);
	state.display = wl_display_connect_to_fd(sockets[0]);
	assert(state.display != NULL);
	pthread_mutex_init(&state.mutex, NULL);
	pthread_cond_init(&state.condition, NULL);
	error = wl_display_prepare_read(state.display);
	assert(error == 0);
	error = pthread_create(&thread, NULL, reader_main, &state);
	assert(error == 0);
	pthread_mutex_lock(&state.mutex);

	/* Waits for the other reader to prepare before canceling this intention. */
	while (!state.entered)
		pthread_cond_wait(&state.condition, &state.mutex);

	assert(state.done == 0);
	pthread_mutex_unlock(&state.mutex);
	wl_display_cancel_read(state.display);
	pthread_join(thread, NULL);
	assert(state.done == 1);
	assert(state.error == 0);

	/* Reuses the barrier after cancellation and proves it does not underflow. */
	error = wl_display_prepare_read(state.display);
	assert(error == 0);
	wl_display_cancel_read(state.display);
	wl_display_cancel_read(state.display);
	error = wl_display_prepare_read(state.display);
	assert(error == 0);
	wl_display_cancel_read(state.display);
	wl_display_disconnect(state.display);
	close(sockets[1]);
	pthread_cond_destroy(&state.condition);
	pthread_mutex_destroy(&state.mutex);

	/* Succeeded: read intentions neither race socket consumption nor deadlock. */
	return;
}

/* Validates malformed-size rejection, sticky errors and connection loss. */
static void
check_errors(void)
{
	struct wl_display *display;
	uint32_t words[2];
	int sockets[2];
	int error;

	/* Supplies an invalid aligned-message size directly from the independent peer. */
	error = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
	assert(error == 0);
	display = wl_display_connect_to_fd(sockets[0]);
	assert(display != NULL);
	words[0] = 1;
	words[1] = 7U << 16;
	send_bytes(sockets[1], words, sizeof(words));
	error = wl_display_prepare_read(display);
	assert(error == 0);
	error = wl_display_read_events(display);
	assert(error == -1);
	assert(errno == EPROTO);
	error = wl_display_get_error(display);
	assert(error == EPROTO);
	error = wl_display_flush(display);
	assert(error == -1);
	assert(errno == EPROTO);
	wl_display_disconnect(display);
	close(sockets[1]);

	/* A closed compositor must become an observable surface/connection loss. */
	error = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
	assert(error == 0);
	display = wl_display_connect_to_fd(sockets[0]);
	assert(display != NULL);
	close(sockets[1]);
	error = wl_display_prepare_read(display);
	assert(error == 0);
	error = wl_display_read_events(display);
	assert(error == -1);
	assert(errno == EPIPE);
	wl_display_disconnect(display);

	/* Succeeded: invalid and disconnected streams cannot be reused silently. */
	return;
}
