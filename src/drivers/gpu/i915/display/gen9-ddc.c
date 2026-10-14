/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The side channels of the Gen9 outputs (WS237 stage 3, see gen9-ddc.h).
 *
 * The AUX transfer follows the Linux kernel 6.8.12's
 * intel_dp_aux_xfer() and skl_get_aux_send_ctl() (MIT): wait for a
 * previous transfer, load the data registers, start with the send control
 * word, wait for the busy bit, clear the status, and retry up to five
 * times on a timeout or a receive error.  The DPCD and I2C-over-AUX
 * messages are DisplayPort's (drm_dp.h's request codes): a native read or
 * write of up to 16 bytes, and for the EDID an I2C write of the offset and
 * I2C reads of 16 bytes with the middle-of-transaction bit, ended by an
 * address-only read.  The GMBUS read follows do_gmbus_xfer() and
 * gmbus_xfer_read_chunk() with an index cycle for the EDID offset, and the
 * PCH GMBUS clock gating workaround (Display WA #0868).  The code is this
 * file's own.
 */

#include "gen9-ddc.h"
#include <kern/kcrt.h>

#include "../intel/display-regs-gen9.h"
#include "../intel/display-regs-gen9-modeset.h"

#include <uapi/errno.h>

/* How many times a transfer is started (the DP spec asks for at least three). */
#define I915_SKL_AUX_TRIES		5U

/* How long a previous transfer may keep the channel, and one transfer may run. */
#define I915_SKL_AUX_IDLE_TRIES		3U
#define I915_SKL_AUX_DONE_US		10000U

/* The pause after a receive error (DP CTS 4.2.1.1), and after a sink's deferral. */
#define I915_SKL_AUX_ERROR_PAUSE_US	400U
#define I915_SKL_AUX_DEFER_PAUSE_US	500U

/* How many times a deferred message is sent again. */
#define I915_SKL_AUX_DEFER_TRIES	32U

/* The bytes of an AUX request header without the length, and with it. */
#define I915_SKL_AUX_BARE_HEADER	3U
#define I915_SKL_AUX_HEADER		4U

/* The data registers' bytes. */
#define I915_SKL_AUX_DATA_BYTES		20U

/* How long the GMBUS may take for a byte group and to go idle, in microseconds. */
#define I915_SKL_GMBUS_WAIT_US		50000U
#define I915_SKL_GMBUS_IDLE_US		10000U

static void i915_skl_aux_header(uint8_t *header, unsigned request, uint32_t address, unsigned size);
static int i915_skl_aux_message(const struct i915_skl_io *io, unsigned port, unsigned request, uint32_t address, const uint8_t *data, unsigned size, uint8_t *reply, unsigned reply_capacity, int i2c);
static int i915_skl_gmbus_wait(const struct i915_skl_io *io, uint32_t status);
static int i915_skl_gmbus_block(const struct i915_skl_io *io, unsigned pin, uint8_t offset, uint8_t *bytes);
static void i915_skl_gmbus_finish(const struct i915_skl_io *io, int failed);

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
 *
 * Returns the reply's bytes (1 to 20, the first being the reply code), or
 * -EBUSY (the channel stayed busy, or no valid reply), -ETIMEDOUT (no sink
 * answered) or -EIO (a receive error).
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
	uint32_t ctl;
	uint32_t data;
	uint32_t control;
	uint32_t status;
	unsigned tries;
	unsigned index;
	unsigned received;

	/* The port's AUX control and data registers. */
	ctl = I915_SKL_AUX_CTL_A + port * I915_SKL_PORT_STRIDE;
	data = I915_SKL_AUX_DATA_A + port * I915_SKL_PORT_STRIDE;

	/* Refuses a message the five data registers do not hold. */
	if (send_bytes == 0U || send_bytes > I915_SKL_AUX_DATA_BYTES || receive_capacity > I915_SKL_AUX_DATA_BYTES)
		return -EINVAL;

	/* Waits for a previous transfer to end. */
	status = 0U;
	for (tries = 0U; tries < I915_SKL_AUX_IDLE_TRIES; tries++) {
		status = drv_i915_skl_read(io, ctl);
		if ((status & I915_SKL_AUX_SEND_BUSY) == 0U)
			break;
		io->msleep(io->context, 1U);
	}

	/* A channel that stayed busy. */
	if (tries == I915_SKL_AUX_IDLE_TRIES)
		return -EBUSY;

	/* The send control word: busy, the status bits to clear, the longest timeout, the size and the sync pulses. */
	control = I915_SKL_AUX_SEND_BUSY | I915_SKL_AUX_DONE | I915_SKL_AUX_INTERRUPT | I915_SKL_AUX_TIME_OUT_ERROR |
	    I915_SKL_AUX_TIME_OUT_MAX | I915_SKL_AUX_RECEIVE_ERROR | (send_bytes << I915_SKL_AUX_MESSAGE_SIZE_SHIFT) |
	    ((I915_SKL_AUX_FW_SYNC_LENGTH - 1U) << I915_SKL_AUX_FW_SYNC_PULSE_SHIFT) | ((I915_SKL_AUX_SYNC_LENGTH - 1U) << I915_SKL_AUX_SYNC_PULSE_SHIFT);

	/* Sends it until it is done, at most five times. */
	for (tries = 0U; tries < I915_SKL_AUX_TRIES; tries++) {
		/* The data, four bytes a register. */
		for (index = 0U; index < send_bytes; index += 4U)
			drv_i915_skl_write(io, data + index, drv_i915_skl_aux_pack(send + index, send_bytes - index));

		/* Starts the transfer and waits for the busy bit to drop. */
		drv_i915_skl_write(io, ctl, control);
		(void)drv_i915_skl_wait(io, ctl, I915_SKL_AUX_SEND_BUSY, 0U, I915_SKL_AUX_DONE_US, &status);

		/* Clears the done and error bits. */
		drv_i915_skl_write(io, ctl, status | I915_SKL_AUX_DONE | I915_SKL_AUX_TIME_OUT_ERROR | I915_SKL_AUX_RECEIVE_ERROR);

		/* A timeout already waited long enough; a receive error waits 400 us; a done transfer ends the loop. */
		if ((status & I915_SKL_AUX_TIME_OUT_ERROR) != 0U)
			continue;
		if ((status & I915_SKL_AUX_RECEIVE_ERROR) != 0U) {
			io->udelay(io->context, I915_SKL_AUX_ERROR_PAUSE_US);
			continue;
		}

		/* A transfer done ends the loop. */
		if ((status & I915_SKL_AUX_DONE) != 0U)
			break;
	}

	/* Reports a transfer that never finished, a receive error, or a sink that never answered. */
	if ((status & I915_SKL_AUX_DONE) == 0U)
		return -EBUSY;
	if ((status & I915_SKL_AUX_RECEIVE_ERROR) != 0U)
		return -EIO;
	if ((status & I915_SKL_AUX_TIME_OUT_ERROR) != 0U)
		return -ETIMEDOUT;

	/* The reply's size; 0 or more than 20 is not a reply. */
	received = (status >> I915_SKL_AUX_MESSAGE_SIZE_SHIFT) & I915_SKL_AUX_MESSAGE_SIZE_MASK;
	if (received == 0U || received > I915_SKL_AUX_DATA_BYTES)
		return -EBUSY;
	if (received > receive_capacity)
		received = receive_capacity;

	/* The reply, four bytes a register. */
	for (index = 0U; index < received; index += 4U)
		drv_i915_skl_aux_unpack(drv_i915_skl_read(io, data + index), receive + index, received - index);

	/* Succeeded: the reply's bytes. */
	return (int)received;
}

