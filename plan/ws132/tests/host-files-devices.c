/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws132-p005: the removable devices in Files on the host (files.h's
 * fm_devices_*, the sidebar's Devices section and the Today page's
 * Devices cards), with files' host objects (plan/tools/files/host-build.sh):
 * a new device blinks (three times, then not), a device not mounted is
 * mounted by a double click on its row or card and not by a single one,
 * the mount's answer opens it, a mounted one has an eject button that asks
 * for the eject, a device's mount is not listed again under Locations, and
 * the list emptied takes the section away.
 *
 *   host-files-devices FONT
 */

#include "files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* How many checks failed. */
static int failures;

static void check(int condition, const char *text);
static int find_hit(const struct fm_app *app, unsigned kind, int index, struct kl_rect *rect);
static int find_place(const struct fm_app *app, int device);
static void press(struct fm_app *app, const struct kl_rect *rect, uint64_t *now);
static int sidebar_has(const struct fm_app *app, const char *label, unsigned section);

int
main(
	int argc,
	char **argv)
{
	static struct fm_app app;
	static uint32_t pixels[1280 * 800];
	struct kl_text text;
	struct kl_canvas canvas;
	struct fm_device list[2];
	struct kl_rect rect;
	struct fm_tab *tab;
	uint64_t now;
	float bright;
	int place;
	int found;

	/* The font, the canvas and the app on Today. */
	if (argc != 2 || kl_text_open(&text, argv[1], NULL) != 0) {
		fprintf(stderr, "usage: host-files-devices FONT\n");
		return 2;
	}
	if (kl_canvas_init(&canvas, pixels, 1280U, 1280, 800) != 0)
		return 1;
	now = 1000U;
	app.now = now;
	if (fm_app_init(&app, &text, NULL) != 0)
		return 1;
	app.width = 1280;
	app.height = 800;
	app.now = now;

	/* A new stick, not mounted: it blinks, and is under Devices. */
	memset(list, 0, sizeof(list));
	snprintf(list[0].id, sizeof(list[0].id), "sda");
	snprintf(list[0].name, sizeof(list[0].name), "USBSTICK");
	snprintf(list[0].fs, sizeof(list[0].fs), "fat");
	list[0].bytes = 16777216U;
	list[0].fresh = 1;
	fm_devices_set(&app, list, 1, 0);
	check(app.places.device_count == 1 && app.places.devices[0].blink_at == now, "a new device starts blinking");
	check(sidebar_has(&app, "USBSTICK", FM_SECTION_DEVICES), "the device is under Devices");
	app.now = now + FM_DEVICE_BLINK_PERIOD / 4U;
	bright = fm_devices_blink(&app, &app.places.devices[0]);
	check(bright < 1.0f && fm_devices_blinking(&app) == 1, "a quarter into a blink it is dimmer");
	app.now = now + FM_DEVICE_BLINK_MS + 1U;
	bright = fm_devices_blink(&app, &app.places.devices[0]);
	check(bright == 1.0f && fm_devices_blinking(&app) == 0, "after three blinks it no longer blinks");

	/* The same list again does not blink again; blink_all (files --devices) does. */
	app.now = now + 5000U;
	fm_devices_set(&app, list, 1, 0);
	check(app.places.devices[0].blink_at == now, "the same new device does not blink again");
	fm_devices_set(&app, list, 1, 1);
	check(app.places.devices[0].blink_at == app.now, "files --devices blinks the new devices again");

	/* A single click on its row does not mount it; a double click asks for the mount. */
	now = app.now + 10000U;
	app.now = now;
	fm_ui_draw(&app, &canvas);
	place = find_place(&app, 1);
	found = find_hit(&app, FM_HIT_PLACE, place, &rect);
	check(found, "the device's row takes clicks");
	press(&app, &rect, &now);
	check(app.request == FM_REQUEST_NONE, "a single click does not mount");
	now += 2000U;
	press(&app, &rect, &now);
	press(&app, &rect, &now);
	check(app.request == FM_REQUEST_NONE && app.dialog == FM_DIALOG_MOUNT && strcmp(app.device_confirm, "sda") == 0,
	    "a double click asks whether to mount sda (ws132-p009)");

	/* Cancel leaves it; Mount on the question asks for the mount. */
	fm_ui_draw(&app, &canvas);
	found = find_hit(&app, FM_HIT_BUTTON, FM_BUTTON_CANCEL, &rect);
	check(found, "the question has Cancel");
	now += 2000U;
	press(&app, &rect, &now);
	check(app.dialog == FM_DIALOG_NONE && app.request == FM_REQUEST_NONE, "Cancel mounts nothing");
	fm_ui_draw(&app, &canvas);
	place = find_place(&app, 1);
	(void)find_hit(&app, FM_HIT_PLACE, place, &rect);
	now += 2000U;
	press(&app, &rect, &now);
	press(&app, &rect, &now);
	fm_ui_draw(&app, &canvas);
	found = find_hit(&app, FM_HIT_BUTTON, FM_BUTTON_CONFIRM, &rect);
	check(found && app.dialog == FM_DIALOG_MOUNT, "asked again, the question has Mount");
	now += 2000U;
	press(&app, &rect, &now);
	check(app.request == FM_REQUEST_DEVICE_MOUNT && strcmp(app.device_asked, "sda") == 0 && app.dialog == FM_DIALOG_NONE,
	    "Mount asks for the mount of sda");
	app.request = FM_REQUEST_NONE;

	/* Mounted: the answer's list opens it, and it is not under Locations again. */
	snprintf(list[0].path, sizeof(list[0].path), "/media/USBSTICK");
	list[0].mounted = 1;
	list[0].fresh = 0;
	fm_devices_set(&app, list, 1, 0);
	tab = fm_ui_tab(&app);
	check(strcmp(tab->history[tab->history_index].location.path, "/media/USBSTICK") == 0 && app.device_open[0] == '\0',
	    "the mounted device opens");
	check(!sidebar_has(&app, "USBSTICK", FM_SECTION_LOCATIONS), "its mount is not under Locations");

	/* The eject button of a mounted device asks for the eject. */
	fm_ui_draw(&app, &canvas);
	place = find_place(&app, 1);
	found = find_hit(&app, FM_HIT_BUTTON, FM_BUTTON_EJECT_PLACE + place, &rect);
	check(found, "a mounted device has an eject button");
	now += 2000U;
	press(&app, &rect, &now);
	check(app.request == FM_REQUEST_DEVICE_EJECT && strcmp(app.device_asked, "sda") == 0, "the eject button asks for the eject of sda");
	app.request = FM_REQUEST_NONE;

	/* Today: a card per device; a double click on a card not mounted asks for the mount. */
	list[0].mounted = 0;
	list[0].path[0] = '\0';
	fm_devices_set(&app, list, 1, 0);
	fm_ui_go(&app, &app.places.items[0].location);
	fm_ui_draw(&app, &canvas);
	found = find_hit(&app, FM_HIT_CARD, 200, &rect);
	check(found, "Today shows the device's card");
	now += 2000U;
	press(&app, &rect, &now);
	check(app.request == FM_REQUEST_NONE, "a single click on the card does not mount");
	now += 2000U;
	press(&app, &rect, &now);
	press(&app, &rect, &now);
	check(app.dialog == FM_DIALOG_MOUNT && app.request == FM_REQUEST_NONE, "a double click on the card asks whether to mount");
	fm_action_confirm(&app, 1);
	check(app.request == FM_REQUEST_DEVICE_MOUNT, "Enter (yes) asks for the mount");
	app.request = FM_REQUEST_NONE;

	/* The list emptied: no Devices section, no card. */
	fm_devices_set(&app, list, 0, 0);
	fm_ui_draw(&app, &canvas);
	check(app.places.device_count == 0 && !sidebar_has(&app, "USBSTICK", FM_SECTION_DEVICES), "an empty list takes the section away");
	found = find_hit(&app, FM_HIT_CARD, 200, &rect);
	check(!found, "and the card");

	/* The verdict. */
	fm_app_release(&app);
	kl_text_close(&text);
	if (failures != 0) {
		printf("host-files-devices: FAIL (%d)\n", failures);
		return 1;
	}
	printf("host-files-devices: PASS\n");
	return 0;
}

