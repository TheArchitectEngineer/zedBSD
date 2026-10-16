/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The DP AUX channel of the eDP port.
 *
 * The transfer is the Linux v6.8.12 drivers/gpu/drm/i915/display/
 * intel_dp_aux.c (sha256 f4a5dff0764804bdc7b5a57f79b11d3f10dfa8869f09f0df37f6fa999e777925)
 * rewritten in this tree's style: intel_dp_aux_xfer() and
 * intel_dp_aux_transfer() with the header helper, with their power
 * references unchanged.  The hardware transaction itself (the data words,
 * the control word, the tries and the waits) is the xd AUX channel's
 * (xd-aux.c), which this file reaches through the environment's AUX
 * window; this file turns its outcome into the Linux errno and messages.
 *
 * The file also holds the DPCD hooks the modeset reaches the live eDP's AUX
 * channel through (struct i915_lcd_emit).
 *
 * The messages of the Linux text show the format text only and never
 * evaluate their arguments (dp-internal.h); a transfer the channel does not
 * finish is reported with its values instead (i915_dp_aux_report()).
 *
 * The Linux original is under the MIT licence:
 *
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "dp-internal.h"
#include "aux.h"
#include "dp-sink.h"

#include <kern/klog.h>

/* The bytes of an AUX request header without, and with, the length byte. */
#define BARE_ADDRESS_SIZE	3
#define HEADER_SIZE		(BARE_ADDRESS_SIZE + 1)

/*
 * The AUX power wells' BIOS and driver request registers (the Linux
 * ICL_PWR_WELL_CTL_AUX1 and _AUX2), read into a failed transfer's report:
 * two bits a well, the state at 2n and the request at 2n + 1.
 */
#define I915_DP_AUX_WELLS_BIOS		0x45440u
#define I915_DP_AUX_WELLS_DRIVER	0x45444u

/*
 * The main power wells' driver requests (HSW_PWR_WELL_CTL2: PW_1 at bits 0
 * and 1, PW_2 at 2 and 3, PW_A to D after them) and the DC state enable
 * (DC_STATE_EN), also in the report: an AUX channel needs PW_2 up and the
 * display out of DC5 and DC6.
 */
#define I915_DP_MAIN_WELLS_DRIVER	0x45404u
#define I915_DP_DC_STATE_EN		0x45504u

/* The failed transfers reported in full: the first ones, then one of every so many. */
#define I915_DP_AUX_REPORTS_FIRST	8u
#define I915_DP_AUX_REPORTS_EVERY	64u

/*
 * How many failed AUX transfers were reported, of every port.
 *
 * It only grows, under the AUX mutex the transfers hold; it decides which
 * failures are logged in full, so a sink that never answers does not flood
 * the log.
 */
static unsigned i915_dp_aux_reports;

static unsigned i915_dp_aux_channel(struct intel_dp *intel_dp);
static int i915_dp_aux_exchange(struct intel_dp *intel_dp, struct drm_i915_private *i915, const u8 *send, int send_bytes, u8 *recv, int recv_size, int hdcp_key);
static void i915_dp_aux_stalled(void *context, uint32_t status);
static int i915_dp_aux_xfer(struct intel_dp *intel_dp, const u8 *send, int send_bytes, u8 *recv, int recv_size, int hdcp_key);
static void i915_dp_aux_header(u8 txbuf[HEADER_SIZE], const struct drm_dp_aux_msg *msg);
static int i915_dp_aux_hdcp_key(const struct drm_dp_aux_msg *msg);
static i915_dp_ssize_t i915_dp_aux_transfer(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg);
static struct i915_dp_world *i915_aux_emit_world(void *ctx);
static void i915_dp_aux_report(struct intel_dp *intel_dp, struct drm_i915_private *i915, i915_reg_t ch_ctl, const char *what, u32 status);

/*
 * Binds the AUX channel of a DP port to its hardware channel (the Linux
 * intel_dp_aux_init() for display versions 12 and 13).
 *
 * The port's channel is prepared on the xd AUX channel of the same
 * number, whose stalls this file reports; the channel's mutex and DDC
 * adapter are prepared, and the transfer hook is set.
 */
