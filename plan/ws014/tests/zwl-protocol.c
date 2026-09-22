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
#include <sys/stat.h>

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

/* Find the nth queued event for one object and opcode and return its payload words. */
static const uint32_t *
find_event(
	struct zwl_client *client,
	uint32_t id,
	uint32_t opcode,
	unsigned nth,
	size_t *words)
{
	struct zwl_packet *packet;
	uint32_t header[2];

	for (packet = client->output_head; packet != NULL; packet = packet->next) {
		memcpy(header, packet->bytes, sizeof(header));
		if (header[0] != id || (header[1] & 65535) != opcode)
			continue;
		if (nth != 0) {
			nth--;
			continue;
		}
		*words = (packet->size - 8) / 4;
		return (const uint32_t *)(packet->bytes + 8);
	}
	return NULL;
}

/* Return the queue position of the event for one object and opcode whose first word matches, or -1. */
static int
event_position(
	struct zwl_client *client,
	uint32_t id,
	uint32_t opcode,
	uint32_t first_word)
{
	struct zwl_packet *packet;
	uint32_t header[3];
	int position;

	position = 0;
	for (packet = client->output_head; packet != NULL; packet = packet->next) {
		if (packet->size >= 12) {
			memcpy(header, packet->bytes, sizeof(header));
			if (header[0] == id && (header[1] & 65535) == opcode && header[2] == first_word)
				return position;
		}
		position++;
	}
	return -1;
}

/* Drop every queued event so the next checks see only new ones. */
static void
drop_events(
	struct zwl_client *client)
{
	struct zwl_packet *packet;

	while (client->output_head != NULL) {
		packet = client->output_head;
		client->output_head = packet->next;
		client->output_bytes -= packet->size;
		zwl_packet_free(packet);
	}
	client->output_tail = NULL;
}

/* Write one evdev report into a test device pipe and let the seat read it. */
static void
device_report(
	struct zwl_server *server,
	struct zwl_input_device *device,
	int writer,
	const struct input_event *events,
	size_t count)
{
	ssize_t written;

	written = write(writer, events, count * sizeof(*events));
	assert(written == (ssize_t)(count * sizeof(*events)));
	zwl_input_read(server, device);
}

/* Fill one evdev record with a fixed timestamp of 12.345 s. */
static void
set_event(
	struct input_event *event,
	uint16_t type,
	uint16_t code,
	int32_t value)
{
	memset(event, 0, sizeof(*event));
	event->time.tv_sec = 12;
	event->time.tv_usec = 345000;
	event->type = type;
	event->code = code;
	event->value = value;
}

/* Open a nonblocking pipe standing in for one evdev node. */
static void
test_device(
	int pipe_ends[2])
{
	int error;
	int flags;

	error = pipe(pipe_ends);
	assert(error == 0);
	flags = fcntl(pipe_ends[0], F_GETFL);
	assert(flags >= 0);
	error = fcntl(pipe_ends[0], F_SETFL, flags | O_NONBLOCK);
	assert(error == 0);
}

