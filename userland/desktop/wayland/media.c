/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The removable media (ws132-p004).
 *
 * The compositor follows the volumes through libkeiland-backend on its event
 * loop's thread (the system extension's tick): the list goes to the devices
 * objects (system.c), and the bar shows a small USB stick while a volume
 * that was never mounted since it was inserted is there.  It blinks three
 * times when such a volume comes, and a click starts Files on its devices
 * (files --devices), where the volume blinks too.  This stands in for the
 * notification of WS156 until that exists.
 */

#include "media.h"
#include "glass.h"

#include "userland/desktop/paths.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The bar's room for the icon, the blinks and their period. */
#define MEDIA_ICON_ROOM		34

/* The icon's size in pixels, as the volume's. */
#define MEDIA_ICON_PIXELS	20U
#define MEDIA_BLINKS		3U
#define MEDIA_BLINK_MS		600U

/* What Files is started with by a click. */
#define MEDIA_FILES		KEILAND_BINDIR "/files --devices"

/*
 * The one view of the media: the backend's following (opened on the first
 * tick), the volumes of its last report, whether the icon shows, when its
 * blinks began (0: not blinking), and where a click lands.  Only the event
 * loop's thread touches it.
 */
struct media_view {
	unsigned opened;
	struct kl_backend_volumes *volumes;
	struct kl_backend_volume list[KL_BACKEND_VOLUMES_MAX];
	size_t count;
	unsigned shown;
	uint64_t blink_ms;
	int32_t icon_x;
	int32_t icon_y;
	int32_t icon_width;
	int32_t icon_height;
	unsigned icon_logged;
};

static int media_fresh(const char *id);
static int media_in_icon(int32_t x, int32_t y);

/* The view. */
static struct media_view media_view;

/*
 * Reads what the backend reported; a new volume not mounted yet shows the
 * icon and starts its blinks.  Returns the changed bits.
 */
unsigned
zwl_media_tick(
	struct zwl_server *server)
{
	struct kl_backend_volume list[KL_BACKEND_VOLUMES_MAX];
	uint64_t now;
	unsigned changed;
	unsigned shown;
	size_t count;
	size_t index;
	int known;

	/* The following, once (the backend connects to volumed when it can). */
	if (!media_view.opened) {
		media_view.opened = 1U;
		media_view.volumes = kl_backend_volumes_open();
	}

	/* No following (no memory): no media. */
	if (media_view.volumes == NULL)
		return 0U;

	/* What arrived. */
	changed = 0U;
	(void)kl_backend_volumes_update(media_view.volumes, &changed);

	/* The blinks ask for frames until they are over. */
	if (media_view.blink_ms != 0U) {
		server->dirty = 1;
		now = zwl_milliseconds();
		if (now - media_view.blink_ms >= MEDIA_BLINKS * MEDIA_BLINK_MS)
			media_view.blink_ms = 0U;
	}

	/* The list did not change. */
	if ((changed & KL_BACKEND_VOLUMES_CHANGED_LIST) == 0U)
		return changed;

	/* The new list; a fresh volume that was not fresh before starts the blinks. */
	count = kl_backend_volumes_get(media_view.volumes, list, KL_BACKEND_VOLUMES_MAX);
	shown = 0U;
	for (index = 0U; index < count; index++) {
		if (list[index].fresh == 0U || list[index].path[0] != '\0')
			continue;
		shown = 1U;
		known = media_fresh(list[index].id);
		if (!known) {
			media_view.blink_ms = zwl_milliseconds();
			printf("ZWL MEDIA new id=%s label=%s\n", list[index].id, list[index].label);
		}
	}

	/* Kept, and drawn. */
	memcpy(media_view.list, list, count * sizeof(list[0]));
	media_view.count = count;
	media_view.shown = shown;
	server->dirty = 1;
	printf("ZWL MEDIA volumes=%zu icon=%u\n", count, shown);

	/* Succeeded: what changed. */
	return changed;
}

/*
 * Copies the volumes of the last report.
 */
size_t
zwl_media_volumes(
	struct kl_backend_volume *list,
	size_t capacity)
{
	size_t count;

	/* As many as fit. */
	count = media_view.count;
	if (count > capacity)
		count = capacity;
	memcpy(list, media_view.list, count * sizeof(list[0]));

	/* Succeeded. */
	return count;
}

/*
 * Asks for a volume to be mounted (mount 1) or ejected.  Returns 0 with the
 * backend's request number, or an errno value.
 */