void
drv_i915_dp_aux_init(
	struct intel_dp *intel_dp)
{
	unsigned channel;

	/* Prepares the hardware channel the port's channel names. */
	channel = i915_dp_aux_channel(intel_dp);
	(void)drv_i915_xd_aux_init(&intel_dp->xd_aux, channel);
	intel_dp->xd_aux.stalled = i915_dp_aux_stalled;
	intel_dp->xd_aux.context = intel_dp;

	/* Prepares the channel's mutex and DDC adapter, then sets the transfer hook. */
	drv_i915_drm_dp_aux_init(&intel_dp->aux);
	intel_dp->aux.transfer = i915_dp_aux_transfer;
}

/*
 * Reads DPCD bytes of the live eDP for the modeset (drm_dp_dpcd_read()).
 *
 * It is the dpcd_read hook of struct i915_lcd_emit; ctx is the panel run
 * (struct i915_lcd_kernel).  It returns the bytes transferred, or a negative
 * Linux errno (-I915_EDP_EINVAL without a live eDP).
 */
long
drv_i915_edp_emit_dpcd_read(
	void *ctx,
	unsigned offset,
	uint8_t *buf,
	size_t size)
{
	struct i915_dp_world *world;
	long transferred;

	/* Finds the eDP world of the run's panel. */
	world = i915_aux_emit_world(ctx);

	/* Reads the bytes over the live AUX channel. */
	transferred = drv_i915_edp_dpcd_read(world, offset, buf, size);
	if (transferred < 0)
		return transferred;

	/* Succeeded: reports the bytes read. */
	return transferred;
}

/*
 * Writes DPCD bytes of the live eDP for the modeset (drm_dp_dpcd_write()).
 *
 * It is the dpcd_write hook of struct i915_lcd_emit, as
 * drv_i915_edp_emit_dpcd_read().
 */
long
drv_i915_edp_emit_dpcd_write(
	void *ctx,
	unsigned offset,
	const uint8_t *buf,
	size_t size)
{
	struct i915_dp_world *world;
	long transferred;

	/* Finds the eDP world of the run's panel. */
	world = i915_aux_emit_world(ctx);

	/* Writes the bytes over the live AUX channel. */
	transferred = drv_i915_edp_dpcd_write(world, offset, buf, size);
	if (transferred < 0)
		return transferred;

	/* Succeeded: reports the bytes written. */
	return transferred;
}

/*
 * Reads the receiver capabilities of the live eDP for the modeset
 * (drm_dp_read_dpcd_caps()).
 *
 * It is the read_dpcd_caps hook of struct i915_lcd_emit; it returns 0, or a
 * negative Linux errno.
 */
int
drv_i915_edp_emit_read_dpcd_caps(
	void *ctx,
	uint8_t dpcd[15])
{
	struct i915_dp_world *world;
	int read;

	/* Finds the eDP world of the run's panel. */
	world = i915_aux_emit_world(ctx);

	/* Reads the capabilities, with the extended field, over the live AUX channel. */
	read = drv_i915_edp_read_dpcd_caps(world, dpcd);
	if (read != 0)
		return read;

	/* Succeeded: the capabilities are in dpcd. */
	return 0;
}

/*
 * Reports the hardware channel of a port's AUX channel: A, B and C, then
 * the six Type-C channels (USBC5 and USBC6 are also display 13's D and E),
 * numbered as the display numbers them.  A channel the display does not
 * have falls back to A.
 */
static unsigned
i915_dp_aux_channel(
	struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port;
	enum aux_ch aux_ch;

	/* Finds the port's channel. */
	dig_port = i915_dp_dp_to_dig_port(intel_dp);
	aux_ch = dig_port->aux_ch;

	/* Keeps a channel the display has; reports any other and uses A. */
	switch (aux_ch) {
	case AUX_CH_A:
	case AUX_CH_B:
	case AUX_CH_C:
	case AUX_CH_USBC1:
	case AUX_CH_USBC2:
	case AUX_CH_USBC3:
	case AUX_CH_USBC4:
	case AUX_CH_USBC5:
	case AUX_CH_USBC6:
		return (unsigned)aux_ch;
	default:
		I915_DP_MISSING_CASE(aux_ch);
		return (unsigned)AUX_CH_A;
	}
}

