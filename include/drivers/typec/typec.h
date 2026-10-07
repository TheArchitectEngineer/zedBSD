/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The USB Type-C connector layer (kernel-internal).
 *
 * A connector driver (the UCSI driver) keeps one record per USB-C
 * connector: whether something is attached, the power role and contract,
 * the partner's kind and the Alternate Modes of the connector, the
 * partner and the cable.  Other drivers (DisplayPort Alternate Mode,
 * power management) read a copy of a record and register a listener that
 * is told which connector changed.  Every change stamps the record with a
 * new generation, so a reader can tell whether what it holds is current.
 */

#ifndef KERN_DRIVERS_TYPEC_TYPEC_H
#define KERN_DRIVERS_TYPEC_TYPEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The most connectors kept (UCSI numbers up to 127; a PC has a few).
 */
#define DRV_TYPEC_CONNECTOR_MAX 16U

/*
 * The most Alternate Modes kept for each of the connector, the partner and
 * the cable.
 */
#define DRV_TYPEC_ALT_MODE_MAX 16U

/*
 * The most Power Data Objects a USB PD port advertises.
 */
#define DRV_TYPEC_PDO_MAX 7U

/*
 * The most Alternate Modes a connector is in at once that are kept.
 */
#define DRV_TYPEC_CURRENT_MODE_MAX 4U

/*
 * The most listeners registered at once.
 */
#define DRV_TYPEC_LISTENER_MAX 4U

/*
 * The Standard or Vendor ID of the DisplayPort Alternate Mode.
 */
#define DRV_TYPEC_SVID_DISPLAYPORT 0xFF01U

/*
 * What a connector can do (from its capability), as bits.
 */
enum drv_typec_capability {
	DRV_TYPEC_CAPABILITY_SOURCE_ONLY = 1U << 0,
	DRV_TYPEC_CAPABILITY_SINK_ONLY = 1U << 1,
	DRV_TYPEC_CAPABILITY_DUAL_ROLE = 1U << 2,
	DRV_TYPEC_CAPABILITY_AUDIO_ACCESSORY = 1U << 3,
	DRV_TYPEC_CAPABILITY_DEBUG_ACCESSORY = 1U << 4,
	DRV_TYPEC_CAPABILITY_USB2 = 1U << 5,
	DRV_TYPEC_CAPABILITY_USB3 = 1U << 6,
	DRV_TYPEC_CAPABILITY_ALT_MODE = 1U << 7,
	DRV_TYPEC_CAPABILITY_PROVIDER = 1U << 8,
	DRV_TYPEC_CAPABILITY_CONSUMER = 1U << 9,
	DRV_TYPEC_CAPABILITY_SWAP_TO_DFP = 1U << 10,
	DRV_TYPEC_CAPABILITY_SWAP_TO_UFP = 1U << 11,
	DRV_TYPEC_CAPABILITY_SWAP_TO_SOURCE = 1U << 12,
	DRV_TYPEC_CAPABILITY_SWAP_TO_SINK = 1U << 13,
};

/*
 * How power is delivered on an attached connector.
 */
enum drv_typec_power_operation {
	DRV_TYPEC_POWER_UNKNOWN = 0,
	DRV_TYPEC_POWER_USB_DEFAULT = 1,
	DRV_TYPEC_POWER_BC = 2,
	DRV_TYPEC_POWER_PD = 3,
	DRV_TYPEC_POWER_TYPEC_1_5A = 4,
	DRV_TYPEC_POWER_TYPEC_3A = 5,
	DRV_TYPEC_POWER_TYPEC_5A = 6,
};

/*
 * The role a connector plays in the power it carries.
 */
enum drv_typec_power_role {
	DRV_TYPEC_ROLE_SINK = 0,
	DRV_TYPEC_ROLE_SOURCE = 1,
};

/*
 * What is attached to a connector.
 */
enum drv_typec_partner_type {
	DRV_TYPEC_PARTNER_NONE = 0,
	DRV_TYPEC_PARTNER_DFP = 1,
	DRV_TYPEC_PARTNER_UFP = 2,
	DRV_TYPEC_PARTNER_POWERED_CABLE = 3,
	DRV_TYPEC_PARTNER_POWERED_CABLE_UFP = 4,
	DRV_TYPEC_PARTNER_DEBUG_ACCESSORY = 5,
	DRV_TYPEC_PARTNER_AUDIO_ACCESSORY = 6,
};

