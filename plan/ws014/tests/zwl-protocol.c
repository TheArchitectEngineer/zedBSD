/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Real compositor wire and ownership code; only GPU ioctl completion is simulated. */
#define main zwl_service_main
#include "../../../userland/base/zwl/main.c"
#undef main
#include <assert.h>
#include <stdarg.h>
#include <sys/ioctl.h>

static struct gpu_image_descriptor authoritative;
static unsigned resources[256];
static unsigned next_resource;
static unsigned destroyed;
static unsigned released;
static unsigned imported;
static unsigned presented;
static uint64_t scanning;
static int fail_present;
static int imported_fd;
static int imported_cloexec;

/* The test double enforces independent imports and hardware-before-free ordering. */
int
ioctl(
	int descriptor,
	unsigned long operation,
	...)
{
	struct gpu_resource_import *image;
	struct gpu_resource_destroy *destroy;
	struct gpu_display_claim *claim;
	struct gpu_display_present *present;
	struct gpu_display_info *display;
	struct gpu_display_mode *mode;
	struct gpu_info *information;
	void *argument;
	va_list arguments;
	int flags;

	/* These are real open descriptors even though the device completion is controlled. */
	flags = fcntl(descriptor, F_GETFD);
	assert(flags >= 0);
	va_start(arguments, operation);
	argument = va_arg(arguments, void *);
	va_end(arguments);
	if (operation == GPU_GET_INFO) {
		information = argument;
		information->capabilities = GPU_CAP_SHARE | GPU_CAP_DISPLAY;
		strcpy(information->driver_name, "test-completion");
		return 0;
	}
	if (operation == GPU_DISPLAY_QUERY) {
		display = argument;
		display->flags = GPU_DISPLAY_CONNECTED | GPU_DISPLAY_BLOB;
		display->display_id = 1;
		display->generation = 7;
		display->refresh_millihz = 50000;
		return 0;
	}
	if (operation == GPU_DISPLAY_MODE) {
		mode = argument;
		assert(mode->refresh_millihz == 0);
		mode->refresh_millihz = 50000;
		return 0;
	}
	if (operation == GPU_RESOURCE_IMPORT) {
		image = argument;
		assert(image->handle == 0 && image->resource_id == 0);
		assert(image->image.version == 0 && image->flags == 0);
		imported_fd = image->fd;
		flags = fcntl(image->fd, F_GETFD);
		assert(flags >= 0);
		imported_cloexec = flags & FD_CLOEXEC;
		assert(next_resource < 255);
		next_resource++;
		resources[next_resource] = 1;
		image->handle = next_resource;
		image->resource_id = 100 + next_resource;
		image->image = authoritative;
		imported++;
		return 0;
	}
	if (operation == GPU_RESOURCE_DESTROY) {
		destroy = argument;
		assert(destroy->handle > 0 && destroy->handle <= next_resource);
		assert(resources[destroy->handle] == 1);
		assert(scanning != destroy->handle);
		resources[destroy->handle] = 0;
		destroyed++;
		return 0;
	}
	if (operation == GPU_DISPLAY_CLAIM) {
		claim = argument;
		assert(claim->lease == 0 && claim->plane_index == 0);
		claim->lease = 91;
		return 0;
	}
	if (operation == GPU_DISPLAY_PRESENT) {
		present = argument;
		assert(present->flags == (GPU_DISPLAY_PRESENT_FIFO | GPU_DISPLAY_PRESENT_BLOB));
		assert(present->refresh_millihz == 50000);
		assert(resources[present->handle] == 1);
		if (fail_present) {
			errno = EIO;
			return -1;
		}
		scanning = present->handle;
		presented++;
		present->sequence = presented;
		return 0;
	}
	if (operation == GPU_DISPLAY_RELEASE) {
		scanning = 0;
		released++;
		return 0;
	}
	assert(0 && "unexpected ioctl");
	return -1;
}

/* Initialize each isolated server with a genuinely owned consumer descriptor. */
static void
server_init(
	struct zwl_server *server)
{
	int error;

