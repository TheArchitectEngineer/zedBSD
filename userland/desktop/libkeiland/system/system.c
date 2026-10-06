/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The desktop's system for applications (keiland.h's kl_system_*; WS131
 * p010, plan/ws131/design.md section 4.4): the client of Keiland's system
 * extension (kl_system_manager_v1 and its network, sound, power, devices
 * and account objects; keiland/kl-system-protocol.h).
 *
 * As the settings do (settings.c), the objects live on a queue of the
 * library's own, which stays theirs, so that a change sent right after the
 * first state is not lost to another queue.  The application's loop reads
 * the display; kl_system_dispatch dispatches this queue, which only fills
 * the view (system-view.c), and reports what changed.
 */

#include <keiland.h>

#include <wayland-client.h>

#include "system-private.h"
#include "system-protocol.h"
#include "userland/desktop/keiland/kl-system-protocol.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Marks protocol callback arguments that this client does not inspect. */
#define UNUSED_PARAMETER(name) ((void)(name))

/*
 * One application's system: the display, the library's queue with the
 * manager (bound at manager_version) and its objects on it (an object is
 * NULL when the compositor does not offer it), the view, the number of the
 * next request, and whether the compositor went.
 */
struct kl_system {
	struct wl_display *display;
	struct wl_event_queue *queue;
	struct wl_proxy *manager;
	uint32_t manager_version;
	struct wl_proxy *network;
	struct wl_proxy *audio;
	struct wl_proxy *power;
	struct wl_proxy *devices;
	struct wl_proxy *account;
	struct wl_proxy *sharing;
	struct wl_proxy *notify;
	struct system_view view;
	uint32_t next_request;
	unsigned lost;
};

/* What the registry search found: the manager's global name (0 for none) and the version it offers. */
struct system_search {
	uint32_t name;
	uint32_t version;
};

/* The listener of kl_system_manager_v1's event, as libwayland calls it. */
struct system_manager_listener {
	void (*capabilities)(void *data, struct wl_proxy *proxy, uint32_t bits);
};

/* The listener of kl_system_network_v1's events, in their order. */
struct system_network_listener {
	void (*state)(void *data, struct wl_proxy *proxy, uint32_t reachable, uint32_t connected, uint32_t kind, const char *interface, const char *wired, uint32_t wifi, const char *wifi_interface, const char *ssid);
	void (*access_point)(void *data, struct wl_proxy *proxy, const char *ssid, int32_t rssi, uint32_t secured);
	void (*scan_done)(void *data, struct wl_proxy *proxy);
	void (*link)(void *data, struct wl_proxy *proxy, const char *name, uint32_t flags, const char *address, const char *netmask, const char *hardware, uint32_t mtu, uint32_t received_high, uint32_t received_low, uint32_t sent_high, uint32_t sent_low);
	void (*dns)(void *data, struct wl_proxy *proxy, const char *address);
	void (*saved_network)(void *data, struct wl_proxy *proxy, const char *ssid);
	void (*details_done)(void *data, struct wl_proxy *proxy);
	void (*done)(void *data, struct wl_proxy *proxy, uint32_t serial);
	void (*result)(void *data, struct wl_proxy *proxy, uint32_t request, uint32_t applied, uint32_t saved);
	void (*wired)(void *data, struct wl_proxy *proxy, const char *name, uint32_t mode, const char *router);
	void (*link_speed)(void *data, struct wl_proxy *proxy, const char *name, uint32_t mbps);
};

/* The listener of kl_system_audio_v1's events, in their order. */
struct system_audio_listener {
	void (*state)(void *data, struct wl_proxy *proxy, uint32_t reachable, uint32_t device, uint32_t rate, uint32_t channels, uint32_t left, uint32_t right, uint32_t muted);
	void (*done)(void *data, struct wl_proxy *proxy, uint32_t serial);
	void (*result)(void *data, struct wl_proxy *proxy, uint32_t request, uint32_t applied, uint32_t saved);
};

/* The listener of kl_system_power_v1's events, in their order. */
struct system_power_listener {
	void (*state)(void *data, struct wl_proxy *proxy, uint32_t source, int32_t percent, uint32_t charging, uint32_t actions);
	void (*done)(void *data, struct wl_proxy *proxy, uint32_t serial);
	void (*result)(void *data, struct wl_proxy *proxy, uint32_t request, uint32_t applied, uint32_t saved);
};

/* The listener of kl_system_account_v1's events (refused since version 8, ws089-p026; enrolled since 11, ws172-p002; key and touch since 14, ws172-p003). */
struct system_account_listener {
	void (*result)(void *data, struct wl_proxy *proxy, uint32_t request, uint32_t applied, uint32_t saved);
	void (*refused)(void *data, struct wl_proxy *proxy, uint32_t request, const char *reason);
	void (*enrolled)(void *data, struct wl_proxy *proxy, uint32_t pin, uint32_t keys);
	void (*key)(void *data, struct wl_proxy *proxy, const char *ref, const char *label);
	void (*touch)(void *data, struct wl_proxy *proxy, uint32_t request);
};

/* The listener of kl_system_sharing_v1's events (ws089-p025), in their order. */
struct system_sharing_listener {
	void (*state)(void *data, struct wl_proxy *proxy, uint32_t available, uint32_t enabled, uint32_t running, uint32_t port, uint32_t allowed, const char *fingerprint);
	void (*done)(void *data, struct wl_proxy *proxy, uint32_t serial);
	void (*result)(void *data, struct wl_proxy *proxy, uint32_t request, uint32_t applied, uint32_t saved);
};

/* The listener of kl_system_notify_v1's events (ws156-p002), in their order. */
struct system_notify_listener {
	void (*posted)(void *data, struct wl_proxy *proxy, uint32_t request, uint32_t id);
	void (*activated)(void *data, struct wl_proxy *proxy, uint32_t id);
	void (*closed)(void *data, struct wl_proxy *proxy, uint32_t id, uint32_t reason);
	void (*result)(void *data, struct wl_proxy *proxy, uint32_t request, uint32_t applied, uint32_t saved);
};

/* The listener of kl_system_devices_v1's events, in their order. */
struct system_devices_listener {
	void (*device)(void *data, struct wl_proxy *proxy, const char *id, uint32_t kind, uint32_t state, const char *name, const char *location);
	void (*done)(void *data, struct wl_proxy *proxy, uint32_t serial);
	void (*result)(void *data, struct wl_proxy *proxy, uint32_t request, uint32_t applied, uint32_t saved);
	void (*busy)(void *data, struct wl_proxy *proxy, uint32_t request, const char *program);
	void (*volume)(void *data, struct wl_proxy *proxy, const char *id, const char *fs, uint32_t bytes_high, uint32_t bytes_low);
};

