/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The system as an application sees it (system-private.h's struct
 * system_view; WS131 p010): what the compositor sends is kept pending and
 * put into effect at its done, so that an application never sees half of
 * a change; the answered requests wait in a ring until they are taken.
 * Nothing here knows Wayland.
 */

#include "system-private.h"

#include "userland/desktop/keiland/kl-system-protocol.h"

#include <errno.h>
#include <string.h>

/*
 * Starts a view with nothing told: the network not reached, the sound not
 * reached, the power unknown with no action, no device.
 */
void
system_view_init(
	struct system_view *view)
{
	/* Everything zero, the battery's charge unknown. */
	memset(view, 0, sizeof(*view));
	view->power.percent = -1;
	view->power_pending.percent = -1;
}

/*
 * Keeps the network's state pending until its done.
 */
void
system_view_network_state(
	struct system_view *view,
	const struct kl_network_state *state)
{
	/* The state, pending. */
	view->network_pending = *state;
	view->network_touched = 1U;
}

/*
 * Adds a network of a scan to the pending list, starting a new list after
 * the last one's end.
 */
void
system_view_access_point(
	struct system_view *view,
	const struct kl_network_ap *ap)
{
	/* The first network of a new scan starts its list. */
	if (!view->scan_open) {
		view->scan_open = 1U;
		view->scan_pending_count = 0U;
	}

	/* The network, while there is room (the compositor sends no more than the list holds). */
	if (view->scan_pending_count >= KL_NETWORK_SCAN_MAX)
		return;
	view->scan_pending[view->scan_pending_count] = *ap;
	view->scan_pending_count++;
}

/*
 * Ends a scan's pending list (an empty scan has no network before it).
 */
void
system_view_scan_done(
	struct system_view *view)
{
	/* A scan that found nothing. */
	if (!view->scan_open)
		view->scan_pending_count = 0U;

	/* The list is whole, and waits for the done. */
	view->scan_open = 0U;
	view->scan_touched = 1U;
}

/*
 * Puts the network's pending state and scan into effect.
 */
void
system_view_network_done(
	struct system_view *view)
{
	int differs;

	/* The state, when it changed. */
	if (view->network_touched) {
		view->network_touched = 0U;
		differs = memcmp(&view->network, &view->network_pending, sizeof(view->network));
		view->network = view->network_pending;
		if (differs != 0)
			view->changed |= KL_SYSTEM_CHANGED_NETWORK;
	}

	/* The scan, as a new list. */
	if (view->scan_touched) {
		view->scan_touched = 0U;
		memcpy(view->scan, view->scan_pending, view->scan_pending_count * sizeof(view->scan[0]));
		view->scan_count = view->scan_pending_count;
		view->changed |= KL_SYSTEM_CHANGED_SCAN;
	}
}

/*
 * Adds an interface to the pending details, starting new details after the
 * last ones' end.
 */
void
system_view_link(
	struct system_view *view,
	const struct kl_network_link *link)
{
	/* The first item of new details starts them. */
	if (!view->details_open) {
		view->details_open = 1U;
		view->links_pending_count = 0U;
		view->dns_pending_count = 0U;
		view->saved_pending_count = 0U;
	}

	/* The interface, while there is room. */
	if (view->links_pending_count >= KL_NETWORK_LINKS_MAX)
		return;
	view->links_pending[view->links_pending_count] = *link;
	view->links_pending_count++;
}

/*
 * Gives a pending interface its wired configuration (ws089-p022): how it
 * is configured and its router; a name not among them changes nothing.
 */
void
system_view_wired(
	struct system_view *view,
	const char *name,
	unsigned mode,
	const char *router)
{
	struct kl_network_link *link;
	size_t index;
	int differs;

	/* Only within details being received. */
	if (!view->details_open)
		return;

	/* The pending interface of that name. */
	for (index = 0; index < view->links_pending_count; index++) {
		link = &view->links_pending[index];
		differs = strcmp(link->name, name);
		if (differs != 0)
			continue;
		link->wired_mode = mode;
		system_view_copy(link->router, sizeof(link->router), router);
		return;
	}
}

