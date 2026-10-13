/* -*- mode: c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Intel AX211 private initialization protocol codecs.
 *
 * Sources of the values.  The register offsets, bit positions, command
 * numbers, firmware file record types and wire layouts in this file are facts
 * of the AX211 hardware and of its firmware interface.  Intel publishes no
 * register or host-command specification for this device, so the values were
 * collected with Intel's own driver headers as the primary source: Linux
 * iwlwifi at tag v6.17, under drivers/net/wireless/intel/iwlwifi/, in
 * fw/api/commands.h, fw/api/nvm-reg.h and fw/api/alive.h.  They were
 * cross-checked against OpenBSD sys/dev/pci/if_iwxreg.h and if_iwx.c at
 * commit 0f464d413c50.  Each block of values below names the header it was
 * taken from.
 *
 * This file is not an adaptation of those headers.  It collects the interface
 * values, gives them names of its own, and adds what the zedBSD driver
 * established about each of them while it was brought up on AX211 hardware:
 * where the value is used, what it means to the device or to the firmware,
 * the order the device requires, and how the firmware behaved.  It is written
 * as a reference that a developer of an AX211 driver for another operating
 * system can use on its own.
 */

#include "intel-ax211-init.h"

static int ax211_init_version_require_absent( const struct intel_ax211_protocol_command_table *table, uint8_t group, uint8_t opcode);
static int ax211_init_version_require( const struct intel_ax211_protocol_command_table *table, uint8_t group, uint8_t opcode, uint8_t command_version, uint8_t notification_version);
static void ax211_init_put_le32(uint8_t *bytes, uint32_t value);
static uint32_t ax211_init_get_le32(const uint8_t *bytes);

/*
 * The digest-pinned API89 image does not advertise g2/c03 or legacy g0/c04. OpenBSD supplies the former's fixed v1 wire layout and treats the latter as a legacy, versionless notification.  Their absence is part of the exact artifact identity rather than permission to infer a version.
 */
int
drv_intel_ax211_init_api89_validate(
	const struct intel_ax211_protocol_command_table *table)
{
	int error;
	int result;

	/* Checks the operation result. */
	result = drv_intel_ax211_protocol_command_table_validate_api89(table);
	if (result != INTEL_AX211_PROTOCOL_OK)
		return result;

	/* Checks the operation result. */
	result = ax211_init_version_require_absent(
		table, INTEL_AX211_INIT_FAMILY_PLATFORM,
		INTEL_AX211_INIT_STAGE_SELECT_OPCODE);
	if (result != INTEL_AX211_PROTOCOL_OK)
		return result;

	/* Checks the operation result. */
	result = ax211_init_version_require_absent(
		table, INTEL_AX211_PROTOCOL_FAMILY_CLASSIC,
		INTEL_AX211_PROTOCOL_INIT_FINISHED_OPCODE);
	if (result != INTEL_AX211_PROTOCOL_OK)
		return result;

	/* Checks the operation result. */
	result = ax211_init_version_require(
		table, INTEL_AX211_PROTOCOL_FAMILY_REGULATORY_STORE,
		INTEL_AX211_PROTOCOL_NVM_HANDOFF_OPCODE,
		INTEL_AX211_INIT_NVM_HANDOFF_COMMAND_VERSION,
		INTEL_AX211_INIT_NVM_HANDOFF_NOTIFICATION_VERSION);
	if (result != INTEL_AX211_PROTOCOL_OK)
		return result;

	/* Obtains the ax211 init version require result. */
	error = ax211_init_version_require(
		table, INTEL_AX211_PROTOCOL_FAMILY_REGULATORY_STORE,
		INTEL_AX211_PROTOCOL_NVM_QUERY_OPCODE,
		INTEL_AX211_INIT_NVM_QUERY_COMMAND_VERSION,
		INTEL_AX211_PROTOCOL_NVM_QUERY_VERSION);

	/* Returns the computed result. */
	return error;
}

/*
 * Implements the drv intel ax211 init extended cfg encode operation.
 */