static void system_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
static void system_notify_posted(void *data, struct wl_proxy *proxy, uint32_t request, uint32_t id);
static void system_notify_activated(void *data, struct wl_proxy *proxy, uint32_t id);
static void system_notify_closed(void *data, struct wl_proxy *proxy, uint32_t id, uint32_t reason);
static void system_global_remove(void *data, struct wl_registry *registry, uint32_t name);
static void system_capabilities(void *data, struct wl_proxy *proxy, uint32_t bits);
static void system_network_state(void *data, struct wl_proxy *proxy, uint32_t reachable, uint32_t connected, uint32_t kind, const char *interface, const char *wired, uint32_t wifi, const char *wifi_interface, const char *ssid);
static void system_access_point(void *data, struct wl_proxy *proxy, const char *ssid, int32_t rssi, uint32_t secured);
static void system_scan_done(void *data, struct wl_proxy *proxy);
static void system_link(void *data, struct wl_proxy *proxy, const char *name, uint32_t flags, const char *address, const char *netmask, const char *hardware, uint32_t mtu, uint32_t received_high, uint32_t received_low, uint32_t sent_high, uint32_t sent_low);
static void system_dns(void *data, struct wl_proxy *proxy, const char *address);
static void system_wired(void *data, struct wl_proxy *proxy, const char *name, uint32_t mode, const char *router);
static void system_link_speed(void *data, struct wl_proxy *proxy, const char *name, uint32_t mbps);
static void system_saved_network(void *data, struct wl_proxy *proxy, const char *ssid);
static void system_details_done(void *data, struct wl_proxy *proxy);
static void system_network_done(void *data, struct wl_proxy *proxy, uint32_t serial);
static void system_audio_state(void *data, struct wl_proxy *proxy, uint32_t reachable, uint32_t device, uint32_t rate, uint32_t channels, uint32_t left, uint32_t right, uint32_t muted);
static void system_audio_done(void *data, struct wl_proxy *proxy, uint32_t serial);
static void system_power_state(void *data, struct wl_proxy *proxy, uint32_t source, int32_t percent, uint32_t charging, uint32_t actions);
static void system_power_done(void *data, struct wl_proxy *proxy, uint32_t serial);
static void system_device(void *data, struct wl_proxy *proxy, const char *id, uint32_t kind, uint32_t state, const char *name, const char *location);
static void system_devices_done(void *data, struct wl_proxy *proxy, uint32_t serial);
static void system_devices_busy(void *data, struct wl_proxy *proxy, uint32_t request, const char *program);
static void system_devices_volume(void *data, struct wl_proxy *proxy, const char *id, const char *fs, uint32_t bytes_high, uint32_t bytes_low);
static void system_account_refused(void *data, struct wl_proxy *proxy, uint32_t request, const char *reason);
static void system_account_enrolled(void *data, struct wl_proxy *proxy, uint32_t pin, uint32_t keys);
static void system_account_key(void *data, struct wl_proxy *proxy, const char *ref, const char *label);
static void system_account_touch(void *data, struct wl_proxy *proxy, uint32_t request);
static int system_key_secret_valid(const char *secret);
static void system_result(void *data, struct wl_proxy *proxy, uint32_t request, uint32_t applied, uint32_t saved);
static void system_sharing_state(void *data, struct wl_proxy *proxy, uint32_t available, uint32_t enabled, uint32_t running, uint32_t port, uint32_t allowed, const char *fingerprint);
static void system_sharing_done(void *data, struct wl_proxy *proxy, uint32_t serial);
static int system_bind(struct kl_system *system);
static struct wl_proxy *system_make(struct kl_system *system, uint32_t bit, uint32_t opcode, const struct wl_interface *interface, const void *listener);
static void system_destroy(struct wl_proxy *proxy, uint32_t opcode);
static uint32_t system_number(struct kl_system *system, uint32_t *request);

/* The registry's callbacks while the manager is looked for. */
static const struct wl_registry_listener system_registry_listener = {
	system_global,
	system_global_remove
};

/* The manager's callback. */
static const struct system_manager_listener system_manager_listener = {
	system_capabilities
};

/* The network object's callbacks, which fill the view of the kl_system they are given. */
static const struct system_network_listener system_network_listener = {
	system_network_state,
	system_access_point,
	system_scan_done,
	system_link,
	system_dns,
	system_saved_network,
	system_details_done,
	system_network_done,
	system_result,
	system_wired,
	system_link_speed
};

/* The sound object's callbacks. */
static const struct system_audio_listener system_audio_listener = {
	system_audio_state,
	system_audio_done,
	system_result
};

/* The power object's callbacks. */
static const struct system_power_listener system_power_listener = {
	system_power_state,
	system_power_done,
	system_result
};

/* The account object's callbacks (ws160-p002, ws089-p026, ws172-p002, ws172-p003). */
static const struct system_account_listener system_account_listener = {
	system_result,
	system_account_refused,
	system_account_enrolled,
	system_account_key,
	system_account_touch
};

/* The sharing object's callbacks (ws089-p025). */
static const struct system_sharing_listener system_sharing_listener = {
	system_sharing_state,
	system_sharing_done,
	system_result
};

/* The notify object's callbacks (ws156-p002). */
static const struct system_notify_listener system_notify_listener = {
	system_notify_posted,
	system_notify_activated,
	system_notify_closed,
	system_result
};

/* The devices object's callbacks. */
static const struct system_devices_listener system_devices_listener = {
	system_device,
	system_devices_done,
	system_result,
	system_devices_busy,
	system_devices_volume
};

/*
 * Opens the system on a display, waiting once for its first state.
 */
struct kl_system *
kl_system_open(
	struct wl_display *display)
{
	struct kl_system *system;
	int error;

	/* A display to open it on. */
	if (display == NULL) {
		errno = EINVAL;
		return NULL;
	}

	/* Allocates the record; request numbers start at 1. */
	system = calloc(1, sizeof(*system));
	if (system == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	/* Nothing told yet. */
	system->display = display;
	system->next_request = 1U;
	system_view_init(&system->view);

	/* Binds the extension and takes the first state. */
	error = system_bind(system);
	if (error != 0) {
		kl_system_close(system);
		errno = error;
		return NULL;
	}

	/* What the system started with is not told as a change. */
	(void)system_view_take_changed(&system->view);

	/* Succeeded: the system is open. */
	return system;
}

/*
 * Closes the system.
 */
void
kl_system_close(
	struct kl_system *system)
{
	/* Nothing to close. */
	if (system == NULL)
		return;

	/* Destroys the objects first, then the manager. */
	system_destroy(system->network, KL_SYSTEM_NETWORK_DESTROY);
	system_destroy(system->audio, KL_SYSTEM_AUDIO_DESTROY);
	system_destroy(system->power, KL_SYSTEM_POWER_DESTROY);
	system_destroy(system->devices, KL_SYSTEM_DEVICES_DESTROY);
	system_destroy(system->account, KL_SYSTEM_ACCOUNT_DESTROY);
	system_destroy(system->sharing, KL_SYSTEM_SHARING_DESTROY);
	system_destroy(system->notify, KL_SYSTEM_NOTIFY_DESTROY);
	system_destroy(system->manager, KL_SYSTEM_MANAGER_DESTROY);

	/* Then the queue they lived on. */
	if (system->queue != NULL)
		wl_event_queue_destroy(system->queue);

