/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Implements Wayland connections, isolated event queues and coordinated readers.
 */

#include "internal.h"
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static void wlc_roundtrip_done(void *data, struct wl_callback *callback, uint32_t serial);
static int wlc_display_wait(struct wl_display *display);

/*
 * Connects to a standard Wayland endpoint selected by name or environment.
 */
struct wl_display *
wl_display_connect(
	const char *name)
{
	struct sockaddr_un address;
	struct wl_display *display;
	const char *socket_variable;
	const char *runtime;
	char *end;
	long inherited;
	size_t length;
	int fd;
	int count;
	int error;

	/* Honors the inherited socket convention before path-based lookup. */
	socket_variable = getenv("WAYLAND_SOCKET");
	if (socket_variable != NULL) {
		/* Requires a complete decimal descriptor within the process fd range. */
		errno = 0;
		inherited = strtol(socket_variable, &end, 10);
		if (errno != 0 || end == socket_variable || *end != '\0' ||
		    inherited < 0 || inherited > INT_MAX) {
			errno = EINVAL;
			return NULL;
		}

		/* Prevents a later child from treating this already-consumed fd as fresh. */
		error = unsetenv("WAYLAND_SOCKET");
		if (error != 0)
			return NULL;

		/* Transfers the inherited socket into the normal connection constructor. */
		display = wl_display_connect_to_fd((int)inherited);
		if (display == NULL)
			return NULL;

		return display;
	}

	/* Resolves an unspecified endpoint through the standard display variable. */
	if (name == NULL)
		name = getenv("WAYLAND_DISPLAY");

	/* Uses the standard first-display name only when no name was supplied. */
	if (name == NULL)
		name = "wayland-0";

	/* Selects an absolute endpoint or a path below the runtime directory. */
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	if (name[0] == '/') {
		count = snprintf(address.sun_path, sizeof(address.sun_path), "%s", name);
	} else {
		/* Relative display names require the standard runtime directory. */
		runtime = getenv("XDG_RUNTIME_DIR");
		if (runtime == NULL || runtime[0] != '/') {
			errno = ENOENT;
			return NULL;
		}

		count = snprintf(address.sun_path, sizeof(address.sun_path), "%s/%s", runtime, name);
	}

	/* Refuses truncating a socket path into a different endpoint. */
	if (count < 0 || (size_t)count >= sizeof(address.sun_path)) {
		errno = ENAMETOOLONG;
		return NULL;
	}

	/* Creates a close-on-exec connection before transferring ownership. */
	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return NULL;

	/* Connects with the precise pathname extent, including its terminator. */
	length = offsetof(struct sockaddr_un, sun_path) + (size_t)count + 1;
	error = connect(fd, (const struct sockaddr *)&address, (socklen_t)length);
	if (error != 0) {
		error = errno;
		close(fd);
		errno = error;
		return NULL;
	}

	/* The shared constructor consumes fd on both success and failure. */
	display = wl_display_connect_to_fd(fd);
	if (display == NULL)
		return NULL;

	/* Succeeded: the caller owns the connected display. */
	return display;
}

/*
 * Adopts a connected socket and initializes its protocol state.
 */