	memset(server, 0, sizeof(*server));
	server->listener = -1;
	server->gpu = -1;
	server->gpu_path = "/dev/null";
	server->width = 320;
	server->height = 240;
	server->timeout_ms = 20;
	error = zwl_gpu_open(server);
	assert(error == 0);
}

/* Give each protocol namespace a real nonblocking Unix stream. */
static struct zwl_client *
client_new(
	struct zwl_server *server,
	int *peer)
{
	struct zwl_client *client;
	struct zwl_object *object;
	int pair[2];
	int error;

	error = socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pair);
	assert(error == 0);
	client = calloc(1, sizeof(*client));
	assert(client != NULL);
	client->server = server;
	client->number = ++server->client_serial;
	client->fd = pair[0];
	*peer = pair[1];
	client->next = server->clients;
	server->clients = client;
	object = zwl_create(client, 1, ZWL_DISPLAY, 1);
	assert(object != NULL);
	return client;
}

/* Send canonical bytes with SCM_RIGHTS; no fd-valued word exists in the payload. */
static int
request(
	struct zwl_client *client,
	int peer,
	uint32_t id,
	uint32_t opcode,
	const void *payload,
	size_t bytes,
	int right)
{
	unsigned char frame[512];
	union {
		struct cmsghdr alignment;
		unsigned char bytes[CMSG_SPACE(sizeof(int))];
	} control;
	struct msghdr message;
	struct iovec vector;
	struct cmsghdr *header;
	uint32_t words[2];
	ssize_t sent;
	int error;

	assert(bytes + 8 <= sizeof(frame));
	words[0] = id;
	words[1] = (uint32_t)((bytes + 8) << 16) | opcode;
	memcpy(frame, words, 8);
	if (bytes != 0)
		memcpy(frame + 8, payload, bytes);
	memset(&message, 0, sizeof(message));
	memset(&control, 0, sizeof(control));
	vector.iov_base = frame;
	vector.iov_len = bytes + 8;
	message.msg_iov = &vector;
	message.msg_iovlen = 1;
	if (right >= 0) {
		message.msg_control = control.bytes;
		message.msg_controllen = sizeof(control);
		header = CMSG_FIRSTHDR(&message);
		header->cmsg_len = CMSG_LEN(sizeof(int));
		header->cmsg_level = SOL_SOCKET;
		header->cmsg_type = SCM_RIGHTS;
		memcpy(CMSG_DATA(header), &right, sizeof(right));
	}
	sent = sendmsg(peer, &message, MSG_NOSIGNAL);
	assert(sent == (ssize_t)(bytes + 8));
	error = zwl_read(client);
	return error;
}

/* Bind through the real registry method, including standard string alignment. */
static void
bind_interface(
	struct zwl_client *client,
	int peer,
	uint32_t name,
	const char *interface,
	uint32_t version,
	uint32_t id)
{
	unsigned char payload[128];
	uint32_t length;
	size_t offset;
	int error;

	memset(payload, 0, sizeof(payload));
	length = (uint32_t)strlen(interface) + 1;
	memcpy(payload, &name, 4);
	memcpy(payload + 4, &length, 4);
	memcpy(payload + 8, interface, length);
	offset = 8 + ((length + 3U) & ~3U);
	memcpy(payload + offset, &version, 4);
	memcpy(payload + offset + 4, &id, 4);
	error = request(client, peer, 2, 0, payload, offset + 8, -1);
	assert(error == 0);
}

