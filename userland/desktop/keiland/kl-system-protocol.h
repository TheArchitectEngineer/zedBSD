/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The wire of Keiland's system extension (WS135, plan/ws135/design.md
 * section 4.2; WS131 section 4): the opcodes of kl_system_manager_v1 and
 * kl_system_settings_v1, and the values their events carry.  The
 * compositor serves them and libkeiland speaks them; both include this
 * header and neither the other's code (WS131 D4 (c)).
 *
 * kl_system_manager_v1 (a global, version 1)
 *   request 0 destroy
 *   request 1 get_settings(new_id kl_system_settings_v1)
 *   request 2 get_network(new_id kl_system_network_v1)    (WS131 p010)
 *   request 3 get_audio(new_id kl_system_audio_v1)
 *   request 4 get_power(new_id kl_system_power_v1)
 *   request 5 get_devices(new_id kl_system_devices_v1)
 *   event   0 capabilities(uint bits)              sent when it is bound
 *
 * kl_system_settings_v1
 *   request 0 destroy
 *   request 1 set(uint request, string key, string value)
 *   request 2 reset(uint request, string key)       back to the default
 *   event   0 value(string key, string value, uint flags)
 *   event   1 done(uint serial)                     the values before it are one state
 *   event   2 result(uint request, uint applied, uint saved)
 *
 * When made, a settings object hears every compositor setting's value and
 * a done; afterwards each change made by anyone (a client, the system bar,
 * audiod) comes to every settings object as its value and a done.  Every
 * set and reset is answered by one result.
 *
 * kl_system_network_v1 (WS131 p010)
 *   request 0 destroy
 *   request 1 request(uint request, uint what, string ssid)   scan, join (ssid), disconnect, Wi-Fi on, off
 *   request 2 save_key(uint request, string ssid, string key) saves the key, tells the daemon, joins
 *   request 3 query_details(uint request)                      the links, the DNS servers, the saved networks
 *   event   0 state(uint reachable, uint connected, uint kind, string interface, string wired, uint wifi,
 *                   string wifi_interface, string ssid)
 *   event   1 access_point(string ssid, int rssi, uint secured)
 *   event   2 scan_done()                           the access points before it are the whole scan
 *   event   3 link(string name, uint flags, string address, string netmask, string hardware, uint mtu,
 *                  uint received_high, uint received_low, uint sent_high, uint sent_low)
 *   event   4 dns(string address)
 *   event   5 saved_network(string ssid)
 *   event   6 details_done()                        the links, servers and networks before it are the whole details
 *   event   7 done(uint serial)
 *   event   8 result(uint request, uint applied, uint saved)
 *   One request of the network is outstanding at a time, the system bar's
 *   included; another is answered busy.
 *
 * kl_system_audio_v1
 *   request 0 destroy
 *   request 1 set_volume(uint request, uint left, uint right, uint muted)
 *   request 2 feedback(uint request)
 *   event   0 state(uint reachable, uint device, uint rate, uint channels, uint left, uint right, uint muted)
 *   event   1 done(uint serial)
 *   event   2 result(uint request, uint applied, uint saved)
 *
 * kl_system_power_v1
 *   request 0 destroy
 *   request 1 action(uint request, uint action)              1 power off, 2 restart, 3 suspend
 *   event   0 state(uint source, int percent, uint charging, uint actions)
 *   event   1 done(uint serial)
 *   event   2 result(uint request, uint applied, uint saved)
 *
 * kl_system_devices_v1 (the frame for WS132; no device yet)
 *   request 0 destroy
 *   request 1 eject(uint request, string id)
 *   event   0 device(string id, uint kind, uint state, string name, string location)
 *   event   1 done(uint serial)
 *   event   2 result(uint request, uint applied, uint saved)
 *
 * Every object hears its first state and a done when it is made, and each
 * change afterwards as the changed events and a done.
 */

#ifndef KEILAND_KL_SYSTEM_PROTOCOL_H
#define KEILAND_KL_SYSTEM_PROTOCOL_H

/* The interfaces' names and versions. */
#define KL_SYSTEM_MANAGER_NAME			"kl_system_manager_v1"
#define KL_SYSTEM_MANAGER_VERSION		1U
#define KL_SYSTEM_SETTINGS_NAME			"kl_system_settings_v1"

/* kl_system_manager_v1's requests and event. */
#define KL_SYSTEM_MANAGER_DESTROY		0U
#define KL_SYSTEM_MANAGER_GET_SETTINGS		1U
#define KL_SYSTEM_MANAGER_GET_NETWORK		2U
#define KL_SYSTEM_MANAGER_GET_AUDIO		3U
#define KL_SYSTEM_MANAGER_GET_POWER		4U
#define KL_SYSTEM_MANAGER_GET_DEVICES		5U
#define KL_SYSTEM_MANAGER_EVENT_CAPABILITIES	0U

/* The capabilities' bits. */
#define KL_SYSTEM_CAPABILITY_SETTINGS		0x1U
#define KL_SYSTEM_CAPABILITY_NETWORK		0x2U
#define KL_SYSTEM_CAPABILITY_AUDIO		0x4U
#define KL_SYSTEM_CAPABILITY_POWER		0x8U
#define KL_SYSTEM_CAPABILITY_DEVICES		0x10U

