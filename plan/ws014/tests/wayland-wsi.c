/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises the actual Wayland WSI backend against an independent wire peer.
 * GPU allocation content is outside this protocol/lifetime fixture's boundary.
 */

#include "../../../userland/base/libvulkan/wsi-wayland.c"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

/* Tracks protocol observations independently of WSI callback bookkeeping. */
struct compositor_peer {
	int fd;
	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_cond_t changed;
	uint32_t surface_id;
	uint32_t registry_id;
	uint32_t factory_id;
	uint32_t buffer_ids[16];
	uint32_t callbacks[32];
	uint32_t pending_callback;
	unsigned buffers;
	unsigned commits;
	unsigned null_attaches;
	unsigned destroyed_buffers;
	unsigned default_callbacks;
	unsigned allocation_attempts;
	unsigned allocations;
	unsigned frees;
	unsigned fail_allocation;
	unsigned hide_factory;
	unsigned allocator_changes_errno;
};

static void *peer_main(void *argument);
static void peer_send(struct compositor_peer *peer, const void *bytes, size_t count);
static void peer_global(struct compositor_peer *peer, uint32_t registry);
static void peer_done(struct compositor_peer *peer, uint32_t callback);
static void peer_delete(struct compositor_peer *peer, uint32_t object_id);
static void peer_release(struct compositor_peer *peer, uint32_t buffer);
static void peer_wait_commits(struct compositor_peer *peer, unsigned count);
static void *tracked_allocate(void *data, size_t bytes, size_t alignment, VkSystemAllocationScope scope);
static void tracked_free(void *data, void *allocation);
static void application_done(void *data, struct wl_callback *callback, uint32_t serial);
static void peer_request(struct compositor_peer *peer, uint32_t object, uint32_t opcode, unsigned char *payload, size_t length, int descriptor);

/*
 * Validates native Wayland queue isolation, pacing and image ownership semantics.
 */