/*
 * Runs one AUX message on the port's hardware channel and turns what the
 * channel reports into the Linux result: the bytes received, or -EBUSY
 * (the channel stays busy, does not finish, or reports a forbidden size;
 * the DRM layer then retries), -E2BIG for a message larger than the five
 * data registers, -EIO for a receive error and -ETIMEDOUT when the sink
 * did not answer.  The channel's power, the PPS lock and VDD are held by
 * the caller.
 */
static int
i915_dp_aux_exchange(
	struct intel_dp *intel_dp,
	struct drm_i915_private *i915,
	const u8 *send,
	int send_bytes,
	u8 *recv,
	int recv_size,
	int hdcp_key)
{
	struct intel_digital_port *dig_port;
	struct i915_xd_aux_message message;
	struct i915_xd_aux_result result;
	struct i915_xd_io *io;
	int missing;
	bool tbt;

	/* Finds the AUX window the environment's binding opened. */
	io = i915_dp_env_of(i915)->aux_io;
	missing = I915_DP_DRM_WARN_ON(&i915->drm, io == NULL);
	if (missing)
		return -I915_DP_EIO;

	/* A Type-C port in TBT-alt mode sends through the TBT I/O. */
	dig_port = i915_dp_dp_to_dig_port(intel_dp);
	tbt = i915_dp_intel_tc_port_in_tbt_alt_mode(dig_port);

	/* Describes the message, its reply room and how the channel reaches the sink. */
	message.request = send;
	message.request_bytes = (unsigned)send_bytes;
	message.reply = recv;
	message.reply_room = (unsigned)recv_size;
	message.thunderbolt = 0;
	if (tbt)
		message.thunderbolt = 1;
	message.hdcp_key = hdcp_key;

	/* Runs the transaction. */
	drv_i915_xd_aux_exchange(&intel_dp->xd_aux, io, &message, &result);

	/* Turns what the channel reported into the Linux result and messages. */
	switch (result.outcome) {
	case I915_XD_AUX_BUSY:
		/* A channel still busy is reported once per new status. */
		if (result.status != intel_dp->aux_busy_last_status) {
			(void)I915_DP_DRM_WARN(&i915->drm, 1,
					       "%s: not started (status 0x%08x)\n",
					       intel_dp->aux.name, result.status);
			intel_dp->aux_busy_last_status = result.status;
		}

		return -I915_DP_EBUSY;
	case I915_XD_AUX_TOO_LONG:
		/* The channel has only five data registers. */
		(void)I915_DP_DRM_WARN_ON(&i915->drm, send_bytes > 20 || recv_size > 20);
		return -I915_DP_E2BIG;
	case I915_XD_AUX_UNFINISHED:
		/* A message the channel never finished. */
		i915_dp_aux_report(intel_dp, i915, DP_AUX_CH_CTL(intel_dp->xd_aux.channel), "not done", result.status);
		return -I915_DP_EBUSY;
	case I915_XD_AUX_RECEIVE_ERROR:
		/* A reply that arrived damaged. */
		i915_dp_aux_report(intel_dp, i915, DP_AUX_CH_CTL(intel_dp->xd_aux.channel), "receive error", result.status);
		return -I915_DP_EIO;
	case I915_XD_AUX_NO_ANSWER:
		/* A timeout, which is normal when nothing is connected, only at debug level. */
		I915_DP_DRM_DBG_KMS(&i915->drm, "%s: timeout (status 0x%08x)\n",
				    intel_dp->aux.name, result.status);
		return -I915_DP_ETIMEDOUT;
	case I915_XD_AUX_BAD_SIZE:
		/* A received size of 0 or more than 20, which BSpec forbids; -EBUSY lets the DRM layer retry. */
		I915_DP_DRM_DBG_KMS(&i915->drm,
				    "%s: Forbidden recv_bytes = %d on aux transaction\n",
				    intel_dp->aux.name, (int)result.reply_bytes);
		return -I915_DP_EBUSY;
	default:
		break;
	}

	/* Succeeded: reports the bytes received. */
	return (int)result.reply_bytes;
}