/* Reads DPCD bytes, 16 at a time: 0, or an errno. */
int
drv_i915_skl_dpcd_read(
	const struct i915_skl_io *io,
	unsigned port,
	uint32_t address,
	uint8_t *bytes,
	size_t count)
{
	uint8_t reply[I915_SKL_AUX_MAX_BYTES + 1U];
	size_t done;
	unsigned piece;
	int received;

	/* Each piece of up to 16 bytes. */
	done = 0U;
	while (done < count) {
		piece = (unsigned)(count - done);
		if (piece > I915_SKL_AUX_MAX_BYTES)
			piece = I915_SKL_AUX_MAX_BYTES;

		/* A native read of the piece. */
		received = i915_skl_aux_message(io, port, I915_DP_AUX_NATIVE_READ, address + (uint32_t)done, NULL, piece, reply, piece + 1U, 0);
		if (received < 0)
			return -received;

		/* A short reply is taken as what the sink had; nothing at all is an error. */
		if (received < 2)
			return EIO;
		kern_memcpy(bytes + done, reply + 1, (size_t)(received - 1));
		done += (size_t)(received - 1);
	}

	/* Succeeded: the bytes are read. */
	return 0;
}

/* Writes DPCD bytes, 16 at a time: 0, or an errno. */
int
drv_i915_skl_dpcd_write(
	const struct i915_skl_io *io,
	unsigned port,
	uint32_t address,
	const uint8_t *bytes,
	size_t count)
{
	uint8_t reply[2];
	size_t done;
	unsigned piece;
	int received;

	/* Each piece of up to 16 bytes. */
	done = 0U;
	while (done < count) {
		piece = (unsigned)(count - done);
		if (piece > I915_SKL_AUX_MAX_BYTES)
			piece = I915_SKL_AUX_MAX_BYTES;

		/* A native write of the piece. */
		received = i915_skl_aux_message(io, port, I915_DP_AUX_NATIVE_WRITE, address + (uint32_t)done, bytes + done, piece, reply, sizeof(reply), 0);
		if (received < 0)
			return -received;
		done += piece;
	}

	/* Succeeded: the bytes are written. */
	return 0;
}

