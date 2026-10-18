/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The side channels of the Gen9 outputs (see gen9-ddc.h): the AUX channel
 * with its DPCD and I2C-over-AUX messages, and the GMBUS.
 */

#include "gen9-ddc.h"
#include "xd-aux.h"
#include "xd-gmbus.h"
#include "xd-io.h"
#include <kern/kcrt.h>

#include "../intel/display-regs-gen9.h"
#include "../intel/display-regs-gen9-modeset.h"

#include <uapi/errno.h>


/* Packs up to four bytes into an AUX data register, the first byte highest (intel_dp_aux_pack). */
uint32_t
drv_i915_skl_aux_pack(
	const uint8_t *bytes,
	unsigned count)
{
	uint32_t word;
	unsigned index;

	/* Up to four bytes, from bit 31 down. */
	if (count > 4U)
		count = 4U;
	word = 0U;
	for (index = 0U; index < count; index++)
		word |= (uint32_t)bytes[index] << ((3U - index) * 8U);

	/* Succeeded: the register's word. */
	return word;
}

/* Unpacks up to four bytes from an AUX data register (intel_dp_aux_unpack). */
void
drv_i915_skl_aux_unpack(
	uint32_t word,
	uint8_t *bytes,
	unsigned count)
{
	unsigned index;

	/* Up to four bytes, from bit 31 down. */
	if (count > 4U)
		count = 4U;
	for (index = 0U; index < count; index++)
		bytes[index] = (uint8_t)(word >> ((3U - index) * 8U));
}

/*
 * Sends one AUX message on a port's channel and receives the reply.
 * Returns the reply's bytes (1 to 20, the first being the reply code), or
 * -EBUSY (the channel stayed busy, or no valid reply), -ETIMEDOUT (no sink
 * answered), -EIO (a receive error) or -EINVAL (a message too long).
 */
int
drv_i915_skl_aux_xfer(
	const struct i915_skl_io *io,
	unsigned port,
	const uint8_t *send,
	unsigned send_bytes,
	uint8_t *receive,
	unsigned receive_capacity)
{
	struct i915_xd_aux aux;
	struct i915_xd_aux_message message;
	struct i915_xd_aux_result result;
	int error;

	/* Refuses an empty message. */
	if (send_bytes == 0U)
		return -EINVAL;

	/* The port's channel of the xd code. */
	error = drv_i915_xd_aux_init(&aux, port);
	if (error != 0)
		return -EINVAL;

	/* The exchange. */
	message.request = send;
	message.request_bytes = send_bytes;
	message.reply = receive;
	message.reply_room = receive_capacity;
	message.thunderbolt = 0;
	message.hdcp_key = 0;
	drv_i915_xd_aux_exchange(&aux, drv_i915_skl_xd_io(io), &message, &result);

	/* Each way it can fail. */
	switch (result.outcome) {
	case I915_XD_AUX_SENT:
		break;
	case I915_XD_AUX_TOO_LONG:
		return -EINVAL;
	case I915_XD_AUX_RECEIVE_ERROR:
		return -EIO;
	case I915_XD_AUX_NO_ANSWER:
		return -ETIMEDOUT;
	default:
		return -EBUSY;
	}

	/* Succeeded: the reply's bytes. */
	return (int)result.reply_bytes;
}

/* Reads DPCD bytes: 0, or an errno. */
int
drv_i915_skl_dpcd_read(
	const struct i915_skl_io *io,
	unsigned port,
	uint32_t address,
	uint8_t *bytes,
	size_t count)
{
	struct i915_xd_aux aux;
	int error;

	/* The port's channel of the xd code. */
	error = drv_i915_xd_aux_init(&aux, port);
	if (error != 0)
		return error;

	/* The read. */
	error = drv_i915_xd_dpcd_read(&aux, drv_i915_skl_xd_io(io), address, bytes, (unsigned)count);
	if (error != 0)
		return error;

	/* Succeeded: the bytes are read. */
	return 0;
}

/* Writes DPCD bytes: 0, or an errno. */
int
drv_i915_skl_dpcd_write(
	const struct i915_skl_io *io,
	unsigned port,
	uint32_t address,
	const uint8_t *bytes,
	size_t count)
{
	struct i915_xd_aux aux;
	int error;

	/* The port's channel of the xd code. */
	error = drv_i915_xd_aux_init(&aux, port);
	if (error != 0)
		return error;

	/* The write. */
	error = drv_i915_xd_dpcd_write(&aux, drv_i915_skl_xd_io(io), address, bytes, (unsigned)count);
	if (error != 0)
		return error;

	/* Succeeded: the bytes are written. */
	return 0;
}

/* Reads EDID blocks of the first segment through I2C-over-AUX: 0, or an errno. */
int
drv_i915_skl_aux_edid(
	const struct i915_skl_io *io,
	unsigned port,
	uint8_t *edid,
	unsigned blocks)
{
	struct i915_xd_aux aux;
	int error;

	/* The port's channel of the xd code. */
	error = drv_i915_xd_aux_init(&aux, port);
	if (error != 0)
		return error;

	/* The blocks. */
	error = drv_i915_xd_aux_edid_blocks(&aux, drv_i915_skl_xd_io(io), edid, blocks);
	if (error != 0)
		return error;

	/* Succeeded: the blocks are read. */
	return 0;
}

/*
 * Reads EDID blocks through the GMBUS on a pin.  Returns 0, ENXIO when
 * nothing answers, ETIMEDOUT, or EINVAL.
 */
int
drv_i915_skl_gmbus_edid(
	const struct i915_skl_io *io,
	unsigned pin,
	uint8_t *edid,
	unsigned blocks)
{
	struct i915_xd_gmbus gmbus;
	int error;

	/* The xd code's GMBUS, read on display 9. */
	drv_i915_xd_gmbus_init(&gmbus);
	error = drv_i915_xd9_gmbus_edid_blocks(&gmbus, drv_i915_skl_xd_io(io), pin, edid, blocks);
	if (error != 0)
		return error;

	/* Succeeded: the blocks are read. */
	return 0;
}

/* Reports the GMBUS pin of an HDMI port, or 0. */
unsigned
drv_i915_skl_gmbus_pin(
	unsigned port)
{
	unsigned pin;

	/* The xd code's display 9 pin. */
	pin = drv_i915_xd9_gmbus_pin(port);

	/* Succeeded: the pin, or 0. */
	return pin;
}