	/* Frees the record. */
	free(system);
}

/*
 * Takes the compositor's events the display has read, and reports what
 * changed.
 */
int
kl_system_dispatch(
	struct kl_system *system,
	unsigned *changed)
{
	unsigned bits;
	int status;
	int error;

	/* Takes the compositor's events on the library's queue (they only fill the view). */
	if (!system->lost) {
		/* Dispatches the queue; a failure, or an error the display holds, means the compositor went. */
		status = wl_display_dispatch_queue_pending(system->display, system->queue);
		error = wl_display_get_error(system->display);
		if (status < 0 || error != 0)
			system->lost = 1U;
	}

	/* What changed since the last dispatch. */
	bits = system_view_take_changed(&system->view);
	if (changed != NULL)
		*changed = bits;

	/* The compositor went. */
	if (system->lost)
		return EPIPE;

	/* Succeeded: every change was taken. */
	return 0;
}

/*
 * Reports what the compositor offers.
 */
unsigned
kl_system_capabilities(
	const struct kl_system *system)
{
	unsigned bits;

	/* The objects it made, as KL_SYSTEM_HAS_* bits. */
	bits = 0U;
	if (system->network != NULL)
		bits |= KL_SYSTEM_HAS_NETWORK;
	if (system->audio != NULL)
		bits |= KL_SYSTEM_HAS_AUDIO;
	if (system->power != NULL)
		bits |= KL_SYSTEM_HAS_POWER;
	if (system->devices != NULL)
		bits |= KL_SYSTEM_HAS_DEVICES;
	if (system->account != NULL)
		bits |= KL_SYSTEM_HAS_ACCOUNT;
	if (system->sharing != NULL)
		bits |= KL_SYSTEM_HAS_SHARING;
	if (system->notify != NULL)
		bits |= KL_SYSTEM_HAS_NOTIFY;

	/* The administration of the accounts, offered with the account to a manager bound at version 8 (ws089-p026). */
	if (system->account != NULL && (system->view.capabilities & KL_SYSTEM_CAPABILITY_ADMINISTER) != 0U && system->manager_version >= KL_SYSTEM_SINCE_ADMINISTER)
		bits |= KL_SYSTEM_HAS_ADMINISTER;

	/* The PIN, offered with the account to a manager bound at version 10 where a session manager runs (ws163-p003). */
	if (system->account != NULL && (system->view.capabilities & KL_SYSTEM_CAPABILITY_PIN) != 0U && system->manager_version >= KL_SYSTEM_SINCE_PIN)
		bits |= KL_SYSTEM_HAS_PIN;

	/* The security keys, offered with the PIN to a manager bound at version 14 (ws172-p003). */
	if ((bits & KL_SYSTEM_HAS_PIN) != 0U && system->manager_version >= KL_SYSTEM_SINCE_KEYS)
		bits |= KL_SYSTEM_HAS_KEYS;

	/* The monitor, offered to a manager bound at version 2 (WS134 p012). */
	if ((system->view.capabilities & KL_SYSTEM_CAPABILITY_MONITOR) != 0U && system->manager_version >= 2U)
		bits |= KL_SYSTEM_HAS_MONITOR;
	return bits;
}

/*
 * Makes a monitor object (WS134 p012, for system-monitor.c) on the
 * library's queue, its events to the listener with data.  Returns NULL
 * when the compositor offers none.
 */
struct wl_proxy *
system_monitor_make(
	struct kl_system *system,
	uint32_t period_ms,
	const void *listener,
	void *data)
{
	struct wl_proxy *proxy;
	unsigned bits;

	/* Not offered, or the compositor went. */
	bits = kl_system_capabilities(system);
	if ((bits & KL_SYSTEM_HAS_MONITOR) == 0U)
		return NULL;
	if (system->lost)
		return NULL;

	/* The object, under a new ID, with its period. */
	proxy = wl_proxy_marshal_constructor(system->manager, KL_SYSTEM_MANAGER_GET_MONITOR, &kl_system_monitor_v1_interface, NULL, period_ms);
	if (proxy == NULL)
		return NULL;

	/* Its events to the monitor; sent at once, so that the samples start. */
	(void)wl_proxy_add_listener(proxy, (void (**)(void))listener, data);
	(void)wl_display_flush(system->display);
	return proxy;
}

/*
 * Takes one answered request.
 */
int
kl_system_take_result(
	struct kl_system *system,
	uint32_t *request,
	int *error)
{
	int taken;

	/* The oldest answer, if any. */
	taken = system_view_take_result(&system->view, request, error);

	/* 1 with one, 0 without. */
	return taken;
}

/*
 * Copies the network's state.
 */
void
kl_system_network_get_state(
	const struct kl_system *system,
	struct kl_network_state *state)
{
	/* The state in effect. */
	*state = system->view.network;
}

/*
 * Copies up to capacity networks of the last scan.
 */
size_t
kl_system_network_get_scan(
	const struct kl_system *system,
	struct kl_network_ap *aps,
	size_t capacity)
{
	size_t count;

	/* As many as there are and fit. */
	count = system->view.scan_count;
	if (count > capacity)
		count = capacity;
	memcpy(aps, system->view.scan, count * sizeof(aps[0]));

	/* Succeeded: the networks are copied. */
	return count;
}

/*
 * Asks for a network request.
 */
int
kl_system_network_request(
	struct kl_system *system,
	unsigned what,
	const char *ssid,
	uint32_t *request)
{
	const char *named;
	uint32_t number;
	size_t length;

	/* The network object. */
	if (system->network == NULL || system->lost)
		return ENOTSUP;

	/* A request of the protocol's; a join names a network that fits. */
	if (what < KL_NETWORK_SCAN || what > KL_NETWORK_WIFI_OFF)
		return EINVAL;
	named = "";
	if (what == KL_NETWORK_JOIN) {
		if (ssid == NULL)
			return EINVAL;
		length = strlen(ssid);
		if (length == 0U || length >= KL_NETWORK_SSID_MAX)
			return EINVAL;
		named = ssid;
	}

	/* Sent with the application's next flush. */
	number = system_number(system, request);
	wl_proxy_marshal(system->network, KL_SYSTEM_NETWORK_REQUEST, number, (uint32_t)what, named);

	/* Succeeded: the answer comes as a result. */
	return 0;
}

/*
 * Asks for a Wi-Fi network's key to be saved and the network joined.
 */
int
kl_system_network_save_key(
	struct kl_system *system,
	const char *ssid,
	const char *key,
	uint32_t *request)
{
	uint32_t number;
	size_t ssid_length;
	size_t key_length;

	/* The network object. */
	if (system->network == NULL || system->lost)
		return ENOTSUP;

	/* A network and a key within their bounds. */
	if (ssid == NULL || key == NULL)
		return EINVAL;
	ssid_length = strlen(ssid);
	key_length = strlen(key);
	if (ssid_length == 0U || ssid_length >= KL_NETWORK_SSID_MAX)
		return EINVAL;
	if (key_length < KL_NETWORK_KEY_MIN || key_length > KL_NETWORK_KEY_MAX)
		return EINVAL;

	/* Sent with the application's next flush (libwayland copies the key into its buffer). */
	number = system_number(system, request);
	wl_proxy_marshal(system->network, KL_SYSTEM_NETWORK_SAVE_KEY, number, ssid, key);

	/* Succeeded: the answer comes once the network is joined. */
	return 0;
}

/*
 * Asks the compositor to keep the radios scanning while the application
 * shows the networks around, or no longer.
 */
int
kl_system_network_set_scanning(
	struct kl_system *system,
	unsigned on)
{
	uint32_t asked;

	/* The network object, of a compositor that knows the request (version 3, ws089-p021). */
	if (system->network == NULL || system->lost)
		return ENOTSUP;
	if (system->manager_version < 3U)
		return ENOTSUP;

	/* On as 1, off as 0. */
	asked = 0U;
	if (on != 0U)
		asked = 1U;

	/* Sent with the application's next flush; it has no answer. */
	wl_proxy_marshal(system->network, KL_SYSTEM_NETWORK_SET_SCANNING, asked);

	/* Succeeded: the compositor is told. */
	return 0;
}

/*
 * Asks for a wired interface to be configured (ws089-p022).
 */
int
kl_system_network_configure_wired(
	struct kl_system *system,
	const struct kl_network_wired_config *config,
	uint32_t *request)
{
	const char *texts[6];
	const char *end;
	uint32_t number;
	size_t index;

	/* The network object, of a compositor that knows the request (version 6). */
	if (system->network == NULL || system->lost)
		return ENOTSUP;
	if (system->manager_version < KL_SYSTEM_NETWORK_SINCE_WIRED)
		return ENOTSUP;

	/* A mode of the two, and every field ended within its room. */
	if (config->mode != KL_WIRED_DHCP && config->mode != KL_WIRED_STATIC)
		return EINVAL;
	texts[0] = config->interface;
	texts[1] = config->address;
	texts[2] = config->netmask;
	texts[3] = config->router;
	texts[4] = config->dns[0];
	texts[5] = config->dns[1];
	for (index = 0; index < 6U; index++) {
		end = memchr(texts[index], '\0', KL_NETWORK_ADDRESS_MAX);
		if (end == NULL)
			return EINVAL;
	}

	/* Sent with the application's next flush; the compositor checks the values. */
	number = system_number(system, request);
	wl_proxy_marshal(system->network, KL_SYSTEM_NETWORK_CONFIGURE_WIRED, number, config->interface, config->mode, config->address, config->netmask, config->router, config->dns[0], config->dns[1]);

	/* Succeeded: the answer comes once it is applied or refused. */
	return 0;
}

/*
 * Posts a notification (ws156-p002): its number comes as a
 * KL_NOTIFY_POSTED event for the request, or a refusal as its result.
 */
int
kl_system_notify(
	struct kl_system *system,
	const struct kl_notification *notification,
	uint32_t *request)
{
	const char *app;
	const char *title;
	const char *body;
	uint32_t number;

	/* The notify object and the words. */
	if (system == NULL || notification == NULL)
		return EINVAL;
	if (system->notify == NULL || system->lost)
		return ENOTSUP;
	app = notification->app;
	if (app == NULL)
		app = "";
	title = notification->title;
	if (title == NULL)
		title = "";
	body = notification->body;
	if (body == NULL)
		body = "";

	/* Sent with the application's next flush. */
	number = system_number(system, request);
	wl_proxy_marshal(system->notify, KL_SYSTEM_NOTIFY_POST, number, notification->replaces, app, title, body, (uint32_t)notification->flags);

	/* Succeeded: the number comes later. */
	return 0;
}

/*
 * Takes back a notification the application posted (KL_NOTIFY_CLOSED,
 * KL_NOTIFY_WITHDRAWN, follows).
 */
int
kl_system_notify_withdraw(
	struct kl_system *system,
	uint32_t id,
	uint32_t *request)
{
	uint32_t number;

	/* The notify object. */
	if (system == NULL)
		return EINVAL;
	if (system->notify == NULL || system->lost)
		return ENOTSUP;

	/* Sent with the application's next flush. */
	number = system_number(system, request);
	wl_proxy_marshal(system->notify, KL_SYSTEM_NOTIFY_WITHDRAW, number, id);

	/* Succeeded: the answer comes later. */
	return 0;
}

/*
 * Takes the oldest notification event: 1 with it, 0 when none waits.
 */
int
kl_system_take_notify_event(
	struct kl_system *system,
	struct kl_notify_event *event)
{
	/* The view's ring. */
	return system_view_take_notify_event(&system->view, event);
}

/*
 * Copies Remote Login's state (ws089-p025).
 */
void
kl_system_sharing_get_state(
	const struct kl_system *system,
	struct kl_sharing_state *state)
{
	/* The state last done. */
	*state = system->view.sharing;
}

/*
 * Turns Remote Login on or off.
 */
int
kl_system_sharing_set_ssh(
	struct kl_system *system,
	unsigned on,
	uint32_t *request)
{
	uint32_t number;
	uint32_t asked;

	/* The sharing object. */
	if (system->sharing == NULL || system->lost)
		return ENOTSUP;

	/* On as 1, off as 0, sent with the application's next flush. */
	asked = 0U;
	if (on != 0U)
		asked = 1U;
	number = system_number(system, request);
	wl_proxy_marshal(system->sharing, KL_SYSTEM_SHARING_SET_SSH, number, asked);

	/* Succeeded: the state and the answer come later. */
	return 0;
}

/*
 * Reads Remote Login's state again.
 */
int
kl_system_sharing_query(
	struct kl_system *system,
	uint32_t *request)
{
	uint32_t number;

	/* The sharing object. */
	if (system->sharing == NULL || system->lost)
		return ENOTSUP;

	/* Sent with the application's next flush. */
	number = system_number(system, request);
	wl_proxy_marshal(system->sharing, KL_SYSTEM_SHARING_QUERY, number);

	/* Succeeded: the state and the answer come later. */
	return 0;
}

/*
 * Asks for the network's details.
 */
int
kl_system_network_query_details(
	struct kl_system *system,
	uint32_t *request)
{
	uint32_t number;

	/* The network object. */
	if (system->network == NULL || system->lost)
		return ENOTSUP;

	/* Sent with the application's next flush. */
	number = system_number(system, request);
	wl_proxy_marshal(system->network, KL_SYSTEM_NETWORK_QUERY_DETAILS, number);

	/* Succeeded: the details and the answer come later. */
	return 0;
}

/*
 * Copies up to capacity interfaces of the details last asked for.
 */
size_t
kl_system_network_get_links(
	const struct kl_system *system,
	struct kl_network_link *links,
	size_t capacity)
{
	size_t count;

	/* As many as there are and fit. */
	count = system->view.link_count;
	if (count > capacity)
		count = capacity;
	memcpy(links, system->view.links, count * sizeof(links[0]));

	/* Succeeded: the interfaces are copied. */
	return count;
}

/*
 * Copies up to capacity DNS servers of the details last asked for.
 */
size_t
kl_system_network_get_dns(
	const struct kl_system *system,
	char (*servers)[KL_NETWORK_ADDRESS_MAX],
	size_t capacity)
{
	size_t count;

	/* As many as there are and fit. */
	count = system->view.dns_count;
	if (count > capacity)
		count = capacity;
	memcpy(servers, system->view.dns, count * sizeof(servers[0]));

	/* Succeeded: the servers are copied. */
	return count;
}

/*
 * Copies up to capacity saved networks of the details last asked for.
 */
size_t
kl_system_network_get_saved(
	const struct kl_system *system,
	char (*ssids)[KL_NETWORK_SSID_MAX],
	size_t capacity)
{
	size_t count;

	/* As many as there are and fit. */
	count = system->view.saved_count;
	if (count > capacity)
		count = capacity;
	memcpy(ssids, system->view.saved, count * sizeof(ssids[0]));

	/* Succeeded: the networks are copied. */
	return count;
}

/*
 * Copies the sound output's state.
 */
void
kl_system_audio_get_state(
	const struct kl_system *system,
	struct kl_audio_state *state)
{
	/* The state in effect. */
	*state = system->view.audio;
}

/*
 * Asks for each channel's volume and the mute.
 */
int
kl_system_audio_set_volume(
	struct kl_system *system,
	unsigned left,
	unsigned right,
	unsigned muted,
	uint32_t *request)
{
	uint32_t number;

	/* The sound object. */
	if (system->audio == NULL || system->lost)
		return ENOTSUP;

	/* Volumes and a mute within their bounds. */
	if (left > 100U || right > 100U || muted > 1U)
		return EINVAL;

	/* Sent with the application's next flush. */
	number = system_number(system, request);
	wl_proxy_marshal(system->audio, KL_SYSTEM_AUDIO_SET_VOLUME, number, (uint32_t)left, (uint32_t)right, (uint32_t)muted);

	/* Succeeded: the answer comes as a result, the volume as a new state. */
	return 0;
}

/*
 * Asks for the short feedback sound.
 */
int
kl_system_audio_feedback(
	struct kl_system *system,
	uint32_t *request)
{
	uint32_t number;

	/* The sound object. */
	if (system->audio == NULL || system->lost)
		return ENOTSUP;

	/* Sent with the application's next flush. */
	number = system_number(system, request);
	wl_proxy_marshal(system->audio, KL_SYSTEM_AUDIO_FEEDBACK, number);

	/* Succeeded: the answer comes as a result. */
	return 0;
}

/*
 * Copies the power's state.
 */
void
kl_system_power_get_state(
	const struct kl_system *system,
	struct kl_power_state *state)
{
	/* The state in effect. */
	*state = system->view.power;
}

/*
 * Asks for a power action.
 */
int
kl_system_power_action(
	struct kl_system *system,
	unsigned action,
	uint32_t *request)
{
	uint32_t number;

	/* The power object. */
	if (system->power == NULL || system->lost)
		return ENOTSUP;

	/* An action of the protocol's. */
	if (action < KL_POWER_POWEROFF || action > KL_POWER_SUSPEND)
		return EINVAL;

	/* Sent with the application's next flush. */
	number = system_number(system, request);
	wl_proxy_marshal(system->power, KL_SYSTEM_POWER_ACTION, number, (uint32_t)action);

	/* Succeeded: the answer comes as a result. */
	return 0;
}

/*
 * Copies up to capacity removable devices.
 */
size_t
kl_system_devices_get(
	const struct kl_system *system,
	struct kl_device *devices,
	size_t capacity)
{
	size_t count;

	/* As many as there are and fit. */
	count = system->view.device_count;
	if (count > capacity)
		count = capacity;
	memcpy(devices, system->view.devices, count * sizeof(devices[0]));

	/* Succeeded: the devices are copied. */
	return count;
}

/*
 * Copies a listed device's file system and size.
 */
int
kl_system_devices_info(
	const struct kl_system *system,
	const char *id,
	struct kl_device_info *info)
{
	size_t index;
	int same;