/* Establish one xdg toplevel using only ordinary request dispatch and events. */
static struct zwl_object *
configure_surface(
	struct zwl_client *client,
	int peer)
{
	struct zwl_object *surface;
	uint32_t words[2];
	int error;

	words[0] = 2;
	error = request(client, peer, 1, 1, words, 4, -1);
	assert(error == 0);
	bind_interface(client, peer, 1, "wl_compositor", 4, 3);
	bind_interface(client, peer, 2, "xdg_wm_base", 1, 4);
	bind_interface(client, peer, 3, "zed_gpu_buffer_v1", 1, 5);
	bind_interface(client, peer, 4, "wl_output", 2, 6);
	words[0] = 10;
	error = request(client, peer, 3, 0, words, 4, -1);
	assert(error == 0);
	words[0] = 11;
	words[1] = 10;
	error = request(client, peer, 4, 2, words, 8, -1);
	assert(error == 0);
	words[0] = 12;
	error = request(client, peer, 11, 1, words, 4, -1);
	assert(error == 0);
	error = request(client, peer, 10, 6, NULL, 0, -1);
	assert(error == 0);
	surface = zwl_find(client, 10);
	assert(surface != NULL && surface->configured && !surface->acknowledged);
	words[0] = surface->configure_serial;
	error = request(client, peer, 11, 4, words, 4, -1);
	assert(error == 0 && surface->acknowledged);
	return surface;
}

/* Import through the real ancillary parser and private nha factory request. */
static struct zwl_object *
create_buffer(
	struct zwl_client *client,
	int peer,
	uint32_t id)
{
	unsigned char payload[72];
	uint32_t length;
	struct zwl_object *buffer;
	int right;
	int error;
	int flags;

	right = open("/dev/null", O_RDONLY | O_CLOEXEC);
	assert(right >= 0);
	length = sizeof(authoritative);
	memcpy(payload, &id, 4);
	memcpy(payload + 4, &length, 4);
	memcpy(payload + 8, &authoritative, sizeof(authoritative));
	error = request(client, peer, 5, 1, payload, sizeof(payload), right);
	assert(error == 0);
	assert(imported_cloexec != 0 && imported_fd != right);
	flags = fcntl(imported_fd, F_GETFD);
	assert(flags < 0 && errno == EBADF);
	close(right);
	buffer = zwl_find(client, id);
	assert(buffer != NULL && buffer->image.handle != 0);
	return buffer;
}

/* Count canonical queued events without relying on the compositor's state fields. */
static unsigned
count_events(
	struct zwl_client *client,
	uint32_t id,
	uint32_t opcode)
{
	struct zwl_packet *packet;
	uint32_t words[2];
	unsigned count;

	count = 0;
	for (packet = client->output_head; packet != NULL; packet = packet->next) {
		memcpy(words, packet->bytes, sizeof(words));
		assert((words[1] >> 16) == packet->size);
		if (words[0] == id && (words[1] & 65535) == opcode)
			count++;
	}
	return count;
}

/* Commit one image, optionally with a standard frame callback. */
static void
commit_buffer(
	struct zwl_client *client,
	int peer,
	uint32_t id,
	uint32_t callback)
{
	uint32_t words[3];
	int error;

	words[0] = id;
	words[1] = 0;
	words[2] = 0;
	error = request(client, peer, 10, 1, words, sizeof(words), -1);
	assert(error == 0);
	if (callback != 0) {
		error = request(client, peer, 10, 3, &callback, 4, -1);
		assert(error == 0);
	}
	error = request(client, peer, 10, 6, NULL, 0, -1);
	assert(error == 0);
}