/*
 * Reads EDID blocks through I2C-over-AUX: the offset written to the DDC
 * address, 16 bytes read at a time, and an address-only read that ends
 * the I2C transaction.  At most two blocks (one segment).
 */
int
drv_i915_skl_aux_edid(
	const struct i915_skl_io *io,
	unsigned port,
	uint8_t *edid,
	unsigned blocks)
{
	uint8_t reply[I915_SKL_AUX_MAX_BYTES + 1U];
	uint8_t offset;
	unsigned block;
	unsigned done;
	int received;
	int error;

	/* Only the first segment's two blocks. */
	if (blocks > 2U)
		blocks = 2U;

	/* Each block. */
	error = 0;
	for (block = 0U; block < blocks && error == 0; block++) {
		/* The offset of the block, written with the transaction kept open. */
		offset = (uint8_t)(block * I915_EDID_BLOCK_BYTES);
		received = i915_skl_aux_message(io, port, I915_DP_AUX_I2C_WRITE | I915_DP_AUX_I2C_MOT, I915_DDC_ADDR, &offset, 1U, reply, 2U, 1);
		if (received < 0) {
			error = -received;
			break;
		}

		/* The block, 16 bytes at a time, with the transaction kept open. */
		for (done = 0U; done < I915_EDID_BLOCK_BYTES; done += I915_SKL_AUX_MAX_BYTES) {
			received = i915_skl_aux_message(io, port, I915_DP_AUX_I2C_READ | I915_DP_AUX_I2C_MOT, I915_DDC_ADDR, NULL, I915_SKL_AUX_MAX_BYTES, reply, sizeof(reply), 1);
			if (received < 0) {
				error = -received;
				break;
			}

			/* A short read is an error. */
			if (received != (int)I915_SKL_AUX_MAX_BYTES + 1) {
				error = EIO;
				break;
			}

			/* The 16 bytes. */
			kern_memcpy(edid + block * I915_EDID_BLOCK_BYTES + done, reply + 1, I915_SKL_AUX_MAX_BYTES);
		}
	}

	/* Ends the I2C transaction with an address-only read, whatever came before. */
	(void)i915_skl_aux_message(io, port, I915_DP_AUX_I2C_READ, I915_DDC_ADDR, NULL, 0U, reply, 2U, 1);

	/* Reports a block that could not be read. */
	if (error != 0)
		return error;

	/* Succeeded: the blocks are read. */
	return 0;
}

/*
 * Reads EDID blocks through the GMBUS on a pin: for each block an index
 * cycle with the offset and a 128-byte read, then a stop; a NAK clears the
 * GMBUS.  Returns 0, ENXIO when nothing answers, ETIMEDOUT, or EINVAL.
 */