/*
 * Reports a try of a transfer that did not complete within its 10 ms; the
 * context is the port's struct intel_dp.
 */
static void
i915_dp_aux_stalled(
	void *context,
	uint32_t status)
{
	struct intel_dp *intel_dp;
	struct drm_i915_private *i915;

	/* Finds the port and the device. */
	intel_dp = context;
	i915 = i915_dp_dp_to_i915(intel_dp);

	/* Reports the stall with the channel's state as it reads now. */
	i915_dp_aux_report(intel_dp, i915, DP_AUX_CH_CTL(intel_dp->xd_aux.channel), "did not complete within 10 ms", status);
}

/*
 * Runs one AUX message on the port (the Linux intel_dp_aux_xfer()).
 *
 * The AUX power domain is taken, then the PPS lock, and VDD is forced on
 * for the message when it was not on already; all three are given back in
 * reverse order, the power reference asynchronously.  It returns the bytes
 * received, or a negative Linux errno.
 */
static int
i915_dp_aux_xfer(
	struct intel_dp *intel_dp,
	const u8 *send,
	int send_bytes,
	u8 *recv,
	int recv_size,
	int hdcp_key)
{
	struct intel_digital_port *dig_port;
	struct drm_i915_private *i915;
	enum phy phy;
	bool is_tc_port;
	bool connected;
	int aux_domain;
	intel_wakeref_t aux_wakeref;
	intel_wakeref_t pps_wakeref;
	int transferred;
	bool vdd;

	/* Finds the port, the device, and whether the port is Type-C. */
	dig_port = i915_dp_dp_to_dig_port(intel_dp);
	i915 = i915_dp_to_i915(dig_port->base.base.dev);
	phy = i915_vbt_intel_port_to_phy(i915, dig_port->base.port);
	is_tc_port = i915_vbt_intel_phy_is_tc(i915, phy);

	/*
	 * Aborts a transfer on a disconnected Type-C port, as DP 1.4a link CTS
	 * 4.2.1.5 requires, which also avoids the long AUX timeouts.
	 */
	if (is_tc_port) {
		intel_tc_port_lock(dig_port);

		connected = intel_tc_port_connected_locked(&dig_port->base);
		if (!connected) {
			intel_tc_port_unlock(dig_port);
			return -I915_DP_ENXIO;
		}
	}

	/* Powers the channel and takes the PPS lock. */
	aux_domain = i915_aux_power_domain(dig_port);

	aux_wakeref = i915_dp_intel_display_power_get(i915, aux_domain);
	pps_wakeref = drv_i915_pps_lock(intel_dp);

	/*
	 * Forces VDD on unless it already is (DPCD, EDID and OUI reads come
	 * with VDD on and leave it to the upper layers to turn it off).
	 */
	vdd = drv_i915_pps_vdd_on_unlocked(intel_dp);

	/* Asks for the lowest wake-up latency: AUX is very sensitive to IRQ latency. */
	cpu_latency_qos_update_request(&intel_dp->pm_qos, 0);

	/* Warns when the panel has neither power nor VDD. */
	drv_i915_pps_check_power_unlocked(intel_dp);

	/* Runs the message on the hardware channel. */
	transferred = i915_dp_aux_exchange(intel_dp, i915, send, send_bytes, recv, recv_size, hdcp_key);

	/* Gives back the latency request, VDD, the lock and the power, in reverse order. */
	cpu_latency_qos_update_request(&intel_dp->pm_qos, PM_QOS_DEFAULT_VALUE);

	if (vdd)
		drv_i915_pps_vdd_off_unlocked(intel_dp, false);

	(void)drv_i915_pps_unlock(intel_dp, pps_wakeref);
	i915_dp_intel_display_power_put_async(i915, aux_domain, aux_wakeref);

	/* Releases the Type-C port. */
	if (is_tc_port)
		intel_tc_port_unlock(dig_port);

	/* Reports a failed message. */
	if (transferred < 0)
		return transferred;

	/* Succeeded: reports the bytes received. */
	return transferred;
}