struct wl_display *
wl_display_connect_to_fd(
	int fd)
{
	struct wl_display *display;
	int flags;
	int error;

	/* Allocates the complete connection before publishing any public proxy. */
	display = calloc(1, sizeof(*display));
	if (display == NULL) {
		close(fd);
		errno = ENOMEM;
		return NULL;
	}

	/* Reserves one maximum-size message for incremental stream parsing. */
	display->input = malloc(WLC_WIRE_MAX);
	if (display->input == NULL) {
		free(display);
		close(fd);
		errno = ENOMEM;
		return NULL;
	}

	/* Initializes the mutex before any helper can inspect connection state. */
	error = pthread_mutex_init(&display->mutex, NULL);
	if (error != 0) {
		free(display->input);
		free(display);
		close(fd);
		errno = error;
		return NULL;
	}

	/* Initializes the condition used to coordinate all prepared readers. */
	error = pthread_cond_init(&display->readers_changed, NULL);
	if (error != 0) {
		pthread_mutex_destroy(&display->mutex);
		free(display->input);
		free(display);
		close(fd);
		errno = error;
		return NULL;
	}

	/* The connection owns fd from this point through every cleanup path. */
	display->fd = fd;
	display->next_id = 2;
	display->default_queue.display = display;
	display->queues = &display->default_queue;
	display->proxy.interface = &wl_display_interface;
	display->proxy.display = display;
	display->proxy.queue = &display->default_queue;
	display->proxy.id = 1;
	display->proxy.version = 1;
	display->proxy.references = 1;

	/* Preserves descriptor-local flags while preventing accidental exec leaks. */
	flags = fcntl(fd, F_GETFD);
	if (flags < 0) {
		error = errno;
		wl_display_disconnect(display);
		errno = error;
		return NULL;
	}

	error = fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
	if (error != 0) {
		error = errno;
		wl_display_disconnect(display);
		errno = error;
		return NULL;
	}

	/* Every library I/O operation must remain nonblocking under the mutex. */
	flags = fcntl(fd, F_GETFL);
	if (flags < 0) {
		error = errno;
		wl_display_disconnect(display);
		errno = error;
		return NULL;
	}

	error = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	if (error != 0) {
		error = errno;
		wl_display_disconnect(display);
		errno = error;
		return NULL;
	}

	/* Succeeded: the display singleton and default event queue are ready. */
	return display;
}

/*
 * Releases a connection after its users have stopped dispatching and reading.
 */
void
wl_display_disconnect(
	struct wl_display *display)
{
	struct wl_event_queue *queue;
	struct wl_event_queue *next_queue;
	struct wlc_event *event;
	struct wl_proxy *proxy;
	struct wlc_packet *packet;
	size_t index;

	/* Allows connection setup failures to share null-safe cleanup. */
	if (display == NULL)
		return;

	/* Caller synchronization excludes concurrent use during final disconnect. */
	while (display->wrappers != NULL)
		wl_proxy_wrapper_destroy(display->wrappers);

	/* Recovers undispatched event references before releasing wire map entries. */
	queue = display->queues;
	while (queue != NULL) {
		next_queue = queue->next;

		/* Discards queued fds and callback-owned object references. */
		while (queue->head != NULL) {
			event = queue->head;
			queue->head = event->next;
			wlc_event_destroy(event);
		}

		/* The default queue lives inside the display allocation. */
		if (queue != &display->default_queue) {
			free(queue->name);
			free(queue);
		}

		queue = next_queue;
	}

	/* Releases unacknowledged tombstones and any still-mapped caller proxies. */
	while (display->objects != NULL) {
		proxy = display->objects;
		wlc_proxy_destroy(proxy);
		wlc_proxy_remove(proxy);
	}

	/* Drops every unsent packet and the fd references it retained. */
	while (display->output_head != NULL) {
		packet = display->output_head;
		display->output_head = packet->next;
		wlc_packet_destroy(packet);
	}

	/* Recovers rights not yet associated with a complete received event. */
	for (index = 0; index < display->input_descriptor_count; index++)
		close(display->input_descriptors[index]);

	/* Releases connection-owned storage and the adopted socket. */
	free(display->input_descriptors);
	free(display->input);
	close(display->fd);
	pthread_cond_destroy(&display->readers_changed);
	pthread_mutex_destroy(&display->mutex);
	free(display);

	/* Succeeded: the connection retains no kernel or userland resources. */
	return;
}

/*
 * Obtains the socket descriptor for event-loop polling.
 */
int
wl_display_get_fd(
	struct wl_display *display)
{
	/* Succeeded: the descriptor remains owned by this display. */
	return display->fd;
}

/*
 * Obtains the connection's first fatal error.
 */
int
wl_display_get_error(
	struct wl_display *display)
{
	int error;

	/* Samples the sticky connection status consistently with all readers. */
	pthread_mutex_lock(&display->mutex);

	error = display->error;

	pthread_mutex_unlock(&display->mutex);

	/* Succeeded: zero identifies a usable connection. */
	return error;
}

/*
 * Obtains the server error's object identity and protocol-specific code.
 */
uint32_t
wl_display_get_protocol_error(
	struct wl_display *display,
	const struct wl_interface **interface,
	uint32_t *id)
{
	uint32_t code;

	/* Captures all error detail from the same connection state. */
	pthread_mutex_lock(&display->mutex);

	code = display->protocol_code;

	/* Optional outputs do not require the original object to remain alive. */
	if (interface != NULL)
		*interface = display->protocol_interface;

	/* Preserves the numeric identity even if the proxy has been destroyed. */
	if (id != NULL)
		*id = display->protocol_id;

	pthread_mutex_unlock(&display->mutex);

	/* Succeeded: reports the protocol code separately from the EPROTO errno. */
	return code;
}