int
drv_i915_skl_gmbus_edid(
	const struct i915_skl_io *io,
	unsigned pin,
	uint8_t *edid,
	unsigned blocks)
{
	unsigned block;
	int error;

	/* A pin is needed, and only the first segment's two blocks are read. */
	if (pin == 0U)
		return EINVAL;
	if (blocks > 2U)
		blocks = 2U;

	/* The PCH's GMBUS unit is kept clocked during the transfer (Display WA #0868). */
	(void)drv_i915_skl_rmw(io, I915_SKL_SOUTH_DSPCLK_GATE_D, 0U, I915_SKL_PCH_GMBUSUNIT_CLOCK_GATE_DISABLE);

	/* Each block. */
	error = 0;
	for (block = 0U; block < blocks; block++) {
		error = i915_skl_gmbus_block(io, pin, (uint8_t)(block * I915_EDID_BLOCK_BYTES), edid + block * I915_EDID_BLOCK_BYTES);
		if (error != 0)
			break;
	}

	/* The stop, or the clearing after a NAK, and the GMBUS off. */
	i915_skl_gmbus_finish(io, error != 0);

	/* The clock gating comes back. */
	(void)drv_i915_skl_rmw(io, I915_SKL_SOUTH_DSPCLK_GATE_D, I915_SKL_PCH_GMBUSUNIT_CLOCK_GATE_DISABLE, 0U);

	/* Reports a block that could not be read. */
	if (error != 0)
		return error;

	/* Succeeded: the blocks are read. */
	return 0;
}

/* Reports the GMBUS pin of an HDMI port (gmbus_pins_skl), or 0. */
unsigned
drv_i915_skl_gmbus_pin(
	unsigned port)
{
	/* B, C and D have pins. */
	if (port == I915_SKL_PORT_B)
		return I915_SKL_GMBUS_PIN_DPB;
	if (port == I915_SKL_PORT_C)
		return I915_SKL_GMBUS_PIN_DPC;
	if (port == I915_SKL_PORT_D)
		return I915_SKL_GMBUS_PIN_DPD;

	/* Succeeded: no pin. */
	return 0U;
}

/* Builds an AUX request header (intel_dp_aux_header). */
static void
i915_skl_aux_header(
	uint8_t *header,
	unsigned request,
	uint32_t address,
	unsigned size)
{
	/* The request and the 20-bit address, then the length less one. */
	header[0] = (uint8_t)((request << 4) | ((address >> 16) & 0xfU));
	header[1] = (uint8_t)((address >> 8) & 0xffU);
	header[2] = (uint8_t)(address & 0xffU);
	header[3] = (uint8_t)(size - 1U);
}

/*
 * Sends one AUX request and checks the reply, sending it again while the
 * sink defers.  i2c says whether the I2C reply field matters too.  Returns
 * the reply's bytes, or a negative errno (-EIO for a NAK).
 */
static int
i915_skl_aux_message(
	const struct i915_skl_io *io,
	unsigned port,
	unsigned request,
	uint32_t address,
	const uint8_t *data,
	unsigned size,
	uint8_t *reply,
	unsigned reply_capacity,
	int i2c)
{
	uint8_t send[I915_SKL_AUX_HEADER + I915_SKL_AUX_MAX_BYTES];
	unsigned send_bytes;
	unsigned length;
	unsigned tries;
	uint8_t code;
	int received;

	/* The header, alone for an address-only message (its length field says one byte), else with the length and any data. */
	length = size;
	if (length == 0U)
		length = 1U;
	i915_skl_aux_header(send, request, address, length);
	send_bytes = I915_SKL_AUX_BARE_HEADER;
	if (size != 0U) {
		send_bytes = I915_SKL_AUX_HEADER;
		if (data != NULL) {
			kern_memcpy(send + I915_SKL_AUX_HEADER, data, size);
			send_bytes += size;
		}
	}

	/* Sends it until the sink stops deferring. */
	received = -EIO;
	for (tries = 0U; tries < I915_SKL_AUX_DEFER_TRIES; tries++) {
		received = drv_i915_skl_aux_xfer(io, port, send, send_bytes, reply, reply_capacity);
		if (received < 0)
			return received;

		/* The native reply: an ACK goes on, a deferral waits and repeats, anything else is a NAK. */
		code = (uint8_t)(reply[0] >> 4);
		if ((code & I915_DP_AUX_NATIVE_REPLY_MASK) == I915_DP_AUX_NATIVE_REPLY_DEFER) {
			io->udelay(io->context, I915_SKL_AUX_DEFER_PAUSE_US);
			continue;
		}

		/* Anything else is a NAK. */
		if ((code & I915_DP_AUX_NATIVE_REPLY_MASK) != I915_DP_AUX_NATIVE_REPLY_ACK)
			return -EIO;

		/* An I2C message also has the I2C reply, which defers or refuses on its own. */
		if (i2c) {
			if ((code & I915_DP_AUX_I2C_REPLY_MASK) == I915_DP_AUX_I2C_REPLY_DEFER) {
				io->udelay(io->context, I915_SKL_AUX_DEFER_PAUSE_US);
				continue;
			}

			/* Anything else from I2C is a NAK. */
			if ((code & I915_DP_AUX_I2C_REPLY_MASK) != I915_DP_AUX_I2C_REPLY_ACK)
				return -EIO;
		}

		/* Succeeded: the reply. */
		return received;
	}

	/* Succeeded in learning the sink kept deferring. */
	return -EBUSY;
}