int
main(void)
{
	static const struct wl_callback_listener application_listener = {
		application_done
	};
	struct compositor_peer peer;
	struct wl_display *display;
	struct wl_surface *application_surface;
	struct wl_callback *application_callback;
	struct vulkan_surface generic;
	struct VkPhysicalDevice_T physical;
	struct vulkan_context context;
	struct VkDevice_T device;
	struct gpu_image_descriptor descriptor;
	VkWaylandSurfaceCreateInfoKHR info;
	VkSurfaceCapabilitiesKHR capabilities;
	VkPresentModeKHR modes[2];
	struct wayland_surface *native;
	void *first;
	void *replacement;
	void *image_a;
	void *image_b;
	void *image_c;
	void *failed;
	uint64_t sequence;
	uint32_t mode_count;
	uint32_t callback;
	uint32_t buffer_id;
	unsigned null_before;
	int sockets[2];
	int descriptor_fd;
	int error;
	int dispatched;
	VkResult result;
	VkBool32 supported;

	/* Creates a real client connection and a peer-owned externally existing surface. */
	memset(&peer, 0, sizeof(peer));
	memset(&generic, 0, sizeof(generic));
	memset(&physical, 0, sizeof(physical));
	memset(&context, 0, sizeof(context));
	memset(&device, 0, sizeof(device));
	error = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
	assert(error == 0);
	display = wl_display_connect_to_fd(sockets[0]);
	assert(display != NULL);
	application_surface = (struct wl_surface *)wl_proxy_create((struct wl_proxy *)display, &wl_surface_interface);
	assert(application_surface != NULL);
	peer.fd = sockets[1];
	peer.surface_id = wl_proxy_get_id((struct wl_proxy *)application_surface);
	pthread_mutex_init(&peer.mutex, NULL);
	pthread_cond_init(&peer.changed, NULL);
	error = pthread_create(&peer.thread, NULL, peer_main, &peer);
	assert(error == 0);

	/* Leaves a default-queue callback pending while WSI performs its own roundtrip. */
	application_callback = wl_display_sync(display);
	assert(application_callback != NULL);
	error = wl_callback_add_listener(application_callback, &application_listener, &peer);
	assert(error == 0);

	/* Registry probing requires an advertised transport and leaves application events pending. */
	pthread_mutex_lock(&peer.mutex);
	peer.hide_factory = 1;
	pthread_mutex_unlock(&peer.mutex);
	supported = wayland_display_supported(display);
	assert(supported == VK_FALSE);
	assert(peer.default_callbacks == 0);
	pthread_mutex_lock(&peer.mutex);
	peer.hide_factory = 0;
	pthread_mutex_unlock(&peer.mutex);
	supported = wayland_display_supported(display);
	assert(supported == VK_TRUE);
	assert(peer.default_callbacks == 0);
	generic.object.allocator.has_callbacks = VK_TRUE;
	generic.object.allocator.callbacks.pUserData = &peer;
	generic.object.allocator.callbacks.pfnAllocation = tracked_allocate;
	generic.object.allocator.callbacks.pfnFree = tracked_free;
	generic.platform = &wayland_platform;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
	info.display = display;
	info.surface = application_surface;
	result = wayland_surface_initialize(&generic, &info);
	assert(result == VK_SUCCESS);
	native = generic.platform_private;
	assert(native->factory != NULL);
	assert(peer.default_callbacks == 0);
	assert(wl_proxy_get_queue((struct wl_proxy *)application_surface) != native->queue);

	/* Checks actual advertised capabilities and both required Wayland present modes. */
	context.capabilities = GPU_CAP_SHARE;
	physical.object.context = &context;
	physical.properties.limits.maxImageDimension2D = 4096;
	result = wayland_capabilities(&generic, &physical, &capabilities);
	assert(result == VK_SUCCESS);
	assert(capabilities.minImageCount == 3);
	assert(capabilities.currentExtent.width == UINT32_MAX);
	assert(capabilities.minImageExtent.width == 16);
	assert(capabilities.minImageExtent.height == 16);
	assert(capabilities.maxImageExtent.width == 4096);
	assert(capabilities.maxImageExtent.height == 4096);

	/* Native bounds constrain a large renderer while smaller renderer bounds still apply. */
	physical.properties.limits.maxImageDimension2D = 8192;
	result = wayland_capabilities(&generic, &physical, &capabilities);
	assert(result == VK_SUCCESS);
	assert(capabilities.maxImageExtent.width == 4096);
	assert(capabilities.maxImageExtent.height == 4096);
	physical.properties.limits.maxImageDimension2D = 2048;
	result = wayland_capabilities(&generic, &physical, &capabilities);
	assert(result == VK_SUCCESS);
	assert(capabilities.maxImageExtent.width == 2048);
	assert(capabilities.maxImageExtent.height == 2048);

	/* An empty renderer/native intersection is unsupported rather than inverted. */
	physical.properties.limits.maxImageDimension2D = 15;
	result = wayland_capabilities(&generic, &physical, &capabilities);
	assert(result == VK_ERROR_FEATURE_NOT_PRESENT);
	physical.properties.limits.maxImageDimension2D = 4096;
	mode_count = 2;
	result = wayland_modes(&generic, &physical, &mode_count, modes);
	assert(result == VK_SUCCESS);
	assert(mode_count == 2);
	assert(modes[0] == VK_PRESENT_MODE_FIFO_KHR);
	assert(modes[1] == VK_PRESENT_MODE_MAILBOX_KHR);
	result = wayland_claim(&generic, &device, &first);
	assert(result == VK_SUCCESS);

	/* Imports a descriptor whose original fd is immediately closed by the producer. */
	memset(&descriptor, 0, sizeof(descriptor));
	descriptor.version = GPU_ABI_VERSION;
	descriptor.size = sizeof(descriptor);
	descriptor.width = 320;
	descriptor.height = 240;
	descriptor.stride = 1280;
	descriptor.format = GPU_PIXEL_RGBA8888;
	descriptor.tiling = GPU_IMAGE_LINEAR;
	descriptor_fd = open("/dev/null", O_RDONLY);
	assert(descriptor_fd >= 0);
	result = wayland_import(first, descriptor_fd, &descriptor, &image_a);
	assert(result == VK_SUCCESS);
	close(descriptor_fd);
	assert(wayland_available(image_a) == VK_TRUE);
	result = wayland_present(first, image_a, VK_PRESENT_MODE_FIFO_KHR, &sequence);
	assert(result == VK_SUCCESS);
	assert(sequence == 1);
	peer_wait_commits(&peer, 1);
	assert(wayland_available(image_a) == VK_FALSE);
	result = wayland_wait(first, sequence, 0);
	assert(result == VK_TIMEOUT);

	/* A completed frame callback advances pacing but does not release image memory. */
	pthread_mutex_lock(&peer.mutex);
	callback = peer.callbacks[0];
	buffer_id = peer.buffer_ids[0];
	peer_done(&peer, callback);
	pthread_mutex_unlock(&peer.mutex);
	result = wayland_wait(first, sequence, 1000000000ULL);
	assert(result == VK_SUCCESS);
	assert(wayland_available(image_a) == VK_FALSE);
	assert(peer.default_callbacks == 0);

	/* Only wl_buffer.release makes the original allocation available for reuse. */
	pthread_mutex_lock(&peer.mutex);
	peer_release(&peer, buffer_id);
	pthread_mutex_unlock(&peer.mutex);
	result = wayland_progress(first);
	assert(result == VK_SUCCESS);
	assert(wayland_available(image_a) == VK_TRUE);

	/* MAILBOX admits multiple commits without waiting for prior frame callbacks. */
	descriptor_fd = open("/dev/null", O_RDONLY);
	assert(descriptor_fd >= 0);
	result = wayland_import(first, descriptor_fd, &descriptor, &image_b);
	assert(result == VK_SUCCESS);
	close(descriptor_fd);
	result = wayland_present(first, image_b, VK_PRESENT_MODE_MAILBOX_KHR, &sequence);
	assert(result == VK_SUCCESS);
	assert(sequence == 2);
	result = wayland_present(first, image_a, VK_PRESENT_MODE_MAILBOX_KHR, &sequence);
	assert(result == VK_SUCCESS);
	assert(sequence == 3);
	peer_wait_commits(&peer, 3);
	assert(wayland_available(image_a) == VK_FALSE);
	assert(wayland_available(image_b) == VK_FALSE);

	/* A new swapchain lease can replace the old native attachment independently. */
	result = wayland_claim(&generic, &device, &replacement);
	assert(result == VK_SUCCESS);
	descriptor_fd = open("/dev/null", O_RDONLY);
	assert(descriptor_fd >= 0);
	result = wayland_import(replacement, descriptor_fd, &descriptor, &image_c);
	assert(result == VK_SUCCESS);
	close(descriptor_fd);
	result = wayland_present(replacement, image_c, VK_PRESENT_MODE_MAILBOX_KHR, &sequence);
	assert(result == VK_SUCCESS);
	peer_wait_commits(&peer, 4);
	pthread_mutex_lock(&peer.mutex);
	null_before = peer.null_attaches;
	pthread_mutex_unlock(&peer.mutex);
	result = wayland_release(first);
	assert(result == VK_SUCCESS);
	dispatched = wl_display_roundtrip_queue(display, native->queue);
	assert(dispatched >= 0);
	pthread_mutex_lock(&peer.mutex);
	assert(peer.null_attaches == null_before);
	assert(peer.destroyed_buffers == 2);

	/* Late events for the retired lease must not use freed callback userdata. */
	peer_done(&peer, peer.callbacks[1]);
	peer_done(&peer, peer.callbacks[2]);
	pthread_mutex_unlock(&peer.mutex);
	result = wayland_progress(replacement);
	assert(result == VK_SUCCESS);
	assert(wayland_available(image_c) == VK_FALSE);

	/* A failed image allocation never emits a buffer or steals the producer fd. */
	peer.fail_allocation = peer.allocation_attempts + 1;
	descriptor_fd = open("/dev/null", O_RDONLY);
	assert(descriptor_fd >= 0);
	failed = (void *)1;
	result = wayland_import(replacement, descriptor_fd, &descriptor, &failed);
	assert(result == VK_ERROR_OUT_OF_HOST_MEMORY);
	assert(failed == NULL);
	error = fcntl(descriptor_fd, F_GETFD);
	assert(error >= 0);
	close(descriptor_fd);
	peer.fail_allocation = 0;

	/* Application callbacks remain untouched until the application dispatches them. */
	assert(peer.default_callbacks == 0);
	dispatched = wl_display_dispatch_pending(display);
	assert(dispatched == 1);
	assert(peer.default_callbacks == 1);

	/* Destroying the active swapchain preserves the application mapping and retires buffer objects. */
	result = wayland_release(replacement);
	assert(result == VK_SUCCESS);
	dispatched = wl_display_roundtrip_queue(display, native->queue);
	assert(dispatched >= 0);
	pthread_mutex_lock(&peer.mutex);
	assert(peer.null_attaches == null_before);
	assert(peer.null_attaches == 0);
	assert(peer.destroyed_buffers == 3);
	pthread_mutex_unlock(&peer.mutex);

	/* Socket loss is terminal even if capability discovery follows the failed progress call. */
	result = wayland_claim(&generic, &device, &first);
	assert(result == VK_SUCCESS);
	error = shutdown(peer.fd, SHUT_RDWR);
	assert(error == 0);
	result = wayland_progress(first);
	assert(result == VK_ERROR_SURFACE_LOST_KHR);
	result = wayland_capabilities(&generic, &physical, &capabilities);
	assert(result == VK_ERROR_SURFACE_LOST_KHR);

	/* A user allocator may change errno without erasing an earlier socket failure. */
	error = wl_display_get_error(display);
	assert(error != 0);
	peer.allocator_changes_errno = 1;
	result = wayland_release(first);
	assert(result == VK_ERROR_SURFACE_LOST_KHR);
	peer.allocator_changes_errno = 0;
	wayland_destroy(&generic);
	assert(generic.platform_private == NULL);
	assert(peer.allocations == peer.frees);
	wl_surface_destroy(application_surface);
	wl_display_disconnect(display);
	pthread_join(peer.thread, NULL);
	close(peer.fd);
	pthread_cond_destroy(&peer.changed);
	pthread_mutex_destroy(&peer.mutex);

	/* Succeeded: reports actual wire/callback evidence without claiming GPU pixels. */
	puts("PASS Wayland WSI: factory probe, extent bounds, private queue, fd import, frame/release distinction, FIFO wait, MAILBOX, replacement, preserved mapping, OOM, terminal surface loss, allocator errno and teardown");
	return 0;
}

