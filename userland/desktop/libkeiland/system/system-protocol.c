/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The interfaces of Keiland's system extension (keiland/kl-system-protocol.h;
 * WS135 for the manager and the settings, WS131 p010 for the network, the
 * sound, the power and the devices), described as wayland-scanner would
 * make them, over libwayland's marshalling.  They are not static because
 * the settings and the system both use them; exports.map keeps them
 * inside the library.
 */

#include "system-protocol.h"

#include "userland/desktop/keiland/kl-system-protocol.h"

#include <stddef.h>

/* The manager's get_* argument types: the new object each makes. */
static const struct wl_interface *system_get_settings_types[] = {
	&kl_system_settings_v1_interface,
};
static const struct wl_interface *system_get_network_types[] = {
	&kl_system_network_v1_interface,
};
static const struct wl_interface *system_get_audio_types[] = {
	&kl_system_audio_v1_interface,
};
static const struct wl_interface *system_get_power_types[] = {
	&kl_system_power_v1_interface,
};
static const struct wl_interface *system_get_devices_types[] = {
	&kl_system_devices_v1_interface,
};
static const struct wl_interface *system_get_monitor_types[] = {
	&kl_system_monitor_v1_interface,
	NULL,
};

/* The arguments of messages that name no interface (at most sixteen, a monitor's disk's). */
static const struct wl_interface *system_plain_types[] = {
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};

/* The requests of kl_system_manager_v1. */
static const struct wl_message system_manager_requests[] = {
	{ "destroy", "", NULL },
	{ "get_settings", "n", system_get_settings_types },
	{ "get_network", "n", system_get_network_types },
	{ "get_audio", "n", system_get_audio_types },
	{ "get_power", "n", system_get_power_types },
	{ "get_devices", "n", system_get_devices_types },
	{ "get_monitor", "2nu", system_get_monitor_types },
};

/* The events of kl_system_manager_v1. */
static const struct wl_message system_manager_events[] = {
	{ "capabilities", "u", system_plain_types },
};

/* kl_system_manager_v1, version 2: seven requests (get_monitor since 2) and one event.  It lives for the program. */
const struct wl_interface kl_system_manager_v1_interface = {
	KL_SYSTEM_MANAGER_NAME,
	2,
	7,
	system_manager_requests,
	1,
	system_manager_events
};

/* The requests of kl_system_settings_v1. */
static const struct wl_message system_settings_requests[] = {
	{ "destroy", "", NULL },
	{ "set", "uss", system_plain_types },
	{ "reset", "us", system_plain_types },
};

/* The events of kl_system_settings_v1. */
static const struct wl_message system_settings_events[] = {
	{ "value", "ssu", system_plain_types },
	{ "done", "u", system_plain_types },
	{ "result", "uuu", system_plain_types },
};

/* kl_system_settings_v1: three requests and three events.  It lives for the program. */
const struct wl_interface kl_system_settings_v1_interface = {
	KL_SYSTEM_SETTINGS_NAME,
	1,
	3,
	system_settings_requests,
	3,
	system_settings_events
};

/* The requests of kl_system_network_v1. */
static const struct wl_message system_network_requests[] = {
	{ "destroy", "", NULL },
	{ "request", "uus", system_plain_types },
	{ "save_key", "uss", system_plain_types },
	{ "query_details", "u", system_plain_types },
};

/* The events of kl_system_network_v1. */
static const struct wl_message system_network_events[] = {
	{ "state", "uuussuss", system_plain_types },
	{ "access_point", "siu", system_plain_types },
	{ "scan_done", "", NULL },
	{ "link", "susssuuuuu", system_plain_types },
	{ "dns", "s", system_plain_types },
	{ "saved_network", "s", system_plain_types },
	{ "details_done", "", NULL },
	{ "done", "u", system_plain_types },
	{ "result", "uuu", system_plain_types },
};

/* kl_system_network_v1: four requests and nine events.  It lives for the program. */
const struct wl_interface kl_system_network_v1_interface = {
	KL_SYSTEM_NETWORK_NAME,
	1,
	4,
	system_network_requests,
	9,
	system_network_events
};

/* The requests of kl_system_audio_v1. */
static const struct wl_message system_audio_requests[] = {
	{ "destroy", "", NULL },
	{ "set_volume", "uuuu", system_plain_types },
	{ "feedback", "u", system_plain_types },
};

/* The events of kl_system_audio_v1. */
static const struct wl_message system_audio_events[] = {
	{ "state", "uuuuuuu", system_plain_types },
	{ "done", "u", system_plain_types },
	{ "result", "uuu", system_plain_types },
};

/* kl_system_audio_v1: three requests and three events.  It lives for the program. */
const struct wl_interface kl_system_audio_v1_interface = {
	KL_SYSTEM_AUDIO_NAME,
	1,
	3,
	system_audio_requests,
	3,
	system_audio_events
};

/* The requests of kl_system_power_v1. */
static const struct wl_message system_power_requests[] = {
	{ "destroy", "", NULL },
	{ "action", "uu", system_plain_types },
};

/* The events of kl_system_power_v1. */
static const struct wl_message system_power_events[] = {
	{ "state", "uiuu", system_plain_types },
	{ "done", "u", system_plain_types },
	{ "result", "uuu", system_plain_types },
};

/* kl_system_power_v1: two requests and three events.  It lives for the program. */
const struct wl_interface kl_system_power_v1_interface = {
	KL_SYSTEM_POWER_NAME,
	1,
	2,
	system_power_requests,
	3,
	system_power_events
};

/* The requests of kl_system_devices_v1. */
static const struct wl_message system_devices_requests[] = {
	{ "destroy", "", NULL },
	{ "eject", "us", system_plain_types },
};

/* The events of kl_system_devices_v1. */
static const struct wl_message system_devices_events[] = {
	{ "device", "suuss", system_plain_types },
	{ "done", "u", system_plain_types },
	{ "result", "uuu", system_plain_types },
};

/* kl_system_devices_v1: two requests and three events.  It lives for the program. */
const struct wl_interface kl_system_devices_v1_interface = {
	KL_SYSTEM_DEVICES_NAME,
	1,
	2,
	system_devices_requests,
	3,
	system_devices_events
};

/* The requests of kl_system_monitor_v1 (WS134 p012). */
static const struct wl_message system_monitor_requests[] = {
	{ "destroy", "", NULL },
	{ "ack", "u", system_plain_types },
	{ "set_period", "u", system_plain_types },
};

/* The events of kl_system_monitor_v1. */
static const struct wl_message system_monitor_events[] = {
	{ "info", "usuu", system_plain_types },
	{ "device", "uuuuuuss", system_plain_types },
	{ "info_done", "u", system_plain_types },
	{ "cpu", "uuuuuuuuu", system_plain_types },
	{ "memory", "uuuuuuuuuuuu", system_plain_types },
	{ "link", "uuuuuuu", system_plain_types },
	{ "disk", "uuuuuuuuuuuuuuuu", system_plain_types },
	{ "gpu", "uuuuuuuuuuuuiu", system_plain_types },
	{ "sample_done", "uuuuuui", system_plain_types },
};

/* kl_system_monitor_v1: three requests and nine events.  It lives for the program. */
const struct wl_interface kl_system_monitor_v1_interface = {
	KL_SYSTEM_MONITOR_NAME,
	1,
	3,
	system_monitor_requests,
	9,
	system_monitor_events
};
