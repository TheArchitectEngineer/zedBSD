/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BCM43455: the device, as the driver's own files see it.
 *
 * Private to src/drivers/wifi/bcm43455/ and its host test.  The radio
 * operations (scan, connect, the FullMAC data path) build on what is
 * here: the bus, the rings the bus thread fills and drains, and the
 * runtime's start and stop.
 */

#ifndef KERN_DRIVERS_BCM43455_DEVICE_H
#define KERN_DRIVERS_BCM43455_DEVICE_H

#include <stddef.h>
#include <stdint.h>

#include <kern/lock.h>
#include <kern/net/wlan.h>

#include "drivers/wifi/bcm43455/bcm43455.h"
#include "drivers/wifi/bcm43455/bcm43455-bus.h"
#include "drivers/wifi/bcm43455/bcm43455-firmware.h"

/* How many received and how many outgoing Ethernet frames wait at most. */
#define BCM43455_RX_RING_SIZE		128U
#define BCM43455_TX_RING_SIZE		64U

/* The most valid channels the firmware's list may hold. */
#define BCM43455_CHANNELS_MAX		64U

/* How many events are logged after each start, so a board's first bring-up shows them. */
#define BCM43455_EVENT_LOG_LIMIT	16U

/* The network device's MTU. */
#define BCM43455_MTU			1500U

/* One Ethernet frame waiting in a ring. */
struct bcm43455_frame_entry {
	size_t length;
	uint8_t bytes[BCM43455_BUS_ETHERNET_MAX];
};

/*
 * A bounded queue of frames between the bus thread and the rest of the
 * kernel.
 *
 * entries holds capacity frames from head on, count of them in use; a
 * frame that finds the ring full is dropped and counted.  Guarded by the
 * device's ring_lock.
 */
struct bcm43455_frame_ring {
	struct bcm43455_frame_entry *entries;
	unsigned capacity;
	unsigned head;
	unsigned count;
	uint64_t dropped;
};

/*
 * The firmware's identity, as the running firmware reports it.
 *
 * mac is the chip's address from its OTP, version the firmware's
 * version text, capabilities its feature list, supplicant whether both
 * name the in-firmware supplicant (idsup) this driver relies on, and
 * profile the channels the firmware allows, as the WLAN station scans
 * them.
 */
struct bcm43455_identity {
	uint8_t mac[6];
	char version[BCM43455_VERSION_TEXT_MAX];
	char capabilities[BCM43455_BCDC_CONTROL_MAX];
	unsigned supplicant;
	struct wlan_scan_profile profile;
};

/*
 * One BCM43455.
 *
 * Made by bcm43455_device_create() and, once published, alive for as long
 * as the kernel runs.  lifecycle_lock serializes probe, publish, open,
 * close and the station's quiesce and retry; the bus thread never takes
 * it.  ring_lock (a leaf spinlock) guards both rings, the event count and
 * bus_error, which the bus thread's hooks change.  powered and
 * firmware_running record what a stop must undo; runtime_active is set
 * while the bus thread serves an opened device.
 */
struct bcm43455_device {
	const struct bcm43455_bus_ops *ops;
	void *context;
	struct bcm43455_files files;
	char file_version[BCM43455_VERSION_TEXT_MAX];
	uint8_t nvram[BCM43455_NVRAM_BLOB_MAX];
	size_t nvram_length;
	uint8_t placeholder_mac[6];
	unsigned has_placeholder;
	struct bcm43455_identity identity;

	struct bcm43455_bus bus;

	struct mutex lifecycle_lock;
	unsigned powered;
	unsigned firmware_running;
	unsigned runtime_active;
	unsigned published;
	struct net_device *net;
	struct wlan_station *station;

	struct spinlock ring_lock;
	struct bcm43455_frame_ring rx_ring;
	struct bcm43455_frame_ring tx_ring;
	unsigned events_logged;
	uint64_t events;
	int bus_error;
};

/*
 * Starts the chip: power, firmware, frame function, the settings every
 * start makes, and, when up is set, the radio and the bus thread.  The
 * caller holds lifecycle_lock; on failure everything is undone.
 */
int bcm43455_device_start(struct bcm43455_device *device, int up);

/*
 * Stops the bus thread, takes the radio down and cuts the chip's power;
 * drops whatever the rings hold.  The caller holds lifecycle_lock.
 */
void bcm43455_device_stop(struct bcm43455_device *device);

/*
 * Queues an Ethernet frame for the firmware to send and wakes the bus
 * thread.  Returns ENETDOWN while the device does not run, EMSGSIZE for a
 * frame too long, and ENOBUFS when the ring is full.  Any thread context.
 */
int bcm43455_device_ethernet_transmit(struct bcm43455_device *device, const uint8_t *frame, size_t length);

/*
 * Takes the oldest received Ethernet frame into frame; returns 1 when one
 * was taken, 0 when the ring is empty.  Poll context.
 */
int bcm43455_device_ethernet_receive(struct bcm43455_device *device, uint8_t *frame, size_t capacity, size_t *length);

/*
 * Builds the scan profile from the firmware's list of valid channels:
 * every channel the WLAN station knows, scanned by the firmware, never
 * probed by the host.
 */
int bcm43455_device_scan_profile(const uint32_t *channels, unsigned count, struct wlan_scan_profile *profile);

/*
 * Reports whether a space-separated capability list names a capability.
 */
int bcm43455_device_has_capability(const char *list, const char *name);

#endif