	/* Nothing found yet. */
	memset(info, 0, sizeof(*info));
	if (system == NULL || id == NULL)
		return 0;

	/* The device of that ID. */
	for (index = 0U; index < system->view.device_count; index++) {
		same = strcmp(system->view.devices[index].id, id);
		if (same == 0) {
			*info = system->view.device_infos[index];
			return 1;
		}
	}

	/* Not in the list. */
	return 0;
}

/*
 * Asks for a removable device to be ejected.
 */
int
kl_system_devices_eject(
	struct kl_system *system,
	const char *id,
	uint32_t *request)
{
	uint32_t number;
	size_t length;

	/* The devices object. */
	if (system->devices == NULL || system->lost)
		return ENOTSUP;

	/* A device's ID that fits. */
	if (id == NULL)
		return EINVAL;
	length = strlen(id);
	if (length == 0U || length >= KL_DEVICE_TEXT_MAX)
		return EINVAL;

	/* Sent with the application's next flush. */
	number = system_number(system, request);
	wl_proxy_marshal(system->devices, KL_SYSTEM_DEVICES_EJECT, number, id);

	/* Succeeded: the answer comes as a result. */
	return 0;
}

/*
 * Asks for a removable device to be mounted (ws132-p004).
 */
int
kl_system_devices_mount(
	struct kl_system *system,
	const char *id,
	uint32_t *request)
{
	uint32_t number;
	size_t length;

	/* The devices object of a compositor with the mount (version 5). */
	if (system->devices == NULL || system->lost)
		return ENOTSUP;
	if (system->manager_version < KL_SYSTEM_DEVICES_SINCE_MOUNT)
		return ENOTSUP;

	/* A device's ID that fits. */
	if (id == NULL)
		return EINVAL;
	length = strlen(id);
	if (length == 0U || length >= KL_DEVICE_TEXT_MAX)
		return EINVAL;

	/* Sent with the application's next flush. */
	number = system_number(system, request);
	wl_proxy_marshal(system->devices, KL_SYSTEM_DEVICES_MOUNT, number, id);

	/* Succeeded: the answer comes as a result. */
	return 0;
}

/*
 * Copies the program that keeps a device from being ejected, for a request
 * answered EBUSY.  Returns 1 with it, 0 when the compositor named none.
 */
int
kl_system_devices_busy_program(
	const struct kl_system *system,
	uint32_t request,
	char *program,
	size_t size)
{
	/* The program named for that request, if any. */
	if (system == NULL || program == NULL || size == 0U)
		return 0;
	if (system->view.busy_request != request || system->view.busy_program[0] == '\0')
		return 0;

	/* Succeeded: the program. */
	system_view_copy(program, size, system->view.busy_program);
	return 1;
}

/*
 * Asks for the user's password to be changed (ws160-p002).  Neither
 * password is kept here: they go out with the application's next flush.
 */
int
kl_system_account_set_password(
	struct kl_system *system,
	const char *current,
	const char *fresh,
	uint32_t *request)
{
	uint32_t number;
	size_t current_length;
	size_t fresh_length;

	/* The account object. */
	if (system->account == NULL || system->lost)
		return ENOTSUP;

	/* Two passwords that are not empty and fit. */
	if (current == NULL || fresh == NULL)
		return EINVAL;
	current_length = strlen(current);
	fresh_length = strlen(fresh);
	if (current_length == 0U || fresh_length == 0U)
		return EINVAL;
	if (current_length > KL_SYSTEM_PASSWORD_MAX || fresh_length > KL_SYSTEM_PASSWORD_MAX)
		return EINVAL;

	/* Sent with the application's next flush. */
	number = system_number(system, request);
	wl_proxy_marshal(system->account, KL_SYSTEM_ACCOUNT_SET_PASSWORD, number, current, fresh);

	/* Succeeded: the answer comes as a result. */
	return 0;
}

/*
 * Asks for the user's PIN to be set, or removed with an empty pin
 * (ws163-p003).  Neither the password nor the PIN is kept here: they go
 * out with the application's next flush.
 */
int
kl_system_account_set_pin(
	struct kl_system *system,
	const char *current,
	const char *pin,
	uint32_t *request)
{
	uint32_t number;
	size_t current_length;
	size_t pin_length;
	size_t current_clean;

	/* The PIN offered. */
	if (system->account == NULL || system->lost || system->manager_version < KL_SYSTEM_SINCE_PIN)
		return ENOTSUP;
	if ((system->view.capabilities & KL_SYSTEM_CAPABILITY_PIN) == 0U)
		return ENOTSUP;

	/* A password of one line that fits, and a PIN that fits (the compositor checks its digits). */
	if (current == NULL || pin == NULL)
		return EINVAL;
	current_length = strlen(current);
	pin_length = strlen(pin);
	current_clean = strcspn(current, "\n");
	if (current_length == 0U || current_length > KL_SYSTEM_PASSWORD_MAX || current_clean != current_length)
		return EINVAL;
	if (pin_length > KL_SYSTEM_PASSWORD_MAX)
		return EINVAL;

	/* A refusal of an earlier request is not this one's. */
	system->view.refused_request = 0U;
	system->view.refused_reason[0] = '\0';

	/* Sent with the application's next flush. */
	number = system_number(system, request);
	wl_proxy_marshal(system->account, KL_SYSTEM_ACCOUNT_SET_PIN, number, current, pin);

	/* Succeeded: the answer comes as a result. */
	return 0;
}

/*
 * Gives what the user has enrolled (ws172-p002), once the compositor told it.
 */
int
kl_system_account_enrolled(
	const struct kl_system *system,
	unsigned *pin,
	unsigned *keys)
{
	/* Nothing before the compositor told it. */
	*pin = 0U;
	*keys = 0U;
	if (system == NULL || !system->view.enrolled_known)
		return 0;

	/* Succeeded: whether a PIN is set, and the keys. */
	*pin = system->view.enrolled_pin;
	*keys = system->view.enrolled_keys;
	return 1;
}

/*
 * Copies the user's security keys the compositor last told (ws172-p003).
 */
size_t
kl_system_account_keys(
	const struct kl_system *system,
	struct kl_system_key *keys,
	size_t capacity)
{
	size_t count;

	/* None before the compositor told them. */
	if (system == NULL)
		return 0U;

	/* As many as fit. */
	count = system->view.key_count;
	if (count > capacity)
		count = capacity;
	memcpy(keys, system->view.keys, count * sizeof(keys[0]));
	return system->view.key_count;
}

/*
 * Asks for the security key plugged in to be registered (ws172-p003).
 * Nothing secret is kept here: it goes out with the next flush.
 */
int
kl_system_account_add_key(
	struct kl_system *system,
	const char *password,
	const char *label,
	const char *pin,
	uint32_t *request)
{
	const char *colon;
	uint32_t number;
	unsigned offered;
	size_t length;
	int valid;

	/* The keys offered. */
	if (system == NULL || system->account == NULL || system->lost)
		return ENOTSUP;
	offered = kl_system_capabilities(system);
	if ((offered & KL_SYSTEM_HAS_KEYS) == 0U)
		return ENOTSUP;

	/* One line each; a label of 1 to 32 bytes without a colon. */
	if (password == NULL || label == NULL || pin == NULL)
		return EINVAL;
	valid = system_key_secret_valid(password) && system_key_secret_valid(pin) && system_key_secret_valid(label);
	length = strlen(label);
	colon = strchr(label, ':');
	if (!valid || length > KL_SYSTEM_KEY_LABEL_MAX || colon != NULL)
		return EINVAL;

	/* A refusal or a touch of an earlier request is not this one's. */
	system->view.refused_request = 0U;
	system->view.refused_reason[0] = '\0';
	system->view.touched = 0U;

	/* Sent with the application's next flush. */
	number = system_number(system, request);
	wl_proxy_marshal(system->account, KL_SYSTEM_ACCOUNT_ADD_KEY, number, password, label, pin);

	/* Succeeded: the answer comes as a result. */
	return 0;
}

/*
 * Asks for one of the user's keys to be removed by its reference (ws172-p003).
 */
int
kl_system_account_remove_key(
	struct kl_system *system,
	const char *password,
	const char *ref,
	uint32_t *request)
{
	uint32_t number;
	unsigned offered;
	size_t length;
	int valid;

	/* The keys offered. */
	if (system == NULL || system->account == NULL || system->lost)
		return ENOTSUP;
	offered = kl_system_capabilities(system);
	if ((offered & KL_SYSTEM_HAS_KEYS) == 0U)
		return ENOTSUP;

	/* The password, and a reference that fits. */
	if (password == NULL || ref == NULL)
		return EINVAL;
	valid = system_key_secret_valid(password) && system_key_secret_valid(ref);
	length = strlen(ref);
	if (!valid || length == 0U || length > KL_SYSTEM_KEY_REF_MAX)
		return EINVAL;

	/* A refusal of an earlier request is not this one's. */
	system->view.refused_request = 0U;
	system->view.refused_reason[0] = '\0';

	/* Sent with the application's next flush. */
	number = system_number(system, request);
	wl_proxy_marshal(system->account, KL_SYSTEM_ACCOUNT_REMOVE_KEY, number, password, ref);

	/* Succeeded: the answer comes as a result. */
	return 0;
}

/*
 * Gives, once, the request whose key waits to be touched (ws172-p003):
 * 1 with it, 0 when no touch came since.
 */
int
kl_system_account_touched(
	struct kl_system *system,
	uint32_t *request)
{
	/* None. */
	if (system == NULL || !system->view.touched)
		return 0;

	/* Taken. */
	system->view.touched = 0U;
	*request = system->view.touched_request;
	return 1;
}

/*
 * Asks for an administrator's change of the people's accounts (ws089-p026):
 * the caller's password and the operation's lines.  Neither is kept here:
 * they go out with the application's next flush.
 */
int
kl_system_account_administer(
	struct kl_system *system,
	const char *password,
	const char *operation,
	uint32_t *request)
{
	uint32_t number;
	size_t password_length;
	size_t operation_length;
	size_t password_clean;

	/* The administration offered. */
	if (system->account == NULL || system->lost || system->manager_version < KL_SYSTEM_SINCE_ADMINISTER)
		return ENOTSUP;
	if ((system->view.capabilities & KL_SYSTEM_CAPABILITY_ADMINISTER) == 0U)
		return ENOTSUP;

	/* A password of one line that fits, and an operation that fits. */
	if (password == NULL || operation == NULL)
		return EINVAL;
	password_length = strlen(password);
	operation_length = strlen(operation);
	password_clean = strcspn(password, "\n");
	if (password_length == 0U || password_length > KL_SYSTEM_PASSWORD_MAX || password_clean != password_length)
		return EINVAL;
	if (operation_length == 0U || operation_length > KL_SYSTEM_OPERATION_MAX)
		return EINVAL;

	/* A refusal of an earlier request is not this one's. */
	system->view.refused_request = 0U;
	system->view.refused_reason[0] = '\0';

	/* Sent with the application's next flush. */
	number = system_number(system, request);
	wl_proxy_marshal(system->account, KL_SYSTEM_ACCOUNT_ADMINISTER, number, password, operation);

	/* Succeeded: the answer comes as a result (and a refusal's word before it). */
	return 0;
}

/*
 * Copies the word of a refused administration's request (ws089-p026).
 * Returns 1 with it, 0 when the compositor sent none for that request.
 */
int
kl_system_account_refusal(
	const struct kl_system *system,
	uint32_t request,
	char *reason,
	size_t size)
{
	/* The word sent for that request, if any. */
	if (system == NULL || reason == NULL || size == 0U)
		return 0;
	if (system->view.refused_request != request || system->view.refused_reason[0] == '\0')
		return 0;

	/* Succeeded: the word. */
	system_view_copy(reason, size, system->view.refused_reason);
	return 1;
}

/* Notes the system manager's global. */
static void
system_global(
	void *data,
	struct wl_registry *registry,
	uint32_t name,
	const char *interface,
	uint32_t version)
{
	struct system_search *search;
	int differs;

	UNUSED_PARAMETER(registry);

	/* The search the registry was given. */
	search = data;

	/* Notes only the system manager's name and version. */
	differs = strcmp(interface, KL_SYSTEM_MANAGER_NAME);
	if (differs == 0) {
		search->name = name;
		search->version = version;
	}
}

/* A global that goes is not the search's concern. */
static void
system_global_remove(
	void *data,
	struct wl_registry *registry,
	uint32_t name)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(registry);
	UNUSED_PARAMETER(name);
}