int
zwl_media_ask(
	int mount,
	const char *id,
	uint32_t *request)
{
	int error;

	/* No following: nothing can be asked. */
	if (media_view.volumes == NULL)
		return ENOTCONN;

	/* The request. */
	if (mount) {
		error = kl_backend_volumes_mount(media_view.volumes, id, request);
	} else {
		error = kl_backend_volumes_eject(media_view.volumes, id, request);
	}

	/* Reports a request that could not be sent. */
	if (error != 0)
		return error;

	/* Succeeded: the answer comes as a result. */
	return 0;
}

/*
 * Takes the oldest answer of the backend; 1 with one, 0 without.
 */
int
zwl_media_take_result(
	uint32_t *request,
	int *error,
	char *user,
	size_t size)
{
	int taken;

	/* No following: no answer. */
	if (media_view.volumes == NULL)
		return 0;

	/* The answer. */
	taken = kl_backend_volumes_take_result(media_view.volumes, request, error, user, size);
	return taken;
}

/*
 * The bar's room for the icon: none while no fresh volume is there.
 */
int32_t
zwl_media_width(
	void)
{
	/* The icon only while it shows. */
	if (!media_view.shown)
		return 0;

	/* Succeeded: its room. */
	return MEDIA_ICON_ROOM;
}

/*
 * Draws the icon at x: the USB trident (the 2026-10-05 user decision; it
 * was a stick whose plug's holes were drawn in the body's own colour and
 * did not show), fading out and back in for each of its three blinks.
 */
void
zwl_media_draw_icon(
	struct zwl_server *server,
	VkCommandBuffer command,
	int32_t x,
	const float *ink)
{
	float color[4];
	uint64_t elapsed;
	uint64_t phase;

	/* Where a click lands, even while hidden (an empty place takes no click). */
	media_view.icon_x = x - 5;
	media_view.icon_y = 3;
	media_view.icon_width = MEDIA_ICON_ROOM - 4;
	media_view.icon_height = ZWL_GLASS_BAR - 6;
	if (!media_view.shown) {
		media_view.icon_logged = 0U;
		return;
	}

	/* Where it is, once each time it shows (the tests click it). */
	if (!media_view.icon_logged) {
		media_view.icon_logged = 1U;
		printf("ZWL MEDIA icon x=%d y=%d width=%d height=%d\n", media_view.icon_x, media_view.icon_y, media_view.icon_width, media_view.icon_height);
	}

	/* The ink, fading in each blink's first half and coming back in its second. */
	memcpy(color, ink, sizeof(color));
	if (media_view.blink_ms != 0U) {
		elapsed = zwl_milliseconds() - media_view.blink_ms;
		phase = elapsed % MEDIA_BLINK_MS;
		if (phase < MEDIA_BLINK_MS / 2U) {
			color[3] *= 1.0f - 0.85f * (float)phase / (float)(MEDIA_BLINK_MS / 2U);
		} else {
			color[3] *= 0.15f + 0.85f * (float)(phase - MEDIA_BLINK_MS / 2U) / (float)(MEDIA_BLINK_MS / 2U);
		}
	}

	/* The trident, at the size of the bar's other icons (the volume's). */
	glass_draw_icon(server, command, GLASS_ICON_USB, x, ZWL_GLASS_BAR_MIDDLE - 10, MEDIA_ICON_PIXELS, color);
}

/*
 * Takes a left press on the icon: Files starts on its devices.  Returns 1
 * when the press was the icon's, 0 otherwise.
 */
int
zwl_media_button(
	struct zwl_server *server,
	uint32_t button,
	uint32_t state)
{
	pid_t child;
	int inside;

	/* Only a left press, on the icon while it shows. */
	if (!media_view.shown || state == 0U || button != ZWL_BUTTON_LEFT)
		return 0;
	inside = media_in_icon(server->pointer_x, server->pointer_y);
	if (!inside)
		return 0;

	/* Files on its devices; the blinks end. */
	media_view.blink_ms = 0U;
	child = zwl_spawn(server, MEDIA_FILES);
	printf("ZWL MEDIA files pid=%d\n", (int)child);

	/* Succeeded: the press was the icon's. */
	return 1;
}

/* Tells whether a volume was already fresh in the last report. */
static int
media_fresh(
	const char *id)
{
	size_t index;
	int same;

	/* Each volume of the last report. */
	for (index = 0U; index < media_view.count; index++) {
		same = strcmp(media_view.list[index].id, id);
		if (same == 0 && media_view.list[index].fresh != 0U)
			return 1;
	}

	/* Not before. */
	return 0;
}

/* Tells whether a point is on the icon. */
static int
media_in_icon(
	int32_t x,
	int32_t y)
{
	/* The icon's rectangle. */
	if (x < media_view.icon_x || x >= media_view.icon_x + media_view.icon_width)
		return 0;
	if (y < media_view.icon_y || y >= media_view.icon_y + media_view.icon_height)
		return 0;

	/* Succeeded: inside. */
	return 1;
}