/*
 * Adds a DNS server to the pending details.
 */
void
system_view_dns(
	struct system_view *view,
	const char *address)
{
	/* The first item of new details starts them. */
	if (!view->details_open) {
		view->details_open = 1U;
		view->links_pending_count = 0U;
		view->dns_pending_count = 0U;
		view->saved_pending_count = 0U;
	}

	/* The server, while there is room. */
	if (view->dns_pending_count >= KL_NETWORK_DNS_MAX)
		return;
	system_view_copy(view->dns_pending[view->dns_pending_count], KL_NETWORK_ADDRESS_MAX, address);
	view->dns_pending_count++;
}

/*
 * Adds a saved network to the pending details.
 */
void
system_view_saved(
	struct system_view *view,
	const char *ssid)
{
	/* The first item of new details starts them. */
	if (!view->details_open) {
		view->details_open = 1U;
		view->links_pending_count = 0U;
		view->dns_pending_count = 0U;
		view->saved_pending_count = 0U;
	}

	/* The network, while there is room. */
	if (view->saved_pending_count >= KL_NETWORK_SAVED_MAX)
		return;
	system_view_copy(view->saved_pending[view->saved_pending_count], KL_NETWORK_SSID_MAX, ssid);
	view->saved_pending_count++;
}

/*
 * Puts the pending details into effect (details with no item are empty).
 */
void
system_view_details_done(
	struct system_view *view)
{
	/* Details that had no item. */
	if (!view->details_open) {
		view->links_pending_count = 0U;
		view->dns_pending_count = 0U;
		view->saved_pending_count = 0U;
	}

	/* The three lists, as one state; the next item starts new details. */
	view->details_open = 0U;
	memcpy(view->links, view->links_pending, view->links_pending_count * sizeof(view->links[0]));
	view->link_count = view->links_pending_count;
	memcpy(view->dns, view->dns_pending, view->dns_pending_count * sizeof(view->dns[0]));
	view->dns_count = view->dns_pending_count;
	memcpy(view->saved, view->saved_pending, view->saved_pending_count * sizeof(view->saved[0]));
	view->saved_count = view->saved_pending_count;
	view->changed |= KL_SYSTEM_CHANGED_DETAILS;
}

/*
 * Keeps the sound's state pending until its done.
 */
void
system_view_audio_state(
	struct system_view *view,
	const struct kl_audio_state *state)
{
	/* The state, pending. */
	view->audio_pending = *state;
	view->audio_touched = 1U;
}

/*
 * Puts the sound's pending state into effect.
 */
void
system_view_audio_done(
	struct system_view *view)
{
	int differs;

	/* Nothing pending. */
	if (!view->audio_touched)
		return;

	/* The state, told when it changed. */
	view->audio_touched = 0U;
	differs = memcmp(&view->audio, &view->audio_pending, sizeof(view->audio));
	view->audio = view->audio_pending;
	if (differs != 0)
		view->changed |= KL_SYSTEM_CHANGED_AUDIO;
}

/*
 * Keeps the power's state pending until its done.
 */
void
system_view_power_state(
	struct system_view *view,
	const struct kl_power_state *state)
{
	/* The state, pending. */
	view->power_pending = *state;
	view->power_touched = 1U;
}

/*
 * Puts the power's pending state into effect.
 */
void
system_view_power_done(
	struct system_view *view)
{
	int differs;

	/* Nothing pending. */
	if (!view->power_touched)
		return;

	/* The state, told when it changed. */
	view->power_touched = 0U;
	differs = memcmp(&view->power, &view->power_pending, sizeof(view->power));
	view->power = view->power_pending;
	if (differs != 0)
		view->changed |= KL_SYSTEM_CHANGED_POWER;
}

/*
 * Adds a device to the pending list, starting a new list after the last
 * done.
 */