/* Keeps what the manager offers. */
static void
system_capabilities(
	void *data,
	struct wl_proxy *proxy,
	uint32_t bits)
{
	struct kl_system *system;

	UNUSED_PARAMETER(proxy);

	/* The bits, for the objects to make. */
	system = data;
	system->view.capabilities = bits;
}

/* Keeps the network's state, in effect at its done. */
static void
system_network_state(
	void *data,
	struct wl_proxy *proxy,
	uint32_t reachable,
	uint32_t connected,
	uint32_t kind,
	const char *interface,
	const char *wired,
	uint32_t wifi,
	const char *wifi_interface,
	const char *ssid)
{
	struct kl_system *system;
	struct kl_network_state state;

	UNUSED_PARAMETER(proxy);

	/* The state as the application's record. */
	system = data;
	memset(&state, 0, sizeof(state));
	state.reachable = reachable;
	state.connected = connected;
	state.kind = kind;
	system_view_copy(state.interface, sizeof(state.interface), interface);
	system_view_copy(state.wired, sizeof(state.wired), wired);
	state.wifi = wifi;
	system_view_copy(state.wifi_interface, sizeof(state.wifi_interface), wifi_interface);
	system_view_copy(state.ssid, sizeof(state.ssid), ssid);

	/* Pending until the done. */
	system_view_network_state(&system->view, &state);
}