/* FIFO completion, mailbox replacement, ID reuse and cross-client ownership. */
static void
lifetime_test(
	void)
{
	struct zwl_server server;
	struct zwl_client *first;
	struct zwl_client *second;
	struct zwl_object *surface;
	struct zwl_object *buffer;
	struct zwl_object *new_identity;
	uint64_t previous_handle;
	unsigned count;
	unsigned before;
	int peer;
	int other_peer;
	int error;

	server_init(&server);
	first = client_new(&server, &peer);
	surface = configure_surface(first, peer);
	create_buffer(first, peer, 20);
	create_buffer(first, peer, 21);
	buffer = create_buffer(first, peer, 22);
	commit_buffer(first, peer, 20, 30);
	zwl_schedule(&server);
	count = count_events(first, 30, 0);
	assert(count == 1);
	count = count_events(first, 20, 0);
	assert(count == 0);
	previous_handle = scanning;
	commit_buffer(first, peer, 21, 31);
	commit_buffer(first, peer, 22, 32);
	count = count_events(first, 21, 0);
	assert(count == 1 && scanning == previous_handle);
	error = request(first, peer, 10, 6, NULL, 0, -1);
	assert(error == 0 && surface->queued == buffer);
	zwl_schedule(&server);
	count = count_events(first, 20, 0);
	assert(count == 1 && scanning == buffer->image.handle);
	count = count_events(first, 31, 0);
	assert(count == 1);
	count = count_events(first, 32, 0);
	assert(count == 1);
	before = destroyed;
	error = request(first, peer, 22, 0, NULL, 0, -1);
	assert(error == 0 && buffer->dead && destroyed == before);
	new_identity = zwl_create(first, 22, ZWL_REGION, 1);
	assert(new_identity != NULL && new_identity != buffer);
	zwl_object_destroy(new_identity);
	second = client_new(&server, &other_peer);
	configure_surface(second, other_peer);
	create_buffer(second, other_peer, 20);
	commit_buffer(second, other_peer, 20, 30);
	zwl_schedule(&server);
	assert(server.front->client == second && destroyed == before);
	zwl_client_destroy(first);
	close(peer);
	assert(destroyed == before + 3 && server.front->client == second);
	before = released;
	zwl_client_destroy(second);
	close(other_peer);
	assert(released == before + 1 && scanning == 0 && server.clients == NULL);
	assert(!server.failed);
	service_cleanup(&server);
}

/* Failed GPU completion must not release or replace the old front allocation. */
static void
present_failure_test(
	void)
{
	struct zwl_server server;
	struct zwl_client *client;
	struct zwl_object *old_front;
	unsigned count;
	int peer;

	server_init(&server);
	client = client_new(&server, &peer);
	configure_surface(client, peer);
	create_buffer(client, peer, 20);
	create_buffer(client, peer, 21);
	commit_buffer(client, peer, 20, 30);
	zwl_schedule(&server);
	old_front = server.front;
	commit_buffer(client, peer, 21, 31);
	fail_present = 1;
	zwl_schedule(&server);
	fail_present = 0;
	count = count_events(client, 20, 0);
	assert(client->fatal && server.front == old_front && count == 0);
	assert(scanning == old_front->image.handle);
	service_cleanup(&server);
	close(peer);
	assert(scanning == 0 && !server.failed);
}

/* Partial stream bytes preserve rights until a complete request or disconnect. */
static void
fragment_and_cleanup_test(
	void)
{
	struct zwl_server server;
	struct zwl_client *client;
	struct msghdr message;
	struct iovec vector;
	union {
		struct cmsghdr alignment;
		unsigned char bytes[CMSG_SPACE(sizeof(int))];
	} control;
	struct cmsghdr *header;
	uint32_t words[3];
	struct zwl_object *object;
	ssize_t result;
	int peer;
	int right;
	int received;
	int error;
	int flags;

	server_init(&server);
	client = client_new(&server, &peer);
	words[0] = 1;
	words[1] = (12U << 16) | 1U;
	words[2] = 2;
	result = send(peer, words, 3, MSG_NOSIGNAL);
	assert(result == 3);
	error = zwl_read(client);
	assert(error == 0 && client->input_size == 3);
	result = send(peer, (unsigned char *)words + 3, sizeof(words) - 3, MSG_NOSIGNAL);
	assert(result == (ssize_t)sizeof(words) - 3);
	error = zwl_read(client);
	assert(error == 0 && client->input_size == 0);
	object = zwl_find(client, 2);
	assert(object != NULL && object->kind == ZWL_REGISTRY);
	right = open("/dev/null", O_RDONLY);
	assert(right >= 0);
	memset(&message, 0, sizeof(message));
	memset(&control, 0, sizeof(control));
	vector.iov_base = words;
	vector.iov_len = 1;
	message.msg_iov = &vector;
	message.msg_iovlen = 1;
	message.msg_control = control.bytes;
	message.msg_controllen = sizeof(control);
	header = CMSG_FIRSTHDR(&message);
	header->cmsg_len = CMSG_LEN(sizeof(int));
	header->cmsg_level = SOL_SOCKET;
	header->cmsg_type = SCM_RIGHTS;
	memcpy(CMSG_DATA(header), &right, sizeof(right));
	result = sendmsg(peer, &message, MSG_NOSIGNAL);
	assert(result == 1);
	error = zwl_read(client);
	assert(error == 0 && client->right_count == 1);
	received = client->rights[0];
	zwl_client_destroy(client);
	flags = fcntl(received, F_GETFD);
	assert(flags < 0 && errno == EBADF);
	close(right);
	close(peer);
	service_cleanup(&server);
}