/* Waits for a GMBUS2 status bit or a NAK (gmbus_wait): 0, ENXIO for a NAK, or ETIMEDOUT. */
static int
i915_skl_gmbus_wait(
	const struct i915_skl_io *io,
	uint32_t status)
{
	uint32_t value;
	unsigned waited;

	/* Polls GMBUS2 until the bit or a NAK. */
	for (waited = 0U; waited < I915_SKL_GMBUS_WAIT_US; waited += 10U) {
		value = drv_i915_skl_read(io, I915_SKL_GMBUS2);
		if ((value & I915_SKL_GMBUS_SATOER) != 0U)
			return ENXIO;
		if ((value & status) != 0U)
			return 0;
		io->udelay(io->context, 10U);
	}

	/* Succeeded in learning the GMBUS did not answer. */
	return ETIMEDOUT;
}

/* Reads one 128-byte block at an offset: an index cycle, the reads, and the wait phase. */
static int
i915_skl_gmbus_block(
	const struct i915_skl_io *io,
	unsigned pin,
	uint8_t offset,
	uint8_t *bytes)
{
	uint32_t word;
	unsigned left;
	unsigned taken;
	int error;

	/* The pin at 100 kHz. */
	drv_i915_skl_write(io, I915_SKL_GMBUS0, I915_SKL_GMBUS_RATE_100KHZ | pin);

	/* The read with the offset as its index. */
	drv_i915_skl_write(io, I915_SKL_GMBUS1,
	    I915_SKL_GMBUS_CYCLE_INDEX | ((uint32_t)offset << I915_SKL_GMBUS_SLAVE_INDEX_SHIFT) | I915_SKL_GMBUS_CYCLE_WAIT |
	    (I915_EDID_BLOCK_BYTES << I915_SKL_GMBUS_BYTE_COUNT_SHIFT) | (I915_DDC_ADDR << I915_SKL_GMBUS_SLAVE_ADDR_SHIFT) |
	    I915_SKL_GMBUS_SLAVE_READ | I915_SKL_GMBUS_SW_RDY);

	/* Four bytes at a time as they come. */
	left = I915_EDID_BLOCK_BYTES;
	while (left != 0U) {
		error = i915_skl_gmbus_wait(io, I915_SKL_GMBUS_HW_RDY);
		if (error != 0)
			return error;

		/* Four bytes, the first lowest. */
		word = drv_i915_skl_read(io, I915_SKL_GMBUS3);
		for (taken = 0U; taken < 4U && left != 0U; taken++) {
			*bytes = (uint8_t)(word & 0xffU);
			bytes++;
			word >>= 8;
			left--;
		}
	}

	/* The block's end. */
	error = i915_skl_gmbus_wait(io, I915_SKL_GMBUS_HW_WAIT_PHASE);
	if (error != 0)
		return error;

	/* Succeeded: the block is read. */
	return 0;
}

/* Ends a GMBUS transfer: a stop and the wait for idle, or after a NAK the idle wait and the clear; then the GMBUS off. */
static void
i915_skl_gmbus_finish(
	const struct i915_skl_io *io,
	int failed)
{
	/* A good transfer ends with a stop cycle. */
	if (!failed)
		drv_i915_skl_write(io, I915_SKL_GMBUS1, I915_SKL_GMBUS_CYCLE_STOP | I915_SKL_GMBUS_SW_RDY);

	/* The bus goes idle either way. */
	(void)drv_i915_skl_wait(io, I915_SKL_GMBUS2, I915_SKL_GMBUS_ACTIVE, 0U, I915_SKL_GMBUS_IDLE_US, NULL);

	/* A NAK is cleared by toggling the software clear. */
	if (failed) {
		drv_i915_skl_write(io, I915_SKL_GMBUS1, I915_SKL_GMBUS_SW_CLR_INT);
		drv_i915_skl_write(io, I915_SKL_GMBUS1, 0U);
	}

	/* The GMBUS off until the next transfer. */
	drv_i915_skl_write(io, I915_SKL_GMBUS0, 0U);
}