/* Adds a network of a scan. */
static void
system_access_point(
	void *data,
	struct wl_proxy *proxy,
	const char *ssid,
	int32_t rssi,
	uint32_t secured)
{
	struct kl_system *system;
	struct kl_network_ap ap;

	UNUSED_PARAMETER(proxy);

	/* The network as the application's record. */
	system = data;
	memset(&ap, 0, sizeof(ap));
	system_view_copy(ap.ssid, sizeof(ap.ssid), ssid);
	ap.rssi = rssi;
	ap.secured = secured;

	/* Added to the pending scan. */
	system_view_access_point(&system->view, &ap);
}

/* Ends a scan's list. */
static void
system_scan_done(
	void *data,
	struct wl_proxy *proxy)
{
	struct kl_system *system;

	UNUSED_PARAMETER(proxy);

	/* The list is whole. */
	system = data;
	system_view_scan_done(&system->view);
}

/* Adds an interface of the details. */
static void
system_link(
	void *data,
	struct wl_proxy *proxy,
	const char *name,
	uint32_t flags,
	const char *address,
	const char *netmask,
	const char *hardware,
	uint32_t mtu,
	uint32_t received_high,
	uint32_t received_low,
	uint32_t sent_high,
	uint32_t sent_low)
{
	struct kl_system *system;
	struct kl_network_link link;

	UNUSED_PARAMETER(proxy);

	/* The interface as the application's record: its flags apart, its counters whole. */
	system = data;
	memset(&link, 0, sizeof(link));
	system_view_copy(link.name, sizeof(link.name), name);
	if ((flags & KL_SYSTEM_LINK_UP) != 0U)
		link.up = 1U;
	if ((flags & KL_SYSTEM_LINK_RUNNING) != 0U)
		link.running = 1U;
	if ((flags & KL_SYSTEM_LINK_LOOPBACK) != 0U)
		link.loopback = 1U;
	system_view_copy(link.address, sizeof(link.address), address);
	system_view_copy(link.netmask, sizeof(link.netmask), netmask);
	system_view_copy(link.hardware, sizeof(link.hardware), hardware);
	link.mtu = mtu;
	link.received_bytes = ((uint64_t)received_high << 32) | received_low;
	link.sent_bytes = ((uint64_t)sent_high << 32) | sent_low;

	/* Added to the pending details. */
	system_view_link(&system->view, &link);
}

/* Gives a wired interface of the details its configuration. */
static void
system_wired(
	void *data,
	struct wl_proxy *proxy,
	const char *name,
	uint32_t mode,
	const char *router)
{
	struct kl_system *system;

	UNUSED_PARAMETER(proxy);

	/* Given to the pending interface of that name. */
	system = data;
	system_view_wired(&system->view, name, mode, router);
}

/* Gives an interface of the details its link's speed (BUG-222). */
static void
system_link_speed(
	void *data,
	struct wl_proxy *proxy,
	const char *name,
	uint32_t mbps)
{
	struct kl_system *system;

	UNUSED_PARAMETER(proxy);

	/* Given to the pending interface of that name. */
	system = data;
	system_view_link_speed(&system->view, name, mbps);
}

/* Keeps Remote Login's state until its done (ws089-p025). */
static void
system_sharing_state(
	void *data,
	struct wl_proxy *proxy,
	uint32_t available,
	uint32_t enabled,
	uint32_t running,
	uint32_t port,
	uint32_t allowed,
	const char *fingerprint)
{
	struct kl_system *system;
	struct kl_sharing_state state;

	UNUSED_PARAMETER(proxy);

	/* The state as the application's record. */
	system = data;
	memset(&state, 0, sizeof(state));
	state.available = available;
	state.enabled = enabled;
	state.running = running;
	state.port = port;
	state.allowed = allowed;
	system_view_copy(state.fingerprint, sizeof(state.fingerprint), fingerprint);
	system_view_sharing_state(&system->view, &state);
}