/* Builds the four-byte AUX request header: request, address and length. */
static void
i915_dp_aux_header(
	u8 txbuf[HEADER_SIZE],
	const struct drm_dp_aux_msg *msg)
{
	/* The request code and the top address bits. */
	txbuf[0] = (msg->request << 4) | ((msg->address >> 16) & 0xf);

	/* The middle and low address bytes. */
	txbuf[1] = (msg->address >> 8) & 0xff;
	txbuf[2] = msg->address & 0xff;

	/* The length, less one. */
	txbuf[3] = msg->size - 1;
}

/*
 * Tells whether a message needs the hardware to send the HDCP Aksv.
 *
 * Writing the HDCP Aksv makes the hardware send the Aksv, which software
 * cannot read, after the header.
 */
static int
i915_dp_aux_hdcp_key(
	const struct drm_dp_aux_msg *msg)
{
	/* A native write to the Aksv address selects the Aksv. */
	if ((msg->request & ~DP_AUX_I2C_MOT) == DP_AUX_NATIVE_WRITE &&
	    msg->address == DP_AUX_HDCP_AKSV)
		return 1;

	/* Succeeded: an ordinary message. */
	return 0;
}

/*
 * Runs one AUX transaction for the DRM helpers (the Linux
 * intel_dp_aux_transfer()).
 *
 * It returns the payload bytes transferred with msg->reply set, or a
 * negative Linux errno.
 */
static i915_dp_ssize_t
i915_dp_aux_transfer(
	struct drm_dp_aux *aux,
	struct drm_dp_aux_msg *msg)
{
	struct intel_dp *intel_dp;
	u8 txbuf[20];
	u8 rxbuf[20];
	size_t txsize;
	size_t rxsize;
	int hdcp_key;
	int ret;
	int too_big;

	/*
	 * Finds the port and whether the message sends the HDCP Aksv.  The
	 * warnings below name no device: the message macros never read it.
	 */
	intel_dp = container_of(aux, struct intel_dp, aux);
	hdcp_key = i915_dp_aux_hdcp_key(msg);

	/* Builds the header. */
	i915_dp_aux_header(txbuf, msg);

	/* Sends a write with its data, or a read, as the request asks. */
	switch (msg->request & ~DP_AUX_I2C_MOT) {
	case DP_AUX_NATIVE_WRITE:
	case DP_AUX_I2C_WRITE:
	case DP_AUX_I2C_WRITE_STATUS_UPDATE:
		/* The header and the data, or a bare address; the reply has 0 or 1 data bytes. */
		if (msg->size != 0) {
			txsize = HEADER_SIZE + msg->size;
		} else {
			txsize = BARE_ADDRESS_SIZE;
		}
		rxsize = 2;

		too_big = I915_DP_DRM_WARN_ON(NULL, txsize > 20);
		if (too_big)
			return -I915_DP_E2BIG;

		(void)I915_DP_DRM_WARN_ON(NULL, !msg->buffer != !msg->size);

		/* Appends the data to the header. */
		if (msg->buffer)
			i915_dp_memcpy(txbuf + HEADER_SIZE, msg->buffer, msg->size);

		/* Sends the write; the reply says how much was taken. */
		ret = i915_dp_aux_xfer(intel_dp, txbuf, txsize, rxbuf, rxsize, hdcp_key);
		if (ret > 0) {
			msg->reply = rxbuf[0] >> 4;

			if (ret > 1) {
				/* The number of bytes written in a short write. */
				ret = I915_DP_CLAMP_T(int, rxbuf[1], 0, msg->size);
			} else {
				/* The whole payload was written. */
				ret = msg->size;
			}
		}
		break;

	case DP_AUX_NATIVE_READ:
	case DP_AUX_I2C_READ:
		/* The header, or a bare address; the reply has the data after one reply byte. */
		if (msg->size != 0) {
			txsize = HEADER_SIZE;
		} else {
			txsize = BARE_ADDRESS_SIZE;
		}
		rxsize = msg->size + 1;

		too_big = I915_DP_DRM_WARN_ON(NULL, rxsize > 20);
		if (too_big)
			return -I915_DP_E2BIG;

		/*
		 * Sends the read and copies the data, assuming a happy day: the
		 * caller checks msg->reply before touching it.
		 */
		ret = i915_dp_aux_xfer(intel_dp, txbuf, txsize, rxbuf, rxsize, hdcp_key);
		if (ret > 0) {
			msg->reply = rxbuf[0] >> 4;

			ret--;
			i915_dp_memcpy(msg->buffer, rxbuf + 1, ret);
		}
		break;

	default:
		ret = -I915_DP_EINVAL;
		break;
	}

