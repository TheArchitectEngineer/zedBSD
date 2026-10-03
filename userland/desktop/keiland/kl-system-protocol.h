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
#define KL_SYSTEM_MANAGER_EVENT_CAPABILITIES	0U

/* The capabilities' bits. */
#define KL_SYSTEM_CAPABILITY_SETTINGS		0x1U

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