/*
 * Flushes queued requests while retaining any unsent suffix on backpressure.
 */
int
wl_display_flush(
	struct wl_display *display)
{
	int sent;
	int error;

	/* Serializes packet ownership with writers and fatal connection errors. */
	pthread_mutex_lock(&display->mutex);

	/* Fatal errors permanently disable further writes to this connection. */
	if (display->error != 0) {
		errno = display->error;
		pthread_mutex_unlock(&display->mutex);
		return -1;
	}

	/* Performs only nonblocking socket writes while holding the state mutex. */
	sent = wlc_wire_flush(display);
	error = errno;

	/* Backpressure retains queued packets and is not a protocol failure. */
	if (sent < 0 && error != EAGAIN && error != EWOULDBLOCK)
		wlc_display_error(display, error);

	pthread_mutex_unlock(&display->mutex);

	/* Preserves the exact socket error for poll-based callers. */
	if (sent < 0) {
		errno = error;
		return -1;
	}

	/* Succeeded: reports how many request bytes reached the socket. */
	return sent;
}

/*
 * Allocates a private event queue for one independently dispatched client user.
 */
struct wl_event_queue *
wl_display_create_queue_with_name(
	struct wl_display *display,
	const char *name)
{
	struct wl_event_queue *queue;

	/* Creates an empty event container before publishing it on the display. */
	queue = calloc(1, sizeof(*queue));
	if (queue == NULL)
		return NULL;

	/* Copies an optional diagnostic name without depending on caller lifetime. */
	if (name != NULL) {
		queue->name = strdup(name);
		if (queue->name == NULL) {
			free(queue);
			return NULL;
		}
	}

	/* Publishes the queue while object routing and disconnect are serialized. */
	queue->display = display;
	pthread_mutex_lock(&display->mutex);

	queue->next = display->queues;
	display->queues = queue;

	pthread_mutex_unlock(&display->mutex);

	/* Succeeded: new proxies and wrappers may select this queue. */
	return queue;
}

/*
 * Allocates an unnamed private event queue.
 */
struct wl_event_queue *
wl_display_create_queue(
	struct wl_display *display)
{
	struct wl_event_queue *queue;

	/* Uses the same lifetime contract as named diagnostic queues. */
	queue = wl_display_create_queue_with_name(display, NULL);
	if (queue == NULL)
		return NULL;

	/* Succeeded: the caller owns the new empty queue. */
	return queue;
}

/*
 * Obtains a queue's optional diagnostic name.
 */
const char *
wl_event_queue_get_name(
	const struct wl_event_queue *queue)
{
	/* Succeeded: the immutable name remains owned by the queue. */
	return queue->name;
}

/*
 * Releases a private queue and suppresses its undispatched callbacks.
 */
void
wl_event_queue_destroy(
	struct wl_event_queue *queue)
{
	struct wl_display *display;
	struct wl_event_queue **link;
	struct wl_proxy *proxy;
	struct wlc_event *event;

	/* Allows partially constructed WSI instances to share cleanup. */
	if (queue == NULL)
		return;

	/* The default queue belongs to the enclosing display allocation. */
	display = queue->display;
	if (queue == &display->default_queue)
		return;

	/* Detaches queue routing before releasing any callback-owned resources. */
	pthread_mutex_lock(&display->mutex);

	/* Reassigns surviving objects defensively; normal callers destroy them first. */
	for (proxy = display->objects; proxy != NULL; proxy = proxy->next) {
		if (proxy->queue == queue)
			proxy->queue = &display->default_queue;
	}

	/* Wrappers can outlive queues without retaining a dangling destination. */
	for (proxy = display->wrappers; proxy != NULL; proxy = proxy->next) {
		if (proxy->queue == queue)
			proxy->queue = &display->default_queue;
	}

	/* Removes this exact queue from the connection-owned queue list. */
	for (link = &display->queues; *link != NULL; link = &(*link)->next) {
		if (*link == queue) {
			*link = queue->next;
			break;
		}
	}