/* Puts Remote Login's state into effect. */
static void
system_sharing_done(
	void *data,
	struct wl_proxy *proxy,
	uint32_t serial)
{
	struct kl_system *system;

	UNUSED_PARAMETER(proxy);
	UNUSED_PARAMETER(serial);

	/* The state, as one. */
	system = data;
	system_view_sharing_done(&system->view);
}

/* Adds a DNS server of the details. */
static void
system_dns(
	void *data,
	struct wl_proxy *proxy,
	const char *address)
{
	struct kl_system *system;

	UNUSED_PARAMETER(proxy);

	/* Added to the pending details. */
	system = data;
	system_view_dns(&system->view, address);
}

/* Adds a saved network of the details. */
static void
system_saved_network(
	void *data,
	struct wl_proxy *proxy,
	const char *ssid)
{
	struct kl_system *system;

	UNUSED_PARAMETER(proxy);

	/* Added to the pending details. */
	system = data;
	system_view_saved(&system->view, ssid);
}

/* Puts the details into effect. */
static void
system_details_done(
	void *data,
	struct wl_proxy *proxy)
{
	struct kl_system *system;

	UNUSED_PARAMETER(proxy);

	/* The details, as one state. */
	system = data;
	system_view_details_done(&system->view);
}

/* Puts the network's state and scan into effect. */
static void
system_network_done(
	void *data,
	struct wl_proxy *proxy,
	uint32_t serial)
{
	struct kl_system *system;

	UNUSED_PARAMETER(proxy);
	UNUSED_PARAMETER(serial);

	/* The network, as one state. */
	system = data;
	system_view_network_done(&system->view);
}

/* Keeps the sound's state, in effect at its done. */
static void
system_audio_state(
	void *data,
	struct wl_proxy *proxy,
	uint32_t reachable,
	uint32_t device,
	uint32_t rate,
	uint32_t channels,
	uint32_t left,
	uint32_t right,
	uint32_t muted)
{
	struct kl_system *system;
	struct kl_audio_state state;

	UNUSED_PARAMETER(proxy);

	/* The state as the application's record. */
	system = data;
	state.reachable = reachable;
	state.device = device;
	state.rate = rate;
	state.channels = channels;
	state.left = left;
	state.right = right;
	state.muted = muted;

	/* Pending until the done. */
	system_view_audio_state(&system->view, &state);
}

/* Puts the sound's state into effect. */
static void
system_audio_done(
	void *data,
	struct wl_proxy *proxy,
	uint32_t serial)
{
	struct kl_system *system;

	UNUSED_PARAMETER(proxy);
	UNUSED_PARAMETER(serial);

	/* The sound, as one state. */
	system = data;
	system_view_audio_done(&system->view);
}

/* Keeps the power's state, in effect at its done. */
static void
system_power_state(
	void *data,
	struct wl_proxy *proxy,
	uint32_t source,
	int32_t percent,
	uint32_t charging,
	uint32_t actions)
{
	struct kl_system *system;
	struct kl_power_state state;

	UNUSED_PARAMETER(proxy);

	/* The state as the application's record. */
	system = data;
	state.source = source;
	state.percent = percent;
	state.charging = charging;
	state.actions = actions;

	/* Pending until the done. */
	system_view_power_state(&system->view, &state);
}

/* Puts the power's state into effect. */
static void
system_power_done(
	void *data,
	struct wl_proxy *proxy,
	uint32_t serial)
{
	struct kl_system *system;

	UNUSED_PARAMETER(proxy);
	UNUSED_PARAMETER(serial);

	/* The power, as one state. */
	system = data;
	system_view_power_done(&system->view);
}

/* Adds a removable device. */
static void
system_device(
	void *data,
	struct wl_proxy *proxy,
	const char *id,
	uint32_t kind,
	uint32_t state,
	const char *name,
	const char *location)
{
	struct kl_system *system;
	struct kl_device device;

	UNUSED_PARAMETER(proxy);

	/* The device as the application's record. */
	system = data;
	memset(&device, 0, sizeof(device));
	system_view_copy(device.id, sizeof(device.id), id);
	device.kind = kind;
	device.state = state;
	system_view_copy(device.name, sizeof(device.name), name);
	system_view_copy(device.location, sizeof(device.location), location);

	/* Added to the pending list. */
	system_view_device(&system->view, &device);
}

/* Puts the devices into effect. */
static void
system_devices_done(
	void *data,
	struct wl_proxy *proxy,
	uint32_t serial)
{
	struct kl_system *system;

	UNUSED_PARAMETER(proxy);
	UNUSED_PARAMETER(serial);

	/* The devices, as one state. */
	system = data;
	system_view_devices_done(&system->view);
}

/* Keeps the program that keeps a volume from being ejected, for the busy result after it. */
static void
system_devices_busy(
	void *data,
	struct wl_proxy *proxy,
	uint32_t request,
	const char *program)
{
	struct kl_system *system;

	UNUSED_PARAMETER(proxy);

	/* The request and its program. */
	system = data;
	system->view.busy_request = request;
	system_view_copy(system->view.busy_program, sizeof(system->view.busy_program), program);
}

/* Keeps a device's file system and size (version 9), for the device just told. */
static void
system_devices_volume(
	void *data,
	struct wl_proxy *proxy,
	const char *id,
	const char *fs,
	uint32_t bytes_high,
	uint32_t bytes_low)
{
	struct kl_system *system;
	uint64_t bytes;

	UNUSED_PARAMETER(proxy);

	/* The size from its halves, kept with the pending device. */
	system = data;
	bytes = ((uint64_t)bytes_high << 32) | (uint64_t)bytes_low;
	system_view_device_info(&system->view, id, fs, bytes);
}

/* Keeps the word of a refused administration's request. */
static void
system_account_refused(
	void *data,
	struct wl_proxy *proxy,
	uint32_t request,
	const char *reason)
{
	struct kl_system *system;

	UNUSED_PARAMETER(proxy);

	/* The request and its word. */
	system = data;
	system->view.refused_request = request;
	system_view_copy(system->view.refused_reason, sizeof(system->view.refused_reason), reason);
}

/* Keeps what the user has enrolled (ws172-p002): whether a PIN is set, and how many security keys. */
static void
system_account_enrolled(
	void *data,
	struct wl_proxy *proxy,
	uint32_t pin,
	uint32_t keys)
{
	struct kl_system *system;

	UNUSED_PARAMETER(proxy);

	/* Known now, and changed. */
	system = data;
	system->view.enrolled_known = 1U;
	system->view.enrolled_pin = 0U;
	if (pin != 0U)
		system->view.enrolled_pin = 1U;
	system->view.enrolled_keys = keys;

	/* The keys told before it are the list now (ws172-p003). */
	memcpy(system->view.keys, system->view.keys_pending, system->view.keys_pending_count * sizeof(system->view.keys[0]));
	system->view.key_count = system->view.keys_pending_count;
	system->view.keys_pending_count = 0U;
	system->view.changed |= KL_SYSTEM_CHANGED_ENROLLED;
}

/* Keeps one of the user's keys until the enrolled that ends the list (ws172-p003). */
static void
system_account_key(
	void *data,
	struct wl_proxy *proxy,
	const char *ref,
	const char *label)
{
	struct kl_system *system;
	struct kl_system_key *key;

	UNUSED_PARAMETER(proxy);

	/* The next place, while there is room. */
	system = data;
	if (system->view.keys_pending_count >= KL_SYSTEM_KEYS_MAX)
		return;
	key = &system->view.keys_pending[system->view.keys_pending_count];
	system_view_copy(key->ref, sizeof(key->ref), ref);
	system_view_copy(key->label, sizeof(key->label), label);
	system->view.keys_pending_count++;
}

/* Keeps that a key waits to be touched for an addition (ws172-p003). */
static void
system_account_touch(
	void *data,
	struct wl_proxy *proxy,
	uint32_t request)
{
	struct kl_system *system;

	UNUSED_PARAMETER(proxy);

	/* The request, told once. */
	system = data;
	system->view.touched = 1U;
	system->view.touched_request = request;
	system->view.changed |= KL_SYSTEM_CHANGED_TOUCH;
}

/* Keeps an answered request of any object. */
static void
system_result(
	void *data,
	struct wl_proxy *proxy,
	uint32_t request,
	uint32_t applied,
	uint32_t saved)
{
	struct kl_system *system;

	UNUSED_PARAMETER(proxy);
	UNUSED_PARAMETER(saved);

	/* The answer, for kl_system_take_result. */
	system = data;
	system_view_result(&system->view, request, applied);
}

