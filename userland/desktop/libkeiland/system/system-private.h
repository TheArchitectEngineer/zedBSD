/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The inside of libkeiland's system (system.c; WS131 p010, plan/ws131/
 * design.md section 4): the wire's interfaces, which the settings share
 * (system-protocol.c), and the state an application sees, kept as one
 * state at a time (system-view.c).  The view knows nothing of Wayland, so
 * that the host tests build it alone (plan/ws131/tests/host-system.sh).
 */

#ifndef KEILAND_SYSTEM_PRIVATE_H
#define KEILAND_SYSTEM_PRIVATE_H

#include <keiland.h>

#include <stddef.h>
#include <stdint.h>

/* The most answered requests kept until they are taken. */
#define SYSTEM_VIEW_RESULTS	32U

/* One answered request and its error. */
struct system_view_result {
	uint32_t request;
	int error;
};

/*
 * The system as an application sees it.
 *
 * Each part has the state in effect and the one the compositor is sending
 * (pending), put into effect at its done; touched says the pending one has
 * something.  A list (the scan, the details, the devices) is sent whole:
 * open says its pending list was started since its end, so that the next
 * item starts a new list.  The details have no done: their end puts them
 * into effect.
 *
 * changed has the KL_SYSTEM_CHANGED_* bits since the last take; results is
 * a ring of answered requests, result_head the oldest, result_count how
 * many wait (the oldest is dropped when it is full).
 */
struct system_view {
	unsigned capabilities;
	struct kl_network_state network;
	struct kl_network_state network_pending;
	unsigned network_touched;
	struct kl_network_ap scan[KL_NETWORK_SCAN_MAX];
	size_t scan_count;
	struct kl_network_ap scan_pending[KL_NETWORK_SCAN_MAX];
	size_t scan_pending_count;
	unsigned scan_open;
	unsigned scan_touched;
	struct kl_network_link links[KL_NETWORK_LINKS_MAX];
	size_t link_count;
	char dns[KL_NETWORK_DNS_MAX][KL_NETWORK_ADDRESS_MAX];
	size_t dns_count;
	char saved[KL_NETWORK_SAVED_MAX][KL_NETWORK_SSID_MAX];
	size_t saved_count;
	struct kl_network_link links_pending[KL_NETWORK_LINKS_MAX];
	size_t links_pending_count;
	char dns_pending[KL_NETWORK_DNS_MAX][KL_NETWORK_ADDRESS_MAX];
	size_t dns_pending_count;
	char saved_pending[KL_NETWORK_SAVED_MAX][KL_NETWORK_SSID_MAX];
	size_t saved_pending_count;
	unsigned details_open;
	struct kl_audio_state audio;
	struct kl_audio_state audio_pending;
	unsigned audio_touched;
	struct kl_power_state power;
	struct kl_power_state power_pending;
	unsigned power_touched;
	struct kl_device devices[KL_DEVICES_MAX];
	size_t device_count;
	struct kl_device devices_pending[KL_DEVICES_MAX];
	size_t devices_pending_count;
	unsigned devices_open;
	unsigned changed;
	struct system_view_result results[SYSTEM_VIEW_RESULTS];
	unsigned result_head;
	unsigned result_count;
};

/* system-view.c */
void system_view_init(struct system_view *view);
void system_view_network_state(struct system_view *view, const struct kl_network_state *state);
void system_view_access_point(struct system_view *view, const struct kl_network_ap *ap);
void system_view_scan_done(struct system_view *view);
void system_view_network_done(struct system_view *view);
void system_view_link(struct system_view *view, const struct kl_network_link *link);
void system_view_dns(struct system_view *view, const char *address);
void system_view_saved(struct system_view *view, const char *ssid);
void system_view_details_done(struct system_view *view);
void system_view_audio_state(struct system_view *view, const struct kl_audio_state *state);
void system_view_audio_done(struct system_view *view);
void system_view_power_state(struct system_view *view, const struct kl_power_state *state);
void system_view_power_done(struct system_view *view);
void system_view_device(struct system_view *view, const struct kl_device *device);
void system_view_devices_done(struct system_view *view);
void system_view_result(struct system_view *view, uint32_t request, uint32_t applied);
int system_view_take_result(struct system_view *view, uint32_t *request, int *error);
unsigned system_view_take_changed(struct system_view *view);
int system_view_error_of(uint32_t applied);
void system_view_copy(char *to, size_t size, const char *from);

#endif