/* Receives complete client messages and observes their standard wire operations. */
static void *
peer_main(
	void *argument)
{
	struct compositor_peer *peer;
	struct msghdr message;
	struct iovec vector;
	struct cmsghdr *control;
	union {
		struct cmsghdr alignment;
		unsigned char bytes[CMSG_SPACE(sizeof(int))];
	} ancillary;
	uint32_t header[2];
	unsigned char payload[256];
	size_t length;
	size_t offset;
	ssize_t got;
	int descriptor;

	/* Uses a small independent peer because all tested messages are bounded contracts. */
	peer = argument;
	while (1) {
		memset(&message, 0, sizeof(message));
		memset(&ancillary, 0, sizeof(ancillary));
		vector.iov_base = header;
		vector.iov_len = sizeof(header);
		message.msg_iov = &vector;
		message.msg_iovlen = 1;
		message.msg_control = ancillary.bytes;
		message.msg_controllen = sizeof(ancillary.bytes);
		got = recvmsg(peer->fd, &message, MSG_WAITALL | MSG_CMSG_CLOEXEC);

		/* Client disconnect ends the peer after all queued fixture work is consumed. */
		if (got == 0)
			break;

		assert(got == 8);
		length = (header[1] >> 16) - 8;
		assert(length <= sizeof(payload));
		descriptor = -1;

		/* The actual client attaches rights to the first bytes of their message. */
		if (message.msg_controllen >= CMSG_LEN(sizeof(int))) {
			control = (struct cmsghdr *)ancillary.bytes;
			assert(control->cmsg_level == SOL_SOCKET);
			assert(control->cmsg_type == SCM_RIGHTS);
			memcpy(&descriptor, CMSG_DATA(control), sizeof(descriptor));
		}

		/* Accumulates only this message's remaining bytes before classifying it. */
		offset = 0;
		while (offset < length) {
			got = recv(peer->fd, payload + offset, length - offset, 0);
			assert(got > 0);
			offset += (size_t)got;
		}

		/* Serializes outgoing test events and observation counters with the test thread. */
		pthread_mutex_lock(&peer->mutex);
		peer_request(peer, header[0], header[1] & 0xffffU, payload, length, descriptor);
		pthread_cond_broadcast(&peer->changed);
		pthread_mutex_unlock(&peer->mutex);
	}

	/* Succeeded: the independent wire peer observed clean client disconnect. */
	return NULL;
}