/* A wrong metadata claim is rejected after import, with both fd and import retired. */
static void
invalid_request_test(
	void)
{
	struct zwl_server server;
	struct zwl_client *client;
	unsigned char payload[72];
	uint32_t words[2];
	uint32_t id;
	unsigned before;
	unsigned count;
	int peer;
	int right;
	int error;
	int flags;

	server_init(&server);
	client = client_new(&server, &peer);
	configure_surface(client, peer);
	words[0] = 20;
	words[1] = 64;
	memcpy(payload, words, 8);
	memcpy(payload + 8, &authoritative, 64);
	payload[8 + 8] ^= 1;
	right = open("/dev/null", O_RDONLY);
	assert(right >= 0);
	before = destroyed;
	error = request(client, peer, 5, 1, payload, sizeof(payload), right);
	assert(error == EPROTO && client->fatal && destroyed == before + 1);
	flags = fcntl(imported_fd, F_GETFD);
	assert(flags < 0 && errno == EBADF);
	count = count_events(client, 1, 0);
	assert(count == 1);
	close(right);
	zwl_client_destroy(client);
	close(peer);
	client = client_new(&server, &peer);
	configure_surface(client, peer);
	memcpy(payload + 8, &authoritative, 64);
	error = request(client, peer, 5, 1, payload, sizeof(payload), -1);
	assert(error == 0 && !client->fatal && client->input_size == 80);
	close(peer);
	error = zwl_read(client);
	assert(error == EPIPE);
	zwl_client_destroy(client);
	client = client_new(&server, &peer);
	id = 2;
	error = request(client, peer, 99, 1, &id, 4, -1);
	assert(error == EPROTO && client->fatal);
	zwl_client_destroy(client);
	close(peer);
	service_cleanup(&server);
}

/* A complete nha frame waits for its fd, even when ancillary data travels with a successor. */
static void
late_rights_test(
	void)
{
	struct zwl_server server;
	struct zwl_client *client;
	struct zwl_object *buffer;
	unsigned char payload[72];
	uint32_t words[2];
	uint32_t callback;
	unsigned count;
	int peer;
	int right;
	int error;

	server_init(&server);
	client = client_new(&server, &peer);
	configure_surface(client, peer);
	words[0] = 20;
	words[1] = 64;
	memcpy(payload, words, 8);
	memcpy(payload + 8, &authoritative, 64);
	error = request(client, peer, 5, 1, payload, sizeof(payload), -1);
	assert(error == 0 && !client->fatal && client->input_size == 80);
	right = open("/dev/null", O_RDONLY);
	assert(right >= 0);
	callback = 30;
	error = request(client, peer, 1, 0, &callback, 4, right);
	assert(error == 0 && !client->fatal && client->input_size == 0);
	buffer = zwl_find(client, 20);
	assert(buffer != NULL && buffer->image.handle != 0);
	count = count_events(client, 30, 0);
	assert(count == 1);
	close(right);
	close(peer);
	service_cleanup(&server);
}