/* Receive every queued byte and descriptor from the compositor side. */
static size_t
drain_peer(
	int peer,
	unsigned char *bytes,
	size_t capacity,
	int *descriptors,
	unsigned *descriptor_count)
{
	union {
		struct cmsghdr alignment;
		unsigned char bytes[CMSG_SPACE(8 * sizeof(int))];
	} control;
	struct msghdr message;
	struct iovec vector;
	struct cmsghdr *header;
	size_t received;
	size_t count;
	ssize_t length;

	received = 0;
	*descriptor_count = 0;
	while (1) {
		memset(&message, 0, sizeof(message));
		memset(&control, 0, sizeof(control));
		vector.iov_base = bytes + received;
		vector.iov_len = capacity - received;
		message.msg_iov = &vector;
		message.msg_iovlen = 1;
		message.msg_control = control.bytes;
		message.msg_controllen = sizeof(control.bytes);
		length = recvmsg(peer, &message, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
		if (length < 0) {
			assert(errno == EAGAIN || errno == EWOULDBLOCK);
			break;
		}
		assert(length > 0);
		for (header = CMSG_FIRSTHDR(&message); header != NULL; header = CMSG_NXTHDR(&message, header)) {
			assert(header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS);
			count = (header->cmsg_len - CMSG_LEN(0)) / sizeof(int);
			assert(*descriptor_count + count <= 8);
			memcpy(descriptors + *descriptor_count, CMSG_DATA(header), count * sizeof(int));
			*descriptor_count += (unsigned)count;
		}
		received += (size_t)length;
		assert(received < capacity);
	}
	return received;
}

/* Report whether every queued event of a client targets one of two object IDs. */
static int
only_events_for(
	struct zwl_client *client,
	uint32_t first,
	uint32_t second)
{
	struct zwl_packet *packet;
	uint32_t header[2];

	for (packet = client->output_head; packet != NULL; packet = packet->next) {
		memcpy(header, packet->bytes, sizeof(header));
		if (header[0] != first && header[0] != second)
			return 0;
	}
	return 1;
}

/* wl_seat, wl_pointer and wl_keyboard: wire, focus, evdev mapping and descriptor transfer. */
static void
seat_test(
	void)
{
	struct zwl_server server;
	struct zwl_client *client;
	struct zwl_client *other;
	struct zwl_object *surface;
	struct zwl_object *object;
	struct zwl_input_device *tablet;
	struct zwl_input_device *mouse;
	struct input_absinfo range;
	struct input_event events[4];
	struct stat null_status;
	struct stat received_status;
	unsigned char stream[8192];
	const uint32_t *words;
	uint32_t header[2];
	uint32_t id;
	size_t count;
	size_t received;
	size_t offset;
	unsigned descriptor_count;
	unsigned keymaps;
	int descriptors[8];
	int tablet_pipe[2];
	int mouse_pipe[2];
	int peer;
	int other_peer;
	int error;
	int leave;
	int deleted;

	server_init(&server);
	server.pointer_x = 160;
	server.pointer_y = 120;

	/* An absolute tablet and a relative mouse that also has keys. */
	test_device(tablet_pipe);
	test_device(mouse_pipe);
	memset(&range, 0, sizeof(range));
	range.minimum = 0;
	range.maximum = 32767;
	error = zwl_input_attach(&server, tablet_pipe[0], "/test/event0", 1, 0, &range, &range);
	assert(error == 0 && server.capabilities == 1);
	error = zwl_input_attach(&server, mouse_pipe[0], "/test/event1", 1, 1, NULL, NULL);
	assert(error == 0 && server.capabilities == 3);
	tablet = &server.inputs[0];
	mouse = &server.inputs[1];
	assert(tablet->live && tablet->absolute && mouse->live && !mouse->absolute);

	/* The registry advertises wl_seat version 5 as global 5. */
	client = client_new(&server, &peer);
	surface = configure_surface(client, peer);
	words = find_event(client, 2, 0, 4, &count);
	assert(words != NULL && words[0] == 5 && words[1] == 8);
	assert(memcmp(&words[2], "wl_seat", 8) == 0 && words[4] == 5);

	/* Binding sends capabilities then the name "seat0". */
	drop_events(client);
	bind_interface(client, peer, 5, "wl_seat", 5, 7);
	words = find_event(client, 7, 0, 0, &count);
	assert(words != NULL && count == 1 && words[0] == 3);
	words = find_event(client, 7, 1, 0, &count);
	assert(words != NULL && count == 3 && words[0] == 6);
	assert(memcmp(&words[1], "seat0", 6) == 0);

	/* A pointer and a keyboard created before the surface is shown get no enter. */
	id = 8;
	error = request(client, peer, 7, 0, &id, 4, -1);
	assert(error == 0);
	id = 9;
	error = request(client, peer, 7, 1, &id, 4, -1);
	assert(error == 0);
	object = zwl_find(client, 8);
	assert(object != NULL && object->kind == ZWL_POINTER && object->version == 5);
	object = zwl_find(client, 9);
	assert(object != NULL && object->kind == ZWL_KEYBOARD && object->version == 5);
	assert(count_events(client, 8, 0) == 0 && count_events(client, 9, 1) == 0);

	/* The keyboard hears no_keymap (size 0) and repeat_info with rate 0. */
	words = find_event(client, 9, 0, 0, &count);
	assert(words != NULL && count == 2 && words[0] == 0 && words[1] == 0);
	words = find_event(client, 9, 5, 0, &count);
	assert(words != NULL && count == 2 && words[0] == 0 && words[1] == 600);

	/* The keymap descriptor crosses the socket as SCM_RIGHTS: it is /dev/null. */
	error = zwl_flush(client);
	assert(error == 0 && client->output_head == NULL);
	received = drain_peer(peer, stream, sizeof(stream), descriptors, &descriptor_count);
	assert(descriptor_count == 1);
	error = fstat(descriptors[0], &received_status);
	assert(error == 0);
	error = stat("/dev/null", &null_status);
	assert(error == 0 && received_status.st_rdev == null_status.st_rdev);
	close(descriptors[0]);
	keymaps = 0;
	for (offset = 0; offset < received; offset += header[1] >> 16) {
		memcpy(header, stream + offset, sizeof(header));
		assert((header[1] >> 16) >= 8);
		if (header[0] == 9 && (header[1] & 65535) == 0) {
			assert((header[1] >> 16) == 16);
			keymaps++;
		}
	}
	assert(offset == received && keymaps == 1);

	/* Input without focus moves the pointer but is not delivered. */
	set_event(&events[0], EV_REL, REL_X, 10);
	set_event(&events[1], EV_SYN, SYN_REPORT, 0);
	device_report(&server, mouse, mouse_pipe[1], events, 2);
	assert(server.pointer_x == 170 && client->output_head == NULL);

	/* Presenting the surface gives it focus: enter with position, frame, keyboard enter, modifiers. */
	create_buffer(client, peer, 20);
	commit_buffer(client, peer, 20, 30);
	drop_events(client);
	zwl_schedule(&server);
	assert(server.focus == surface);
	words = find_event(client, 8, 0, 0, &count);
	assert(words != NULL && count == 4 && words[0] != 0 && words[1] == 10);
	assert(words[2] == 170U * 256U && words[3] == 120U * 256U);
	assert(count_events(client, 8, 5) == 1);
	words = find_event(client, 9, 1, 0, &count);
	assert(words != NULL && count == 3 && words[1] == 10 && words[2] == 0);
	words = find_event(client, 9, 4, 0, &count);
	assert(words != NULL && count == 5 && words[1] == 0);

	/* An absolute report maps 0..32767 onto 0..319 and 0..239; one motion and one frame. */
	drop_events(client);
	set_event(&events[0], EV_ABS, ABS_X, 16383);
	set_event(&events[1], EV_ABS, ABS_Y, 32767);
	set_event(&events[2], EV_SYN, SYN_REPORT, 0);
	device_report(&server, tablet, tablet_pipe[1], events, 3);
	words = find_event(client, 8, 2, 0, &count);
	assert(words != NULL && count == 3 && words[0] == 12345);
	assert(words[1] == 159U * 256U && words[2] == 239U * 256U);
	assert(count_events(client, 8, 2) == 1 && count_events(client, 8, 5) == 1);

	/* A button carries a serial, the time, BTN_LEFT and the pressed state, then a frame. */
	drop_events(client);
	set_event(&events[0], EV_KEY, BTN_LEFT, 1);
	set_event(&events[1], EV_SYN, SYN_REPORT, 0);
	device_report(&server, tablet, tablet_pipe[1], events, 2);
	words = find_event(client, 8, 3, 0, &count);
	assert(words != NULL && count == 4 && words[0] != 0);
	assert(words[1] == 12345 && words[2] == BTN_LEFT && words[3] == 1);
	assert(count_events(client, 8, 2) == 0 && count_events(client, 8, 5) == 1);

	/* One wheel notch up: axis_source wheel, discrete -1, value -15, frame. */
	drop_events(client);
	set_event(&events[0], EV_REL, REL_WHEEL, 1);
	set_event(&events[1], EV_SYN, SYN_REPORT, 0);
	device_report(&server, mouse, mouse_pipe[1], events, 2);
	words = find_event(client, 8, 6, 0, &count);
	assert(words != NULL && count == 1 && words[0] == 0);
	words = find_event(client, 8, 8, 0, &count);
	assert(words != NULL && count == 2 && words[0] == 0 && (int32_t)words[1] == -1);
	words = find_event(client, 8, 4, 0, &count);
	assert(words != NULL && count == 3 && words[1] == 0 && (int32_t)words[2] == -15 * 256);
	assert(event_position(client, 8, 8, 0) < event_position(client, 8, 4, 12345));
	assert(count_events(client, 8, 5) == 1);

	/* Relative motion is clamped to the surface. */
	drop_events(client);
	set_event(&events[0], EV_REL, REL_X, -100000);
	set_event(&events[1], EV_SYN, SYN_REPORT, 0);
	device_report(&server, mouse, mouse_pipe[1], events, 2);
	words = find_event(client, 8, 2, 0, &count);
	assert(words != NULL && words[1] == 0 && words[2] == 239U * 256U);

	/* Shift then A: key, modifiers depressed=shift, key; autorepeat is not forwarded. */
	drop_events(client);
	set_event(&events[0], EV_KEY, KEY_LEFTSHIFT, 1);
	set_event(&events[1], EV_KEY, KEY_A, 1);
	set_event(&events[2], EV_KEY, KEY_A, 2);
	set_event(&events[3], EV_SYN, SYN_REPORT, 0);
	device_report(&server, mouse, mouse_pipe[1], events, 4);
	assert(count_events(client, 9, 3) == 2);
	words = find_event(client, 9, 3, 0, &count);
	assert(words != NULL && count == 4 && words[2] == KEY_LEFTSHIFT && words[3] == 1);
	words = find_event(client, 9, 3, 1, &count);
	assert(words != NULL && words[2] == KEY_A && words[3] == 1);
	words = find_event(client, 9, 4, 0, &count);
	assert(words != NULL && count == 5 && words[1] == 1 && words[2] == 0 && words[3] == 0);
	assert(count_events(client, 9, 4) == 1 && count_events(client, 8, 5) == 0);

	/* Releasing shift clears the mask. */
	drop_events(client);
	set_event(&events[0], EV_KEY, KEY_LEFTSHIFT, 0);
	set_event(&events[1], EV_SYN, SYN_REPORT, 0);
	device_report(&server, mouse, mouse_pipe[1], events, 2);
	words = find_event(client, 9, 4, 0, &count);
	assert(words != NULL && words[1] == 0);

	/* A report damaged by SYN_DROPPED is thrown away. */
	drop_events(client);
	set_event(&events[0], EV_ABS, ABS_X, 0);
	set_event(&events[1], EV_SYN, SYN_DROPPED, 0);
	set_event(&events[2], EV_SYN, SYN_REPORT, 0);
	device_report(&server, tablet, tablet_pipe[1], events, 3);
	assert(client->output_head == NULL);

	/* A second client without a seat takes the display: the first hears leave, the second no input. */
	other = client_new(&server, &other_peer);
	configure_surface(other, other_peer);
	create_buffer(other, other_peer, 20);
	commit_buffer(other, other_peer, 20, 30);
	drop_events(other);
	zwl_schedule(&server);
	assert(server.focus != NULL && server.focus->client == other);
	words = find_event(client, 8, 1, 0, &count);
	assert(words != NULL && count == 2 && words[1] == 10);
	words = find_event(client, 9, 2, 0, &count);
	assert(words != NULL && count == 2 && words[1] == 10);
	set_event(&events[0], EV_KEY, KEY_A, 1);
	set_event(&events[1], EV_SYN, SYN_REPORT, 0);
	device_report(&server, mouse, mouse_pipe[1], events, 2);
	assert(count_events(client, 9, 3) == 0);
	assert(count_events(other, 30, 0) == 1);
	error = only_events_for(other, 30, 1);
	assert(error == 1);
	zwl_client_destroy(other);
	close(other_peer);
	assert(server.focus == NULL);

	/* Destroying the focused client's surface sends leave before the surface's delete_id. */
	commit_buffer(client, peer, 20, 31);
	zwl_schedule(&server);
	assert(server.focus == surface);
	drop_events(client);
	zwl_object_destroy(zwl_find(client, 12));
	zwl_object_destroy(zwl_find(client, 11));
	zwl_object_destroy(surface);
	assert(server.focus == NULL);
	words = find_event(client, 8, 1, 0, &count);
	assert(words != NULL && words[1] == 10);
	leave = event_position(client, 8, 1, words[0]);
	deleted = event_position(client, 1, 1, 10);
	assert(leave >= 0 && deleted > leave);

	/* release retires pointer and keyboard with ordinary delete_id. */
	drop_events(client);
	error = request(client, peer, 8, 1, NULL, 0, -1);
	assert(error == 0 && zwl_find(client, 8) == NULL);
	error = request(client, peer, 9, 0, NULL, 0, -1);
	assert(error == 0 && zwl_find(client, 9) == NULL);
	assert(event_position(client, 1, 1, 8) >= 0 && event_position(client, 1, 1, 9) >= 0);

	/* A vanished device is closed and bound seats hear the reduced capabilities. */
	drop_events(client);
	close(mouse_pipe[1]);
	zwl_input_read(&server, mouse);
	assert(!mouse->live && server.capabilities == 1);
	words = find_event(client, 7, 0, 0, &count);
	assert(words != NULL && words[0] == 1);

	/* get_touch is not offered. */
	id = 40;
	error = request(client, peer, 7, 2, &id, 4, -1);
	assert(error == EPROTO && client->fatal);

	close(tablet_pipe[1]);
	close(peer);
	service_cleanup(&server);
	assert(!tablet->live && server.capabilities == 0);
	printf("zwl seat: wl_seat/wl_pointer/wl_keyboard wire, focus, evdev mapping, keymap SCM_RIGHTS PASS\n");
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
	seat_test();
	for (index = 1; index <= next_resource; index++)
		assert(resources[index] == 0);
	assert(imported == destroyed && scanning == 0);
	printf("zwl protocol/ownership: wire, SCM_RIGHTS, configure, FIFO, mailbox, multi-client, failure and cleanup PASS\n");
	return 0;
}