/* Classifies actual wire messages without importing client protocol implementation code. */
static void
peer_request(
	struct compositor_peer *peer,
	uint32_t object,
	uint32_t opcode,
	unsigned char *payload,
	size_t length,
	int descriptor)
{
	uint32_t first;
	uint32_t size;
	uint32_t version;
	uint32_t bound;
	struct gpu_image_descriptor image;
	unsigned index;
	int flags;

	/* Scalar fixture requests use an independently decoded first payload word. */
	first = 0;
	if (length >= 4)
		memcpy(&first, payload, sizeof(first));

	/* Display requests create ordered callbacks or a fresh registry identity. */
	if (object == 1) {
		assert(length == 4);
		if (opcode == 0) {
			peer_done(peer, first);
		} else {
			assert(opcode == 1);
			peer->registry_id = first;
			if (peer->hide_factory == 0)
				peer_global(peer, first);
		}

		return;
	}

	/* Registry bind carries name, interface string, negotiated version and new ID. */
	if (object == peer->registry_id) {
		assert(opcode == 0);
		assert(first == 17);
		assert(length == 36);
		memcpy(&size, payload + 4, sizeof(size));
		assert(size == 18);
		assert(strcmp((const char *)payload + 8, "zed_gpu_buffer_v1") == 0);
		memcpy(&version, payload + 28, sizeof(version));
		memcpy(&bound, payload + 32, sizeof(bound));
		assert(version == 1);
		peer->factory_id = bound;
		return;
	}

	/* The private factory's metadata is compared with independent fixture expectations. */
	if (object == peer->factory_id) {
		if (opcode == 0) {
			peer_delete(peer, object);
			return;
		}

		assert(opcode == 1);
		assert(length == 8 + sizeof(image));
		assert(descriptor >= 0);
		memcpy(&size, payload + 4, sizeof(size));
		assert(size == sizeof(image));
		memcpy(&image, payload + 8, sizeof(image));
		assert(image.width == 320);
		assert(image.height == 240);
		assert(image.stride == 1280);
		flags = fcntl(descriptor, F_GETFD);
		assert(flags >= 0);
		assert((flags & FD_CLOEXEC) != 0);
		close(descriptor);
		assert(peer->buffers < 16);
		peer->buffer_ids[peer->buffers] = first;
		peer->buffers++;
		return;
	}

	/* Surface frame and commit observations drive independent pacing/release tests. */
	if (object == peer->surface_id) {
		if (opcode == 3) {
			assert(length == 4);
			peer->pending_callback = first;
		} else if (opcode == 1) {
			assert(length == 12);
			if (first == 0)
				peer->null_attaches++;
		} else if (opcode == 6) {
			assert(length == 0);
			assert(peer->commits < 32);
			peer->callbacks[peer->commits] = peer->pending_callback;
			peer->pending_callback = 0;
			peer->commits++;
		} else if (opcode == 0) {
			peer_delete(peer, object);
		} else {
			assert(opcode == 2);
			assert(length == 16);
		}

		return;
	}

	/* Buffer destructor requests retire only the exact imported object identity. */
	for (index = 0; index < peer->buffers; index++) {
		if (object == peer->buffer_ids[index]) {
			assert(opcode == 0);
			assert(length == 0);
			peer->destroyed_buffers++;
			peer_delete(peer, object);
			return;
		}
	}

	/* Every request emitted by this fixture has a known independent peer contract. */
	assert(0);
	return;
}