/* Truncated ancillary delivery still gives the connection ownership of every received fd. */
static void
truncated_rights_test(
	void)
{
	struct zwl_server server;
	struct zwl_client *client;
	struct msghdr message;
	struct iovec vector;
	union {
		struct cmsghdr alignment;
		unsigned char bytes[CMSG_SPACE(10 * sizeof(int))];
	} control;
	struct cmsghdr *header;
	unsigned char byte;
	int source[10];
	int received[ZWL_RIGHTS_MAX];
	unsigned count;
	unsigned index;
	ssize_t sent;
	int peer;
	int error;
	int flags;

	server_init(&server);
	client = client_new(&server, &peer);
	for (index = 0; index < 10; index++) {
		source[index] = open("/dev/null", O_RDONLY);
		assert(source[index] >= 0);
	}
	memset(&message, 0, sizeof(message));
	memset(&control, 0, sizeof(control));
	byte = 1;
	vector.iov_base = &byte;
	vector.iov_len = 1;
	message.msg_iov = &vector;
	message.msg_iovlen = 1;
	message.msg_control = control.bytes;
	message.msg_controllen = sizeof(control);
	header = CMSG_FIRSTHDR(&message);
	header->cmsg_len = CMSG_LEN(sizeof(source));
	header->cmsg_level = SOL_SOCKET;
	header->cmsg_type = SCM_RIGHTS;
	memcpy(CMSG_DATA(header), source, sizeof(source));
	sent = sendmsg(peer, &message, MSG_NOSIGNAL);
	assert(sent == 1);
	error = zwl_read(client);
	assert(error == EPROTO && client->right_count > 0 && client->right_count < 10);
	count = client->right_count;
	memcpy(received, client->rights, count * sizeof(int));
	zwl_client_destroy(client);
	for (index = 0; index < count; index++) {
		flags = fcntl(received[index], F_GETFD);
		assert(flags < 0 && errno == EBADF);
	}
	for (index = 0; index < 10; index++)
		close(source[index]);
	close(peer);
	service_cleanup(&server);
}