	/* Discards pending callbacks and closes their undelivered rights. */
	while (queue->head != NULL) {
		event = queue->head;
		queue->head = event->next;
		wlc_event_destroy(event);
	}

	pthread_mutex_unlock(&display->mutex);

	/* Releases the queue's own allocations after it is unreachable to readers. */
	free(queue->name);
	free(queue);

	/* Succeeded: no pending event can call a listener from this queue. */
	return;
}

/*
 * Dispatches queued events without reading or dispatching any other queue.
 */
int
wl_display_dispatch_queue_pending(
	struct wl_display *display,
	struct wl_event_queue *queue)
{
	struct wlc_event *event;
	int dispatched;
	int error;

	/* A null queue denotes the connection's default event destination. */
	if (queue == NULL)
		queue = &display->default_queue;

	/* Refuses dispatching a queue owned by another connection. */
	if (queue->display != display) {
		errno = EINVAL;
		return -1;
	}

	/* Removes one event at a time so listeners run without the display mutex. */
	dispatched = 0;
	while (1) {
		pthread_mutex_lock(&display->mutex);

		/* A fatal connection error takes precedence over stale pending events. */
		if (display->error != 0) {
			errno = display->error;
			pthread_mutex_unlock(&display->mutex);
			return -1;
		}

		/* Detaches one event while preserving its strong object references. */
		event = queue->head;
		if (event == NULL) {
			pthread_mutex_unlock(&display->mutex);
			break;
		}

		queue->head = event->next;

		/* An empty queue has no tail and permits prepare_read_queue again. */
		if (queue->head == NULL)
			queue->tail = NULL;

		pthread_mutex_unlock(&display->mutex);

		/* Invokes only this queue's selected listener or generic dispatcher. */
		error = wlc_event_dispatch(event);

		/* Recovers event-owned storage after a reentrant listener has returned. */
		pthread_mutex_lock(&display->mutex);

		wlc_event_destroy(event);

		/* An unsupported or failing dispatcher makes ordering unusable. */
		if (error != 0)
			wlc_display_error(display, error);

		pthread_mutex_unlock(&display->mutex);

		/* Propagates the first callback-dispatch failure to the event loop. */
		if (error != 0) {
			errno = error;
			return -1;
		}

		/* The count describes consumed events, including destroyed-proxy skips. */
		if (dispatched < INT_MAX)
			dispatched++;
	}

	/* Succeeded: reports the events consumed from exactly the requested queue. */
	return dispatched;
}

/*
 * Dispatches default-queue events already received by any connection reader.
 */
int
wl_display_dispatch_pending(
	struct wl_display *display)
{
	int dispatched;

	/* Leaves WSI and other private queues undispatched. */
	dispatched = wl_display_dispatch_queue_pending(display, NULL);
	if (dispatched < 0)
		return -1;

	/* Succeeded: reports only default-queue dispatches. */
	return dispatched;
}

/*
 * Registers a reader intention only when its chosen queue is empty.
 */
int
wl_display_prepare_read_queue(
	struct wl_display *display,
	struct wl_event_queue *queue)
{
	/* Null selects the standard default queue. */
	if (queue == NULL)
		queue = &display->default_queue;

	/* Keeps a read intention within one connection's queue ownership. */
	if (queue->display != display) {
		errno = EINVAL;
		return -1;
	}

	/* Serializes emptiness testing with message decoding and other readers. */
	pthread_mutex_lock(&display->mutex);

	/* A failed connection cannot start a fresh polling/read cycle. */
	if (display->error != 0) {
		errno = display->error;
		pthread_mutex_unlock(&display->mutex);
		return -1;
	}

	/* Pending events must be dispatched before waiting for new socket bytes. */
	if (queue->head != NULL) {
		pthread_mutex_unlock(&display->mutex);
		errno = EAGAIN;
		return -1;
	}

	/* Prevents an overflowing counter from letting an early reader pass. */
	if (display->prepared_readers == UINT_MAX) {
		pthread_mutex_unlock(&display->mutex);
		errno = EOVERFLOW;
		return -1;
	}

	/* Every registered reader must call read_events or cancel_read exactly once. */
	display->prepared_readers++;

	pthread_mutex_unlock(&display->mutex);

	/* Succeeded: no socket read occurs until all prepared readers participate. */
	return 0;
}

/*
 * Registers a reader intention for the default event queue.
 */