/* Writes independently encoded event words under the peer observation mutex. */
static void
peer_send(
	struct compositor_peer *peer,
	const void *bytes,
	size_t count)
{
	const unsigned char *cursor;
	ssize_t sent;

	/* Completes a finite event packet without relying on one stream write. */
	cursor = bytes;
	while (count != 0) {
		sent = send(peer->fd, cursor, count, MSG_NOSIGNAL);
		assert(sent > 0);
		cursor += sent;
		count -= (size_t)sent;
	}

	/* Succeeded: the client can decode the complete independently built event. */
	return;
}

/* Announces the zed buffer factory using a standard registry global event. */
static void
peer_global(
	struct compositor_peer *peer,
	uint32_t registry)
{
	uint32_t words[10];

	/* Encodes the NUL-terminated factory name with four-byte wire padding. */
	memset(words, 0, sizeof(words));
	words[0] = registry;
	words[1] = 40U << 16;
	words[2] = 17;
	words[3] = 18;
	memcpy(&words[4], "zed_gpu_buffer_v1", 18);
	words[9] = 1;
	peer_send(peer, words, sizeof(words));

	/* Succeeded: the client must bind this registry global before image import. */
	return;
}

/* Completes a frame or roundtrip callback and acknowledges its server retirement. */
static void
peer_done(
	struct compositor_peer *peer,
	uint32_t callback)
{
	uint32_t words[6];

	/* Places done before delete_id while both can arrive in one receive operation. */
	words[0] = callback;
	words[1] = 12U << 16;
	words[2] = 912;
	words[3] = 1;
	words[4] = (12U << 16) | 1U;
	words[5] = callback;
	peer_send(peer, words, sizeof(words));

	/* Succeeded: pacing completed without implying a buffer release. */
	return;
}

