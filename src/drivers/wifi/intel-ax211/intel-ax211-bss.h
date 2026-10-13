/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Intel AX211 private BSS association-metadata contract.
 *
 * Sources of the values.  Every constant in this file is a value of the
 * 802.11 frame itself: the frame control field, the fixed fields of a beacon
 * and a probe response, and the element identifiers and lengths come from
 * IEEE Std 802.11-2020 (9.2.4.1 frame control, 9.3.3.2 and 9.3.3.10 beacon
 * and probe response bodies, 9.4.2 elements), and the WMM information and
 * parameter elements from the Wi-Fi Alliance WMM specification (vendor
 * element of OUI 00-50-F2, type 2).  No value in this file is taken from a
 * driver header.
 */

#ifndef KERN_DRIVERS_INTEL_AX211_BSS_H
#define KERN_DRIVERS_INTEL_AX211_BSS_H

#include <stddef.h>
#include <stdint.h>

#include "intel-ax211-rx.h"

/*
 * Keep every BSS which the common WLAN snapshot may offer back to connect().
 * A smaller private cache can evict the selected BSSID while it is still
 * present in the common 64-entry scan snapshot.
 */
#define INTEL_AX211_BSS_CACHE_LIMIT 64U
#define INTEL_AX211_BSS_ADDRESS_SIZE 6U

enum intel_ax211_bss_result {
	INTEL_AX211_BSS_OK = 0,
	INTEL_AX211_BSS_INVALID = 1,
	INTEL_AX211_BSS_UNSUPPORTED = 2,
	INTEL_AX211_BSS_TRUNCATED = 3,
	INTEL_AX211_BSS_MALFORMED = 4,
	INTEL_AX211_BSS_STALE = 5,
	INTEL_AX211_BSS_NOT_FOUND = 6
};

enum intel_ax211_bss_source {
	INTEL_AX211_BSS_SOURCE_PROBE_RESPONSE = 1,
	INTEL_AX211_BSS_SOURCE_BEACON = 2
};

/*
 * This private record intentionally retains neither an SSID nor credentials.
 * observation_generation records the scan operation which observed the BSS;
 * hardware_epoch independently identifies the firmware boot which produced
 * the RX descriptor.  A later connection has its own common generation.
 */
struct intel_ax211_bss_entry {
	uint8_t bssid[INTEL_AX211_BSS_ADDRESS_SIZE];
	uint64_t observation_generation;
	uint64_t last_seen_ticks;
	uint64_t frame_timestamp;
	uint64_t receive_tsf;
	uint32_t hardware_epoch;
	uint32_t air_start_clock;
	int32_t rssi_dbm;
	uint16_t beacon_interval_tu;
	uint16_t capability;
	uint8_t channel;
	uint8_t dtim_count;
	uint8_t dtim_period;
	uint8_t tim_valid;
	uint8_t wmm_present;
	uint8_t receive_tsf_valid;
	uint8_t source;
	uint8_t valid;
};

struct intel_ax211_bss_cache {
	struct intel_ax211_bss_entry entry[INTEL_AX211_BSS_CACHE_LIMIT];
	uint32_t hardware_epoch;
	size_t count;
	uint8_t initialized;
};

/*
 * Association-side values only.  Queue DMA addresses and other hardware-owned
 * state are deliberately absent.  tim_valid tells the caller whether the
 * DTIM fields can be used for the post-authentication association update.
 */
struct drv_intel_ax211_bss_assoc_metadata {
	uint8_t bssid[INTEL_AX211_BSS_ADDRESS_SIZE];
	uint64_t common_generation;
	uint64_t observation_generation;
	uint64_t beacon_timestamp;
	uint64_t receive_tsf;
	uint32_t hardware_epoch;
	uint32_t beacon_arrive_time;
	uint16_t beacon_interval_tu;
	uint16_t capability;
	uint8_t channel;
	uint8_t dtim_count;
	uint8_t dtim_period;
	uint8_t tim_valid;
	uint8_t wmm_present;
	uint8_t receive_tsf_valid;
};

int drv_intel_ax211_bss_decode(const struct intel_ax211_rx_delivery *mpdu,
			       uint64_t observation_generation,
			       uint32_t hardware_epoch,
			       struct intel_ax211_bss_entry *entry);
int drv_intel_ax211_bss_cache_init(struct intel_ax211_bss_cache *cache,
				   uint32_t hardware_epoch);
int
drv_intel_ax211_bss_cache_observe(struct intel_ax211_bss_cache *cache,
				  const struct intel_ax211_bss_entry *entry);
int drv_intel_ax211_bss_cache_lookup(
	const struct intel_ax211_bss_cache *cache,
	const uint8_t bssid[INTEL_AX211_BSS_ADDRESS_SIZE], uint8_t channel,
	uint32_t hardware_epoch, struct intel_ax211_bss_entry *entry);
int drv_intel_ax211_bss_assoc_metadata(
	const struct intel_ax211_bss_entry *entry,
	uint64_t connection_generation, uint32_t hardware_epoch,
	struct drv_intel_ax211_bss_assoc_metadata *metadata);

#endif