/* The interfaces' names (WS131 p010). */
#define KL_SYSTEM_NETWORK_NAME			"kl_system_network_v1"
#define KL_SYSTEM_AUDIO_NAME			"kl_system_audio_v1"
#define KL_SYSTEM_POWER_NAME			"kl_system_power_v1"
#define KL_SYSTEM_DEVICES_NAME			"kl_system_devices_v1"

/* kl_system_network_v1's requests and events. */
#define KL_SYSTEM_NETWORK_DESTROY		0U
#define KL_SYSTEM_NETWORK_REQUEST		1U
#define KL_SYSTEM_NETWORK_SAVE_KEY		2U
#define KL_SYSTEM_NETWORK_QUERY_DETAILS		3U
#define KL_SYSTEM_NETWORK_EVENT_STATE		0U
#define KL_SYSTEM_NETWORK_EVENT_ACCESS_POINT	1U
#define KL_SYSTEM_NETWORK_EVENT_SCAN_DONE	2U
#define KL_SYSTEM_NETWORK_EVENT_LINK		3U
#define KL_SYSTEM_NETWORK_EVENT_DNS		4U
#define KL_SYSTEM_NETWORK_EVENT_SAVED		5U
#define KL_SYSTEM_NETWORK_EVENT_DETAILS_DONE	6U
#define KL_SYSTEM_NETWORK_EVENT_DONE		7U
#define KL_SYSTEM_NETWORK_EVENT_RESULT		8U

/* The network's requests (request's what; the backend's KL_BACKEND_NETWORK_REQUEST_* values). */
#define KL_SYSTEM_NETWORK_SCAN			1U
#define KL_SYSTEM_NETWORK_JOIN			2U
#define KL_SYSTEM_NETWORK_DISCONNECT		3U
#define KL_SYSTEM_NETWORK_WIFI_ON		4U
#define KL_SYSTEM_NETWORK_WIFI_OFF		5U

/* A link's flags. */
#define KL_SYSTEM_LINK_UP			0x1U
#define KL_SYSTEM_LINK_RUNNING			0x2U
#define KL_SYSTEM_LINK_LOOPBACK			0x4U

/* kl_system_audio_v1's requests and events. */
#define KL_SYSTEM_AUDIO_DESTROY			0U
#define KL_SYSTEM_AUDIO_SET_VOLUME		1U
#define KL_SYSTEM_AUDIO_FEEDBACK		2U
#define KL_SYSTEM_AUDIO_EVENT_STATE		0U
#define KL_SYSTEM_AUDIO_EVENT_DONE		1U
#define KL_SYSTEM_AUDIO_EVENT_RESULT		2U

/* kl_system_power_v1's requests and events, and its actions (the backend's KL_BACKEND_POWER_* values). */
#define KL_SYSTEM_POWER_DESTROY			0U
#define KL_SYSTEM_POWER_ACTION			1U
#define KL_SYSTEM_POWER_EVENT_STATE		0U
#define KL_SYSTEM_POWER_EVENT_DONE		1U
#define KL_SYSTEM_POWER_EVENT_RESULT		2U
#define KL_SYSTEM_POWER_POWEROFF		1U
#define KL_SYSTEM_POWER_REBOOT			2U
#define KL_SYSTEM_POWER_SUSPEND			3U

/* kl_system_devices_v1's requests and events. */
#define KL_SYSTEM_DEVICES_DESTROY		0U
#define KL_SYSTEM_DEVICES_EJECT			1U
#define KL_SYSTEM_DEVICES_EVENT_DEVICE		0U
#define KL_SYSTEM_DEVICES_EVENT_DONE		1U
#define KL_SYSTEM_DEVICES_EVENT_RESULT		2U

/* kl_system_settings_v1's requests and events. */
#define KL_SYSTEM_SETTINGS_DESTROY		0U
#define KL_SYSTEM_SETTINGS_SET			1U
#define KL_SYSTEM_SETTINGS_RESET		2U
#define KL_SYSTEM_SETTINGS_EVENT_VALUE		0U
#define KL_SYSTEM_SETTINGS_EVENT_DONE		1U
#define KL_SYSTEM_SETTINGS_EVENT_RESULT		2U

/* A value's flags: the resolver's default (not chosen), and not known yet (the value is empty). */
#define KL_SYSTEM_SETTINGS_DEFAULT		0x1U
#define KL_SYSTEM_SETTINGS_UNKNOWN		0x2U

/* The results of a request (WS131 section 4.1). */
#define KL_SYSTEM_RESULT_OK			0U
#define KL_SYSTEM_RESULT_DENIED			1U
#define KL_SYSTEM_RESULT_UNSUPPORTED		2U
#define KL_SYSTEM_RESULT_BUSY			3U
#define KL_SYSTEM_RESULT_INVALID		4U
#define KL_SYSTEM_RESULT_UNAVAILABLE		5U
#define KL_SYSTEM_RESULT_FAILED			6U
#define KL_SYSTEM_RESULT_NOT_SAVED		7U

#endif