/* Acknowledges a destructor without fabricating any rendering completion. */
static void
peer_delete(
	struct compositor_peer *peer,
	uint32_t object_id)
{
	uint32_t words[3];

	/* Allows the client to reclaim the retired wire identity. */
	words[0] = 1;
	words[1] = (12U << 16) | 1U;
	words[2] = object_id;
	peer_send(peer, words, sizeof(words));

	/* Succeeded: only this object's wire ownership was retired. */
	return;
}

/* Releases one imported image without advancing its frame callback. */
static void
peer_release(
	struct compositor_peer *peer,
	uint32_t buffer)
{
	uint32_t words[2];

	/* The allocation becomes reusable independently of pacing-event ordering. */
	words[0] = buffer;
	words[1] = 8U << 16;
	peer_send(peer, words, sizeof(words));

	/* Succeeded: the client may now reacquire this image. */
	return;
}

/* Waits for independently observed commits before examining protocol side effects. */
static void
peer_wait_commits(
	struct compositor_peer *peer,
	unsigned count)
{
	/* The shell timeout bounds this fixture's overall peer wait. */
	pthread_mutex_lock(&peer->mutex);
	while (peer->commits < count)
		pthread_cond_wait(&peer->changed, &peer->mutex);

	pthread_mutex_unlock(&peer->mutex);

	/* Succeeded: at least the requested commits reached the compositor peer. */
	return;
}

/* Allocates through real Vulkan callback plumbing with a controlled OOM boundary. */
static void *
tracked_allocate(
	void *data,
	size_t bytes,
	size_t alignment,
	VkSystemAllocationScope scope)
{
	struct compositor_peer *peer;
	void *allocation;
	int error;

	(void)scope;

	/* Refuses one designated allocation without altering already-owned storage. */
	peer = data;
	peer->allocation_attempts++;
	if (peer->fail_allocation == peer->allocation_attempts)
		return NULL;

	/* Supplies the requested power-of-two alignment through a real host allocator. */
	if (alignment < sizeof(void *))
		alignment = sizeof(void *);

	error = posix_memalign(&allocation, alignment, bytes);
	assert(error == 0);
	peer->allocations++;

	/* Succeeded: the real Vulkan allocator will return this storage through free. */
	return allocation;
}

/* Counts actual lifetime completion for Vulkan-owned WSI allocations. */
static void
tracked_free(
	void *data,
	void *allocation)
{
	struct compositor_peer *peer;

	/* Recovers exactly the storage obtained through the matching callback policy. */
	peer = data;
	assert(allocation != NULL);
	peer->frees++;
	free(allocation);

	/* Allocation callbacks do not promise to preserve transport errno. */
	if (peer->allocator_changes_errno != 0)
		errno = EAGAIN;

	/* Succeeded: the fixture can compare acquired and released allocations. */
	return;
}

/* Records a callback that only the application queue is allowed to dispatch. */
static void
application_done(
	void *data,
	struct wl_callback *callback,
	uint32_t serial)
{
	struct compositor_peer *peer;

	/* Reentrant destruction checks the application's ordinary proxy contract too. */
	peer = data;
	assert(serial == 912);
	peer->default_callbacks++;
	wl_callback_destroy(callback);

	/* Succeeded: WSI private dispatch never invoked this application callback. */
	return;
}

/*
 * Provides the native-release wake boundary while the dedicated swapchain fixture verifies notifications.
 */
void
vulkan_wsi_image_notify(
	void)
{
	/* Succeeded: the separate real swapchain fixture verifies condition registration and wake ownership. */
	return;
}