int
wl_display_prepare_read(
	struct wl_display *display)
{
	int error;

	/* Uses the same coordinated reader barrier as private queues. */
	error = wl_display_prepare_read_queue(display, NULL);
	if (error != 0)
		return -1;

	/* Succeeded: the caller must read or cancel this intention. */
	return 0;
}

/*
 * Cancels one prepared read and releases waiting readers when it was last.
 */
void
wl_display_cancel_read(
	struct wl_display *display)
{
	/* Cancels only a real read intention under the shared reader mutex. */
	pthread_mutex_lock(&display->mutex);

	/* A spurious cancellation must not underflow the reader barrier. */
	if (display->prepared_readers == 0) {
		pthread_mutex_unlock(&display->mutex);
		return;
	}

	/* This reader no longer requires any thread to delay the next socket read. */
	display->prepared_readers--;
	if (display->prepared_readers == 0) {
		/* A final cancellation releases the generation without reading bytes. */
		display->read_error = 0;
		display->read_generation++;
		pthread_cond_broadcast(&display->readers_changed);
	}

	pthread_mutex_unlock(&display->mutex);

	/* Succeeded: the caller no longer owns a pending read intention. */
	return;
}

/*
 * Reads events once all prepared readers have entered or canceled their read.
 */
int
wl_display_read_events(
	struct wl_display *display)
{
	uint64_t generation;
	int error;

	/* Joins the generation established by the caller's successful preparation. */
	pthread_mutex_lock(&display->mutex);

	/* Reading without preparation would race toolkit and WSI event loops. */
	if (display->prepared_readers == 0) {
		pthread_mutex_unlock(&display->mutex);
		errno = EINVAL;
		return -1;
	}

	/* Entering a read consumes exactly one previously registered intention. */
	generation = display->read_generation;
	display->prepared_readers--;
	if (display->prepared_readers == 0) {
		/* The last arriving reader alone receives and queues available bytes. */
		error = display->error;
		if (error == 0)
			error = wlc_wire_read(display);

		/* All waiting readers observe the same completed read generation. */
		display->read_error = error;
		display->read_generation++;

		/* Signals are retryable; wire and connection failures are permanent. */
		if (error != 0 && error != EINTR)
			wlc_display_error(display, error);

		pthread_cond_broadcast(&display->readers_changed);
	} else {
		/* Waits for the last reader or cancellation to finish this generation. */
		while (generation == display->read_generation)
			pthread_cond_wait(&display->readers_changed, &display->mutex);
	}

	/* A later cancellation cannot hide a connection failure from a waiting reader. */
	error = display->error;
	if (error == 0)
		error = display->read_error;

	pthread_mutex_unlock(&display->mutex);

	/* Reports this generation's socket or protocol failure consistently. */
	if (error != 0) {
		errno = error;
		return -1;
	}

	/* Succeeded: complete events are queued without invoking application code. */
	return 0;
}

/*
 * Waits for and dispatches events belonging only to one selected event queue.
 */
int
wl_display_dispatch_queue(
	struct wl_display *display,
	struct wl_event_queue *queue)
{
	int dispatched;
	int error;

	/* Rechecks pending events after every coordinated socket read or race. */
	while (1) {
		/* Dispatches already-received events before polling the descriptor. */
		dispatched = wl_display_dispatch_queue_pending(display, queue);
		if (dispatched < 0)
			return -1;

		/* A nonempty selected queue satisfies this dispatch call immediately. */
		if (dispatched != 0)
			return dispatched;

		/* Prevents a different queue reader from consuming readiness unnoticed. */
		error = wl_display_prepare_read_queue(display, queue);
		if (error != 0) {
			/* Newly queued events are dispatched at the start of the next pass. */
			if (errno == EAGAIN)
				continue;

			return -1;
		}

		/* Flushes requests and waits outside the connection mutex. */
		error = wlc_display_wait(display);
		if (error != 0) {
			error = errno;
			wl_display_cancel_read(display);
			errno = error;
			return -1;
		}

		/* The coordinated read never dispatches a different queue's listeners. */
		error = wl_display_read_events(display);
		if (error != 0)
			return -1;
	}
}

/*
 * Waits for and dispatches ordinary application events on the default queue.
 */
int
wl_display_dispatch(
	struct wl_display *display)
{
	int dispatched;

