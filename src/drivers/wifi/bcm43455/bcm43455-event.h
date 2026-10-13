/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BCM43455: the firmware's event messages.
 *
 * The firmware reports what happened (a scan result, a join, a lost
 * link, the supplicant's outcome) as an Ethernet frame of its own
 * ethertype on the event channel: after the Ethernet header comes a
 * Broadcom vendor header and a big-endian event message, then the
 * event's data.  The functions here only decode.
 */

#ifndef KERN_DRIVERS_BCM43455_EVENT_H
#define KERN_DRIVERS_BCM43455_EVENT_H

#include <stddef.h>
#include <stdint.h>

/* The ethertype of the firmware's event frames. */
#define BCM43455_ETHERTYPE_EVENT	0x886cU

/* The length of an event message, up to its data. */
#define BCM43455_EVENT_HEADER_LENGTH	72U

/* The bytes of the firmware's event mask: one bit per event number. */
#define BCM43455_EVENT_MASK_LENGTH	16U

/* The events this driver asks for. */
#define BCM43455_EVENT_SET_SSID		0U
#define BCM43455_EVENT_AUTH		3U
#define BCM43455_EVENT_DEAUTH		5U
#define BCM43455_EVENT_DEAUTH_IND	6U
#define BCM43455_EVENT_ASSOC		7U
#define BCM43455_EVENT_DISASSOC_IND	12U
#define BCM43455_EVENT_LINK		16U
#define BCM43455_EVENT_PSK_SUP		46U
#define BCM43455_EVENT_ESCAN_RESULT	69U

/*
 * One decoded event message.
 *
 * type is the event number, status and reason its outcome as the
 * firmware numbers them, address the peer it concerns.  data_offset and
 * data_length say where the event's data is in the Ethernet frame,
 * already bounded by the frame.
 */
struct bcm43455_event {
	uint16_t version;
	uint16_t flags;
	uint32_t type;
	uint32_t status;
	uint32_t reason;
	uint32_t auth_type;
	uint8_t address[6];
	uint8_t interface_index;
	uint8_t bsscfg_index;
	size_t data_offset;
	size_t data_length;
};

/*
 * Decodes an Ethernet frame the firmware sent as an event.
 *
 * Returns ENOENT for a frame of another ethertype, and EPROTO for an event
 * frame whose vendor header does not match or whose data runs past the
 * frame.
 */
int bcm43455_event_decode(const uint8_t *frame, size_t length, struct bcm43455_event *event);

/*
 * Sets an event's bit in the firmware's event mask.
 *
 * Returns EINVAL for an event number past the mask.
 */
int bcm43455_event_mask_add(uint8_t mask[BCM43455_EVENT_MASK_LENGTH], unsigned event);

#endif