/* Real stream backpressure preserves exact event bytes across partial writes. */
static void
partial_output_test(
	void)
{
	struct zwl_server server;
	struct zwl_client *client;
	unsigned char payload[60000];
	unsigned char actual[180024];
	unsigned char expected[180024];
	uint32_t header[2];
	size_t received;
	size_t offset;
	ssize_t bytes;
	unsigned index;
	unsigned passes;
	int capacity;
	int peer;
	int error;

	server_init(&server);
	client = client_new(&server, &peer);
	capacity = 2048;
	error = setsockopt(client->fd, SOL_SOCKET, SO_SNDBUF, &capacity, sizeof(capacity));
	assert(error == 0);
	memset(payload, 0x6b, sizeof(payload));
	for (index = 0; index < 3; index++) {
		error = zwl_emit(client, 10 + index, 7, payload, sizeof(payload));
		assert(error == 0);
		header[0] = 10 + index;
		header[1] = (60008U << 16) | 7;
		offset = index * 60008;
		memcpy(expected + offset, header, 8);
		memcpy(expected + offset + 8, payload, sizeof(payload));
	}
	error = zwl_flush(client);
	assert(error == 0 && client->output_head != NULL);
	assert(client->output_head->sent > 0 && client->output_head->sent < client->output_head->size);
	received = 0;
	passes = 0;
	while (received != sizeof(actual)) {
		passes++;
		assert(passes < 1000);
		bytes = recv(peer, actual + received, sizeof(actual) - received, MSG_DONTWAIT);
		if (bytes > 0)
			received += (size_t)bytes;
		else
			assert(bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
		error = zwl_flush(client);
		assert(error == 0);
	}
	assert(client->output_head == NULL && client->output_bytes == 0);
	error = memcmp(actual, expected, sizeof(actual));
	assert(error == 0);
	close(peer);
	service_cleanup(&server);
}

/* Explicit application unmap requires a new configure exchange before remapping. */
static void
remap_test(
	void)
{
	struct zwl_server server;
	struct zwl_client *client;
	struct zwl_object *surface;
	uint32_t old_serial;
	uint32_t words[3];
	unsigned count;
	int peer;
	int error;

	server_init(&server);
	client = client_new(&server, &peer);
	surface = configure_surface(client, peer);
	old_serial = surface->configure_serial;
	create_buffer(client, peer, 20);
	create_buffer(client, peer, 21);
	commit_buffer(client, peer, 20, 30);
	zwl_schedule(&server);
	commit_buffer(client, peer, 0, 31);
	assert(!surface->configured && !surface->acknowledged);
	zwl_schedule(&server);
	assert(server.front == NULL && scanning == 0 && server.lease == 0);
	count = count_events(client, 20, 0);
	assert(count == 1);
	error = request(client, peer, 10, 6, NULL, 0, -1);
	assert(error == 0 && surface->configured && !surface->acknowledged);
	assert(surface->configure_serial != old_serial);
	words[0] = surface->configure_serial;
	error = request(client, peer, 11, 4, words, 4, -1);
	assert(error == 0);
	commit_buffer(client, peer, 21, 32);
	zwl_schedule(&server);
	assert(server.front != NULL && server.front->id == 21 && scanning != 0);
	commit_buffer(client, peer, 0, 33);
	zwl_schedule(&server);
	words[0] = 20;
	words[1] = 0;
	words[2] = 0;
	error = request(client, peer, 10, 1, words, sizeof(words), -1);
	assert(error == 0);
	error = request(client, peer, 10, 6, NULL, 0, -1);
	assert(error == EPROTO && client->fatal && scanning == 0);
	close(peer);
	service_cleanup(&server);
}

/* Socket pathname cleanup preserves a replacement generation with a different inode. */
static void
socket_generation_test(
	void)
{
	struct zwl_server server;
	struct zwl_server refused;
	struct stat status;
	int replacement;
	int error;
	int refused_listener;
	int flags;

	server_init(&server);
	snprintf(server.socket_path, sizeof(server.socket_path), "/tmp/q309-zwl-%ld.sock", (long)getpid());
	error = listen_socket(&server);
	assert(error == 0 && server.socket_owned);
	error = unlink(server.socket_path);
	assert(error == 0);
	replacement = open(server.socket_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	assert(replacement >= 0);
	close(replacement);
	service_cleanup(&server);
	error = lstat(server.socket_path, &status);
	assert(error == 0 && S_ISREG(status.st_mode));

	/* Failed bind leaves listener ownership with the caller's ordinary partial cleanup. */
	server_init(&refused);
	strcpy(refused.socket_path, server.socket_path);
	error = listen_socket(&refused);
	assert(error == EADDRINUSE);
	refused_listener = refused.listener;
	assert(refused_listener >= 0 && !refused.socket_owned);
	service_cleanup(&refused);
	flags = fcntl(refused_listener, F_GETFD);
	assert(flags < 0 && errno == EBADF);
	error = lstat(server.socket_path, &status);
	assert(error == 0 && S_ISREG(status.st_mode));

	/* Only this fixture's explicitly owned replacement pathname is removed. */
	error = unlink(server.socket_path);
	assert(error == 0);
}

/* Exercise actual server code with finite transport and failure injection. */
int
main(
	void)
{
	unsigned index;

	memset(&authoritative, 0, sizeof(authoritative));
	authoritative.version = 1;
	authoritative.size = sizeof(authoritative);
	authoritative.width = 320;
	authoritative.height = 240;
	authoritative.stride = 1280;
	authoritative.format = GPU_DISPLAY_FORMAT_BGRA8888;
	authoritative.allocation_bytes = 320 * 240 * 4;
	authoritative.device_id = 17;
	lifetime_test();
	present_failure_test();
	fragment_and_cleanup_test();
	invalid_request_test();
	late_rights_test();
	truncated_rights_test();
	partial_output_test();
	remap_test();
	socket_generation_test();
	for (index = 1; index <= next_resource; index++)
		assert(resources[index] == 0);
	assert(imported == destroyed && scanning == 0);
	printf("zwl protocol/ownership: wire, SCM_RIGHTS, configure, FIFO, mailbox, multi-client, failure and cleanup PASS\n");
	return 0;
}