	/* Keeps this convenience entry point isolated from private WSI callbacks. */
	dispatched = wl_display_dispatch_queue(display, NULL);
	if (dispatched < 0)
		return -1;

	/* Succeeded: reports consumed default-queue events. */
	return dispatched;
}

/*
 * Waits until the compositor has processed the caller's preceding requests.
 */
int
wl_display_roundtrip_queue(
	struct wl_display *display,
	struct wl_event_queue *queue)
{
	static const struct wl_callback_listener wlc_roundtrip_listener = {
		wlc_roundtrip_done
	};
	struct wl_proxy *wrapper;
	struct wl_callback *callback;
	int done;
	int dispatched;
	int total;
	int error;

	/* Uses a wrapper so the sync callback inherits the requested private queue. */
	wrapper = wl_proxy_create_wrapper(display);
	if (wrapper == NULL)
		return -1;

	/* The original display proxy retains its application queue throughout. */
	wl_proxy_set_queue(wrapper, queue);
	callback = wl_display_sync((struct wl_display *)wrapper);
	wl_proxy_wrapper_destroy(wrapper);
	if (callback == NULL)
		return -1;

	/* Installs completion tracking before reading any server event. */
	done = 0;
	error = wl_callback_add_listener(callback, &wlc_roundtrip_listener, &done);
	if (error != 0) {
		wl_callback_destroy(callback);
		return -1;
	}

	/* Dispatches only the selected queue until its ordered sync callback arrives. */
	total = 0;
	while (!done) {
		dispatched = wl_display_dispatch_queue(display, queue);
		if (dispatched < 0) {
			wl_callback_destroy(callback);
			return -1;
		}

		/* Preserves a meaningful count without overflowing a prolonged roundtrip. */
		if (dispatched > INT_MAX - total) {
			total = INT_MAX;
		} else {
			total += dispatched;
		}
	}

	/* Succeeded: the compositor has processed every request preceding sync. */
	return total;
}

/*
 * Performs an ordered compositor roundtrip on the default event queue.
 */
int
wl_display_roundtrip(
	struct wl_display *display)
{
	int dispatched;

	/* Reuses the private-queue implementation with the standard destination. */
	dispatched = wl_display_roundtrip_queue(display, NULL);
	if (dispatched < 0)
		return -1;

	/* Succeeded: all earlier requests precede the completed sync callback. */
	return dispatched;
}

/*
 * Records a sticky connection error while the display mutex is held.
 */
void
wlc_display_error(
	struct wl_display *display,
	int error)
{
	/* Preserves the first cause instead of overwriting it during cleanup. */
	if (display->error == 0)
		display->error = error;

	/* Makes the failure immediately available to the current public caller. */
	errno = display->error;

	/* Succeeded: subsequent protocol operations will report this fatal error. */
	return;
}

/* Marks an ordered roundtrip complete and releases its local callback proxy. */
static void
wlc_roundtrip_done(
	void *data,
	struct wl_callback *callback,
	uint32_t serial)
{
	int *done;

	(void)serial;

	/* Publishes completion to the synchronous roundtrip loop on this thread. */
	done = data;
	*done = 1;
	wl_callback_destroy(callback);

	/* Succeeded: no later event can call the retired callback. */
	return;
}

/* Flushes pending requests and waits for input without holding the display lock. */
static int
wlc_display_wait(
	struct wl_display *display)
{
	struct pollfd descriptor;
	int flushed;
	int ready;

	/* Polls both directions while socket backpressure prevents a full flush. */
	while (1) {
		/* The standard flush interface preserves queued bytes on EAGAIN. */
		flushed = wl_display_flush(display);
		if (flushed < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
			return -1;

		/* Input may become ready even before every queued request is writable. */
		descriptor.fd = display->fd;
		descriptor.events = POLLIN;
		descriptor.revents = 0;
		if (flushed < 0)
			descriptor.events |= POLLOUT;

		/* Library dispatch has the standard unbounded event-waiting semantics. */
		ready = poll(&descriptor, 1, -1);
		if (ready < 0)
			return -1;

		/* A readable or disconnected peer must be resolved through recvmsg. */
		if ((descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0)
			return 0;

		/* An invalid socket cannot be repaired by retrying the event loop. */
		if ((descriptor.revents & POLLNVAL) != 0) {
			errno = EBADF;
			return -1;
		}
	}
}