void
system_view_device(
	struct system_view *view,
	const struct kl_device *device)
{
	/* The first device after a done starts the list. */
	if (!view->devices_open) {
		view->devices_open = 1U;
		view->devices_pending_count = 0U;
	}

	/* The device, while there is room. */
	if (view->devices_pending_count >= KL_DEVICES_MAX)
		return;
	view->devices_pending[view->devices_pending_count] = *device;
	view->devices_pending_count++;
}

/*
 * Puts the pending devices into effect: the compositor sends the whole list
 * before each done, so a done with no device before it empties the list
 * (ws132-p004: the last volume went).
 */
void
system_view_devices_done(
	struct system_view *view)
{
	/* A done after no device: an empty list. */
	if (!view->devices_open)
		view->devices_pending_count = 0U;

	/* The list, as one state. */
	view->devices_open = 0U;
	memcpy(view->devices, view->devices_pending, view->devices_pending_count * sizeof(view->devices[0]));
	view->device_count = view->devices_pending_count;
	view->changed |= KL_SYSTEM_CHANGED_DEVICES;
}

/*
 * Keeps an answered request with its error until it is taken; when the
 * ring is full the oldest is dropped.
 */
void
system_view_result(
	struct system_view *view,
	uint32_t request,
	uint32_t applied)
{
	unsigned slot;

	/* A full ring drops its oldest. */
	if (view->result_count == SYSTEM_VIEW_RESULTS) {
		view->result_head = (view->result_head + 1U) % SYSTEM_VIEW_RESULTS;
		view->result_count--;
	}

	/* The answer after the newest. */
	slot = (view->result_head + view->result_count) % SYSTEM_VIEW_RESULTS;
	view->results[slot].request = request;
	view->results[slot].error = system_view_error_of(applied);
	view->result_count++;
	view->changed |= KL_SYSTEM_CHANGED_RESULT;
}

/*
 * Takes the oldest answered request: 1 with it, 0 when none waits.
 */
int
system_view_take_result(
	struct system_view *view,
	uint32_t *request,
	int *error)
{
	const struct system_view_result *oldest;

	/* None waits. */
	if (view->result_count == 0U)
		return 0;

	/* The oldest, out of the ring. */
	oldest = &view->results[view->result_head];
	*request = oldest->request;
	*error = oldest->error;
	view->result_head = (view->result_head + 1U) % SYSTEM_VIEW_RESULTS;
	view->result_count--;

	/* Succeeded: one answer taken. */
	return 1;
}

/*
 * Gives what changed since the last take, and starts again from nothing.
 */
unsigned
system_view_take_changed(
	struct system_view *view)
{
	unsigned changed;

	/* The bits, cleared. */
	changed = view->changed;
	view->changed = 0U;
	return changed;
}

/*
 * Gives the errno value a request's result means.
 */
int
system_view_error_of(
	uint32_t applied)
{
	/* Each result the compositor gives. */
	switch (applied) {
	case KL_SYSTEM_RESULT_OK:
		return 0;
	case KL_SYSTEM_RESULT_DENIED:
		return EPERM;
	case KL_SYSTEM_RESULT_UNSUPPORTED:
		return ENOTSUP;
	case KL_SYSTEM_RESULT_BUSY:
		return EBUSY;
	case KL_SYSTEM_RESULT_INVALID:
		return EINVAL;
	case KL_SYSTEM_RESULT_UNAVAILABLE:
		return ENODEV;
	case KL_SYSTEM_RESULT_NO_KEY:
		return ENOENT;
	case KL_SYSTEM_RESULT_REFUSED:
		return EACCES;
	case KL_SYSTEM_RESULT_UNREACHABLE:
		return ENETUNREACH;
	default:
		break;
	}

	/* Anything else failed. */
	return EIO;
}

/*
 * Copies a string into room of its own, cut to fit.
 */
void
system_view_copy(
	char *to,
	size_t size,
	const char *from)
{
	size_t length;

	/* As much as fits, with the NUL. */
	length = strlen(from);
	if (length >= size)
		length = size - 1U;
	memcpy(to, from, length);
	to[length] = '\0';
}