int
drv_intel_ax211_init_stage_select_encode(
	enum intel_ax211_init_profile profile,
	uint8_t output[INTEL_AX211_INIT_STAGE_SELECT_SIZE])
{
	uint32_t flags;

	/* Handles the output availability. */
	if (output == NULL)
		return INTEL_AX211_PROTOCOL_INVALID;

	/* Handles the profile condition. */
	if (profile == INTEL_AX211_INIT_PROFILE_RUNTIME)
		flags = 0U;
	else if (profile == INTEL_AX211_INIT_PROFILE_STORE_READ)
		flags = INTEL_AX211_INIT_STAGE_SELECT_STORE_READ;
	else {
		/* Returns the computed result. */
		return INTEL_AX211_PROTOCOL_UNSUPPORTED;
	}
	ax211_init_put_le32(output, flags);

	/* Returns the computed result. */
	return INTEL_AX211_PROTOCOL_OK;
}

/*
 * Implements the drv intel ax211 init extended cfg decode operation.
 */
int
drv_intel_ax211_init_stage_select_decode(
	const uint8_t *bytes,
	size_t length,
	enum intel_ax211_init_profile *profile)
{
	enum intel_ax211_init_profile decoded;
	uint32_t flags;

	/* Handles the bytes availability. */
	if (bytes == NULL || profile == NULL)
		return INTEL_AX211_PROTOCOL_INVALID;

	/* Checks the current data length. */
	if (length < INTEL_AX211_INIT_STAGE_SELECT_SIZE)
		return INTEL_AX211_PROTOCOL_TRUNCATED;

	/* Checks the current data length. */
	if (length > INTEL_AX211_INIT_STAGE_SELECT_SIZE)
		return INTEL_AX211_PROTOCOL_OVERSIZED;

	/* Checks the active flags. */
	flags = ax211_init_get_le32(bytes);
	if (flags == 0U)
		decoded = INTEL_AX211_INIT_PROFILE_RUNTIME;
	else if (flags == INTEL_AX211_INIT_STAGE_SELECT_STORE_READ)
		decoded = INTEL_AX211_INIT_PROFILE_STORE_READ;
	else {
		/* Returns the computed result. */
		return INTEL_AX211_PROTOCOL_UNSUPPORTED;
	}
	*profile = decoded;
	/* Returns the computed result. */
	return INTEL_AX211_PROTOCOL_OK;
}

/*
 * Implements the drv intel ax211 init complete validate operation.
 */
int
drv_intel_ax211_init_finished_validate(
	const struct intel_ax211_protocol_message *message,
	uint32_t generation)
{
	int error;

	/* Handles the generation condition. */
	if (generation == 0U)
		return INTEL_AX211_PROTOCOL_INVALID;

	/* Obtains the drv intel ax211 protocol init complete result. */
	error =
		drv_intel_ax211_protocol_init_finished(message, generation);

	/* Returns the computed result. */
	return error;
}

/* Supports the ax211 init version require absent operation. */
static int
ax211_init_version_require_absent(
	const struct intel_ax211_protocol_command_table *table,
	uint8_t group,
	uint8_t opcode)
{
	struct intel_ax211_protocol_command_version version;
	int result;

	/* Checks the operation result. */
	result = drv_intel_ax211_protocol_command_version_lookup(
		table, group, opcode, &version);
	if (result == INTEL_AX211_PROTOCOL_MISSING)
		return INTEL_AX211_PROTOCOL_OK;
	if (result == INTEL_AX211_PROTOCOL_OK)
		return INTEL_AX211_PROTOCOL_UNSUPPORTED;

	/* Returns the computed result. */
	return result;
}

/* Supports the ax211 init version require operation. */
static int
ax211_init_version_require(
	const struct intel_ax211_protocol_command_table *table,
	uint8_t group,
	uint8_t opcode,
	uint8_t command_version,
	uint8_t notification_version)
{
	struct intel_ax211_protocol_command_version version;
	int result;

	/* Checks the operation result. */
	result = drv_intel_ax211_protocol_command_version_lookup(
		table, group, opcode, &version);
	if (result != INTEL_AX211_PROTOCOL_OK)
		return result;

	/* Handles the version condition. */
	if (version.command_version != command_version ||
	    version.notification_version != notification_version) {
		/* Returns the computed result. */
		return INTEL_AX211_PROTOCOL_UNSUPPORTED;
	}

	/* Returns the computed result. */
	return INTEL_AX211_PROTOCOL_OK;
}

/* Supports the ax211 init put le32 operation. */
static void
ax211_init_put_le32(
	uint8_t *bytes,
	uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
}

/* Supports the ax211 init get le32 operation. */
static uint32_t
ax211_init_get_le32(
	const uint8_t *bytes)
{
	/* Returns the computed result. */
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
	       ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}