/* Finds a clickable region of the last frame. */
static int
find_hit(
	const struct fm_app *app,
	unsigned kind,
	int index,
	struct kl_rect *rect)
{
	int hit;

	for (hit = 0; hit < app->hit_count; hit++) {
		if (app->hits[hit].kind == kind && app->hits[hit].index == index) {
			*rect = app->hits[hit].rect;
			return 1;
		}
	}
	return 0;
}

/* Finds the sidebar place of a device (index + 1). */
static int
find_place(
	const struct fm_app *app,
	int device)
{
	int index;

	for (index = 0; index < app->places.count; index++) {
		if (app->places.items[index].device == device)
			return index;
	}
	return -1;
}

/* Presses and releases the left button in the middle of a region, then draws. */
static void
press(
	struct fm_app *app,
	const struct kl_rect *rect,
	uint64_t *now)
{
	static uint32_t frame[1280 * 800];
	struct kl_canvas canvas;
	struct fm_event event;
	int pressed;

	for (pressed = 1; pressed >= 0; pressed--) {
		memset(&event, 0, sizeof(event));
		event.type = FM_EVENT_MOTION;
		event.x = rect->x + rect->width / 2;
		event.y = rect->y + rect->height / 2;
		event.time = *now;
		fm_ui_event(app, &event);
		event.type = FM_EVENT_BUTTON;
		event.button = FM_BUTTON_LEFT;
		event.pressed = pressed;
		event.serial = 1;
		fm_ui_event(app, &event);
		*now += 40U;
		app->now = *now;
	}
	(void)kl_canvas_init(&canvas, frame, 1280U, 1280, 800);
	fm_ui_draw(app, &canvas);
}

/* Tells whether the sidebar has a place of a label in a section. */
static int
sidebar_has(
	const struct fm_app *app,
	const char *label,
	unsigned section)
{
	int index;

	for (index = 0; index < app->places.count; index++) {
		if (app->places.items[index].section == section && strcmp(app->places.items[index].label, label) == 0)
			return 1;
	}
	return 0;
}

/* Counts a failed check and names it. */
static void
check(
	int condition,
	const char *text)
{
	if (condition) {
		printf("ok: %s\n", text);
	} else {
		printf("FAIL: %s\n", text);
		failures++;
	}
}