	/* Reports a failed transaction. */
	if (ret < 0)
		return ret;

	/* Succeeded: reports the payload bytes. */
	return ret;
}

/* Finds the eDP world of a panel run's panel, for the modeset hooks. */
static struct i915_dp_world *
i915_aux_emit_world(
	void *ctx)
{
	struct i915_lcd_kernel *kernel;
	struct i915_dp_world *world;

	/* The hook context is the panel run; its panel is the display's eDP device. */
	kernel = ctx;
	world = drv_i915_edp_world_of(kernel->d->edp);

	/* Succeeded: reports the world. */
	return world;
}

/*
 * Reports a failed AUX transfer with its values: the channel, what went
 * wrong, the last status, the control register and what it reads now, and
 * the AUX power wells' BIOS and driver requests.  The failure is counted as
 * a DP error; only the first ones, then one of every
 * I915_DP_AUX_REPORTS_EVERY, are logged.
 */
static void
i915_dp_aux_report(
	struct intel_dp *intel_dp,
	struct drm_i915_private *i915,
	i915_reg_t ch_ctl,
	const char *what,
	u32 status)
{
	u32 control;
	u32 wells_bios;
	u32 wells_driver;
	u32 main_wells;
	u32 dc_state;
	int shown;

	/* Counts the error; a level that is not shown ends here. */
	shown = drv_i915_dp_log_enabled(I915_VBT_LOG_ERR);
	if (!shown)
		return;

	/* Only the first failures, then one of every so many, are logged. */
	i915_dp_aux_reports++;
	if (i915_dp_aux_reports > I915_DP_AUX_REPORTS_FIRST && (i915_dp_aux_reports % I915_DP_AUX_REPORTS_EVERY) != 0u)
		return;

	/* The control register now, and the AUX wells' requests and states. */
	control = i915_dp_intel_de_read(i915, ch_ctl);
	wells_bios = i915_dp_intel_de_read(i915, _MMIO(I915_DP_AUX_WELLS_BIOS));
	wells_driver = i915_dp_intel_de_read(i915, _MMIO(I915_DP_AUX_WELLS_DRIVER));

	/* The main wells and the DC state the channel's registers live under. */
	main_wells = i915_dp_intel_de_read(i915, _MMIO(I915_DP_MAIN_WELLS_DRIVER));
	dc_state = i915_dp_intel_de_read(i915, _MMIO(I915_DP_DC_STATE_EN));

	/* One line with every value. */
	kern_logf("i915: aux %s: %s (status 0x%08x; control 0x%05x reads 0x%08x; AUX wells bios 0x%08x driver 0x%08x; main wells driver 0x%08x DC_STATE_EN 0x%08x; report %u)\n",
		  intel_dp->aux.name,
		  what,
		  status,
		  ch_ctl.reg,
		  control,
		  wells_bios,
		  wells_driver,
		  main_wells,
		  dc_state,
		  i915_dp_aux_reports);
}