/*
 * What a connector is carrying to its partner, as bits.
 */
enum drv_typec_partner_flag {
	DRV_TYPEC_PARTNER_USB = 1U << 0,
	DRV_TYPEC_PARTNER_ALT_MODE = 1U << 1,
	DRV_TYPEC_PARTNER_USB4 = 1U << 2,
};

/*
 * Which way the plug is turned, when the connector driver knows.
 */
enum drv_typec_orientation {
	DRV_TYPEC_ORIENTATION_UNKNOWN = 0,
	DRV_TYPEC_ORIENTATION_NORMAL = 1,
	DRV_TYPEC_ORIENTATION_FLIPPED = 2,
};

/*
 * One Alternate Mode: its Standard or Vendor ID and the mode's VDO.
 */
struct drv_typec_alt_mode {
	uint16_t svid;
	uint32_t vdo;
};

/*
 * A list of Alternate Modes, in the order the connector driver reported
 * them (an index into the connector's list names a mode it can enter).
 */
struct drv_typec_alt_mode_list {
	struct drv_typec_alt_mode modes[DRV_TYPEC_ALT_MODE_MAX];
	unsigned count;
};

/*
 * Everything known about one connector at one generation.
 *
 * A copy is what drv_typec_connector_get() hands out; the connector
 * driver publishes a whole new record with drv_typec_connector_publish().
 */
struct drv_typec_connector {
	/* The generation this record was published at (0: never). */
	uint64_t generation;

	/* What the connector can do (enum drv_typec_capability bits). */
	uint32_t capability;

	/* Whether something is attached; the fields below need it. */
	bool connected;

	/* How power is delivered, and the role this side plays. */
	enum drv_typec_power_operation power_operation;
	enum drv_typec_power_role power_role;

	/* The attached partner's kind and what is carried to it (enum drv_typec_partner_flag bits). */
	enum drv_typec_partner_type partner_type;
	uint32_t partner_flags;

	/* The plug's orientation (unknown unless the connector driver reports it). */
	enum drv_typec_orientation orientation;

	/* The USB PD Request Data Object of the contract (0: none known). */
	uint32_t request_data_object;

	/* The Alternate Modes of the connector, the partner (SOP) and the cable (SOP'). */
	struct drv_typec_alt_mode_list connector_modes;
	struct drv_typec_alt_mode_list partner_modes;
	struct drv_typec_alt_mode_list cable_modes;

	/* Which connector modes can be entered now, one bit per index of connector_modes. */
	uint8_t supported_modes[(DRV_TYPEC_ALT_MODE_MAX + 7U) / 8U];

	/* The indexes into connector_modes of the modes the connector is in. */
	uint8_t current_modes[DRV_TYPEC_CURRENT_MODE_MAX];
	unsigned current_mode_count;

	/* The partner's Power Data Objects (its source ones when this side sinks, else its sink ones). */
	uint32_t partner_pdos[DRV_TYPEC_PDO_MAX];
	unsigned partner_pdo_count;
};

/*
 * A listener: told the connector index (0-based) and the generation of
 * each published change.  It runs on the connector driver's thread and
 * must not block or wait for that thread.
 */
typedef void (*drv_typec_listener)(void *argument, unsigned connector, uint64_t generation);

/*
 * Registers a listener of connector changes.
 */
int
drv_typec_listener_register(
	drv_typec_listener listener,
	void *argument);

/*
 * Reports how many connectors there are.
 */
unsigned
drv_typec_connector_count(void);

/*
 * Copies the record of a connector.
 */
int
drv_typec_connector_get(
	unsigned index,
	struct drv_typec_connector *connector);

/*
 * Sets how many connectors the connector driver found (records reset).
 */
void
drv_typec_connectors_reset(
	unsigned count);

/*
 * Publishes a new record of a connector and tells the listeners.
 */
int
drv_typec_connector_publish(
	unsigned index,
	const struct drv_typec_connector *connector);

#endif