/* A notification's number for the request that posted it (ws156-p002). */
static void
system_notify_posted(
	void *data,
	struct wl_proxy *proxy,
	uint32_t request,
	uint32_t id)
{
	struct kl_system *system;
	struct kl_notify_event event;

	UNUSED_PARAMETER(proxy);

	/* For kl_system_take_notify_event. */
	system = data;
	memset(&event, 0, sizeof(event));
	event.kind = KL_NOTIFY_POSTED;
	event.request = request;
	event.id = id;
	system_view_notify_event(&system->view, &event);
}

/* A notification's body was clicked. */
static void
system_notify_activated(
	void *data,
	struct wl_proxy *proxy,
	uint32_t id)
{
	struct kl_system *system;
	struct kl_notify_event event;

	UNUSED_PARAMETER(proxy);

	/* For kl_system_take_notify_event. */
	system = data;
	memset(&event, 0, sizeof(event));
	event.kind = KL_NOTIFY_ACTIVATED;
	event.id = id;
	system_view_notify_event(&system->view, &event);
}

/* A notification closed, and why. */
static void
system_notify_closed(
	void *data,
	struct wl_proxy *proxy,
	uint32_t id,
	uint32_t reason)
{
	struct kl_system *system;
	struct kl_notify_event event;

	UNUSED_PARAMETER(proxy);

	/* For kl_system_take_notify_event. */
	system = data;
	memset(&event, 0, sizeof(event));
	event.kind = KL_NOTIFY_CLOSED;
	event.id = id;
	event.reason = reason;
	system_view_notify_event(&system->view, &event);
}

/*
 * Binds the compositor's system manager on the library's queue, makes the
 * objects it offers and waits once for their first state.  Returns 0,
 * ENOTSUP without the extension, EPIPE when the compositor went, or
 * ENOMEM.
 */
static int
system_bind(
	struct kl_system *system)
{
	struct system_search search;
	struct wl_display *wrapper;
	struct wl_registry *registry;
	int bind_error;
	int status;

	/* Makes the library's queue. */
	system->queue = wl_display_create_queue(system->display);
	if (system->queue == NULL)
		return ENOMEM;

	/* Wraps the display for the search: what the wrapper makes lives on the queue. */
	wrapper = wl_proxy_create_wrapper(system->display);
	if (wrapper == NULL)
		return ENOMEM;
	wl_proxy_set_queue((struct wl_proxy *)wrapper, system->queue);

	/* Asks for the globals, announced to this search alone; the wrapper is not needed after. */
	search.name = 0U;
	search.version = 0U;
	registry = wl_display_get_registry(wrapper);
	wl_proxy_wrapper_destroy(wrapper);
	if (registry == NULL)
		return ENOMEM;

	/* Listens to the registry's globals and waits for every one to be announced. */
	status = wl_registry_add_listener(registry, &system_registry_listener, &search);
	if (status == 0)
		status = wl_display_roundtrip_queue(system->display, system->queue);
	if (status < 0) {
		wl_registry_destroy(registry);
		return EPIPE;
	}

	/* Without the system manager there is no extension (not Keiland, or another user's). */
	if (search.name == 0U) {
		wl_registry_destroy(registry);
		return ENOTSUP;
	}

	/* Binds the manager, on the queue as the registry is; the registry is not needed after. */
	system->manager_version = search.version;
	if (system->manager_version > KL_SYSTEM_MANAGER_VERSION)
		system->manager_version = KL_SYSTEM_MANAGER_VERSION;
	system->manager = wl_registry_bind(registry, search.name, &kl_system_manager_v1_interface, system->manager_version);
	bind_error = errno;
	wl_registry_destroy(registry);
	if (system->manager == NULL) {
		/* The library's reason (a version the interface table does not describe is EINVAL), not always memory. */
		if (bind_error == 0)
			bind_error = ENOMEM;
		return bind_error;
	}

	/* Listens to the manager and waits for what it offers. */
	(void)wl_proxy_add_listener(system->manager, (void (**)(void))&system_manager_listener, system);
	status = wl_display_roundtrip_queue(system->display, system->queue);
	if (status < 0)
		return EPIPE;

	/* Makes each object it offers, on the same queue. */
	system->network = system_make(system, KL_SYSTEM_CAPABILITY_NETWORK, KL_SYSTEM_MANAGER_GET_NETWORK, &kl_system_network_v1_interface, &system_network_listener);
	system->audio = system_make(system, KL_SYSTEM_CAPABILITY_AUDIO, KL_SYSTEM_MANAGER_GET_AUDIO, &kl_system_audio_v1_interface, &system_audio_listener);
	system->power = system_make(system, KL_SYSTEM_CAPABILITY_POWER, KL_SYSTEM_MANAGER_GET_POWER, &kl_system_power_v1_interface, &system_power_listener);
	system->devices = system_make(system, KL_SYSTEM_CAPABILITY_DEVICES, KL_SYSTEM_MANAGER_GET_DEVICES, &kl_system_devices_v1_interface, &system_devices_listener);

	/* The account, offered to a manager bound at version 4 (ws160-p002). */
	if (system->manager_version >= 4U)
		system->account = system_make(system, KL_SYSTEM_CAPABILITY_ACCOUNT, KL_SYSTEM_MANAGER_GET_ACCOUNT, &kl_system_account_v1_interface, &system_account_listener);

	/* Remote Login, offered to a manager bound at version 7 (ws089-p025). */
	if (system->manager_version >= KL_SYSTEM_SINCE_SHARING)
		system->sharing = system_make(system, KL_SYSTEM_CAPABILITY_SHARING, KL_SYSTEM_MANAGER_GET_SHARING, &kl_system_sharing_v1_interface, &system_sharing_listener);

	/* The notifications, offered to a manager bound at version 13 (ws156-p002). */
	if (system->manager_version >= KL_SYSTEM_SINCE_NOTIFY)
		system->notify = system_make(system, KL_SYSTEM_CAPABILITY_NOTIFY, KL_SYSTEM_MANAGER_GET_NOTIFY, &kl_system_notify_v1_interface, &system_notify_listener);

	/* Waits for their first state: each object's state and its done. */
	status = wl_display_roundtrip_queue(system->display, system->queue);
	if (status < 0)
		return EPIPE;

	/* Succeeded: the system has its first state. */
	return 0;
}

/* Makes one of the manager's objects when it is offered, listened to with the system; NULL otherwise. */
static struct wl_proxy *
system_make(
	struct kl_system *system,
	uint32_t bit,
	uint32_t opcode,
	const struct wl_interface *interface,
	const void *listener)
{
	struct wl_proxy *proxy;

	/* Not offered. */
	if ((system->view.capabilities & bit) == 0U)
		return NULL;

	/* The object, under a new ID. */
	proxy = wl_proxy_marshal_constructor(system->manager, opcode, interface, NULL);
	if (proxy == NULL)
		return NULL;

	/* Its events fill the view. */
	(void)wl_proxy_add_listener(proxy, (void (**)(void))listener, system);
	return proxy;
}

/* Destroys one of the system's objects, when it was made. */
static void
system_destroy(
	struct wl_proxy *proxy,
	uint32_t opcode)
{
	/* Not made. */
	if (proxy == NULL)
		return;

	/* The compositor is told, then the proxy goes. */
	wl_proxy_marshal(proxy, opcode);
	wl_proxy_destroy(proxy);
}

/*
 * Gives a request its number and the caller the number when asked for;
 * next_request moves so that each request has a number of its own to be
 * answered by.
 */
static uint32_t
system_number(
	struct kl_system *system,
	uint32_t *request)
{
	uint32_t number;

	/* The next number. */
	number = system->next_request;
	system->next_request++;
	if (request != NULL)
		*request = number;
	return number;
}

/* Tells whether a string goes as one line: not empty, at most KL_SYSTEM_PASSWORD_MAX bytes, no line end. */
static int
system_key_secret_valid(
	const char *secret)
{
	size_t length;
	size_t clean;

	/* Its length, up to a line end. */
	length = strlen(secret);
	clean = strcspn(secret, "\n");
	if (length == 0U || length > KL_SYSTEM_PASSWORD_MAX || clean != length)
		return 0;

	/* One line. */
	return 1;
}
