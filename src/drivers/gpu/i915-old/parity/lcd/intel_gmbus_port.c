/*
 * Copyright (c) 2006 Dave Airlie <airlied@linux.ie>
 * Copyright © 2006-2008,2010 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
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
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 *	Chris Wilson <chris@chris-wilson.co.uk>
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_gmbus.c
 * (sha256 324e6967f70a95046325a81c45f6cd038af59aae1ee9d7202f025622af7f73f0) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: to_intel_gmbus, intel_gmbus_reset, has_gmbus_irq, gmbus_wait,
 *    gmbus_wait_idle, gmbus_max_xfer_size, gmbus_xfer_read_chunk, gmbus_xfer_read,
 *    gmbus_xfer_write_chunk, gmbus_xfer_write, gmbus_is_index_xfer, gmbus_index_xfer,
 *    do_gmbus_xfer, gmbus_xfer, intel_gmbus_force_bit, intel_gmbus_is_forced_bit,
 *    intel_gmbus_irq_handler;
 *  - the includes are replaced by: hpd_compat.h;
 *  - parity_gmbus_glue.inc (zedBSD code) is included at the end of the file.
 */

#include "hpd_compat.h"

struct intel_gmbus {
	struct i2c_adapter adapter;
#define GMBUS_FORCE_BIT_RETRY (1U << 31)
	u32 force_bit;
	u32 reg0;
	i915_reg_t gpio_reg;
	struct i2c_algo_bit_data bit_algo;
	struct drm_i915_private *i915;
};

#define INTEL_GMBUS_BURST_READ_MAX_LEN		767U

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static bool has_gmbus_irq(struct drm_i915_private *i915);
static int gmbus_wait(struct drm_i915_private *i915, u32 status, u32 irq_en);
static int
gmbus_wait_idle(struct drm_i915_private *i915);
static unsigned int gmbus_max_xfer_size(struct drm_i915_private *i915);
static int
gmbus_xfer_read_chunk(struct drm_i915_private *i915,
		      unsigned short addr, u8 *buf, unsigned int len,
		      u32 gmbus0_reg, u32 gmbus1_index);
static int
gmbus_xfer_read(struct drm_i915_private *i915, struct i2c_msg *msg,
		u32 gmbus0_reg, u32 gmbus1_index);
static int
gmbus_xfer_write_chunk(struct drm_i915_private *i915,
		       unsigned short addr, u8 *buf, unsigned int len,
		       u32 gmbus1_index);
static int
gmbus_xfer_write(struct drm_i915_private *i915, struct i2c_msg *msg,
		 u32 gmbus1_index);
static bool
gmbus_is_index_xfer(struct i2c_msg *msgs, int i, int num);
static int
gmbus_index_xfer(struct drm_i915_private *i915, struct i2c_msg *msgs,
		 u32 gmbus0_reg);
static int
do_gmbus_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num,
	      u32 gmbus0_source);
static int
gmbus_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num);

static inline struct intel_gmbus *
to_intel_gmbus(struct i2c_adapter *i2c)
{
	return container_of(i2c, struct intel_gmbus, adapter);
}

void
intel_gmbus_reset(struct drm_i915_private *i915)
{
	intel_de_write(i915, GMBUS0(i915), 0);
	intel_de_write(i915, GMBUS4(i915), 0);
}

static bool has_gmbus_irq(struct drm_i915_private *i915)
{
	/*
	 * encoder->shutdown() may want to use GMBUS
	 * after irqs have already been disabled.
	 */
	return HAS_GMBUS_IRQ(i915) && intel_irqs_enabled(i915);
}

static int gmbus_wait(struct drm_i915_private *i915, u32 status, u32 irq_en)
{
	DEFINE_WAIT(wait);
	u32 gmbus2;
	int ret;

	/* Important: The hw handles only the first bit, so set only one! Since
	 * we also need to check for NAKs besides the hw ready/idle signal, we
	 * need to wake up periodically and check that ourselves.
	 */
	if (!has_gmbus_irq(i915))
		irq_en = 0;

	add_wait_queue(&i915->display.gmbus.wait_queue, &wait);
	intel_de_write_fw(i915, GMBUS4(i915), irq_en);

	status |= GMBUS_SATOER;
	ret = wait_for_us((gmbus2 = intel_de_read_fw(i915, GMBUS2(i915))) & status,
			  2);
	if (ret)
		ret = wait_for((gmbus2 = intel_de_read_fw(i915, GMBUS2(i915))) & status,
			       50);

	intel_de_write_fw(i915, GMBUS4(i915), 0);
	remove_wait_queue(&i915->display.gmbus.wait_queue, &wait);

	if (gmbus2 & GMBUS_SATOER)
		return -ENXIO;

	return ret;
}

static int
gmbus_wait_idle(struct drm_i915_private *i915)
{
	DEFINE_WAIT(wait);
	u32 irq_enable;
	int ret;

	/* Important: The hw handles only the first bit, so set only one! */
	irq_enable = 0;
	if (has_gmbus_irq(i915))
		irq_enable = GMBUS_IDLE_EN;

	add_wait_queue(&i915->display.gmbus.wait_queue, &wait);
	intel_de_write_fw(i915, GMBUS4(i915), irq_enable);

	ret = intel_de_wait_for_register_fw(i915, GMBUS2(i915), GMBUS_ACTIVE, 0, 10);

	intel_de_write_fw(i915, GMBUS4(i915), 0);
	remove_wait_queue(&i915->display.gmbus.wait_queue, &wait);

	return ret;
}

static unsigned int gmbus_max_xfer_size(struct drm_i915_private *i915)
{
	return DISPLAY_VER(i915) >= 9 ? GEN9_GMBUS_BYTE_COUNT_MAX :
	       GMBUS_BYTE_COUNT_MAX;
}

static int
gmbus_xfer_read_chunk(struct drm_i915_private *i915,
		      unsigned short addr, u8 *buf, unsigned int len,
		      u32 gmbus0_reg, u32 gmbus1_index)
{
	unsigned int size = len;
	bool burst_read = len > gmbus_max_xfer_size(i915);
	bool extra_byte_added = false;

	if (burst_read) {
		/*
		 * As per HW Spec, for 512Bytes need to read extra Byte and
		 * Ignore the extra byte read.
		 */
		if (len == 512) {
			extra_byte_added = true;
			len++;
		}
		size = len % 256 + 256;
		intel_de_write_fw(i915, GMBUS0(i915),
				  gmbus0_reg | GMBUS_BYTE_CNT_OVERRIDE);
	}

	intel_de_write_fw(i915, GMBUS1(i915),
			  gmbus1_index | GMBUS_CYCLE_WAIT | (size << GMBUS_BYTE_COUNT_SHIFT) | (addr << GMBUS_SLAVE_ADDR_SHIFT) | GMBUS_SLAVE_READ | GMBUS_SW_RDY);
	while (len) {
		int ret;
		u32 val, loop = 0;

		ret = gmbus_wait(i915, GMBUS_HW_RDY, GMBUS_HW_RDY_EN);
		if (ret)
			return ret;

		val = intel_de_read_fw(i915, GMBUS3(i915));
		do {
			if (extra_byte_added && len == 1) {
				len--;
				break;
			}

			*buf++ = val & 0xff;
			val >>= 8;
		} while (--len && ++loop < 4);

		if (burst_read && len == size - 4)
			/* Reset the override bit */
			intel_de_write_fw(i915, GMBUS0(i915), gmbus0_reg);
	}

	return 0;
}

static int
gmbus_xfer_read(struct drm_i915_private *i915, struct i2c_msg *msg,
		u32 gmbus0_reg, u32 gmbus1_index)
{
	u8 *buf = msg->buf;
	unsigned int rx_size = msg->len;
	unsigned int len;
	int ret;

	do {
		if (HAS_GMBUS_BURST_READ(i915))
			len = min(rx_size, INTEL_GMBUS_BURST_READ_MAX_LEN);
		else
			len = min(rx_size, gmbus_max_xfer_size(i915));

		ret = gmbus_xfer_read_chunk(i915, msg->addr, buf, len,
					    gmbus0_reg, gmbus1_index);
		if (ret)
			return ret;

		rx_size -= len;
		buf += len;
	} while (rx_size != 0);

	return 0;
}

static int
gmbus_xfer_write_chunk(struct drm_i915_private *i915,
		       unsigned short addr, u8 *buf, unsigned int len,
		       u32 gmbus1_index)
{
	unsigned int chunk_size = len;
	u32 val, loop;

	val = loop = 0;
	while (len && loop < 4) {
		val |= *buf++ << (8 * loop++);
		len -= 1;
	}

	intel_de_write_fw(i915, GMBUS3(i915), val);
	intel_de_write_fw(i915, GMBUS1(i915),
			  gmbus1_index | GMBUS_CYCLE_WAIT | (chunk_size << GMBUS_BYTE_COUNT_SHIFT) | (addr << GMBUS_SLAVE_ADDR_SHIFT) | GMBUS_SLAVE_WRITE | GMBUS_SW_RDY);
	while (len) {
		int ret;

		val = loop = 0;
		do {
			val |= *buf++ << (8 * loop);
		} while (--len && ++loop < 4);

		intel_de_write_fw(i915, GMBUS3(i915), val);

		ret = gmbus_wait(i915, GMBUS_HW_RDY, GMBUS_HW_RDY_EN);
		if (ret)
			return ret;
	}

	return 0;
}

static int
gmbus_xfer_write(struct drm_i915_private *i915, struct i2c_msg *msg,
		 u32 gmbus1_index)
{
	u8 *buf = msg->buf;
	unsigned int tx_size = msg->len;
	unsigned int len;
	int ret;

	do {
		len = min(tx_size, gmbus_max_xfer_size(i915));

		ret = gmbus_xfer_write_chunk(i915, msg->addr, buf, len,
					     gmbus1_index);
		if (ret)
			return ret;

		buf += len;
		tx_size -= len;
	} while (tx_size != 0);

	return 0;
}

/*
 * The gmbus controller can combine a 1 or 2 byte write with another read/write
 * that immediately follows it by using an "INDEX" cycle.
 */
static bool
gmbus_is_index_xfer(struct i2c_msg *msgs, int i, int num)
{
	return (i + 1 < num &&
		msgs[i].addr == msgs[i + 1].addr &&
		!(msgs[i].flags & I2C_M_RD) &&
		(msgs[i].len == 1 || msgs[i].len == 2) &&
		msgs[i + 1].len > 0);
}

static int
gmbus_index_xfer(struct drm_i915_private *i915, struct i2c_msg *msgs,
		 u32 gmbus0_reg)
{
	u32 gmbus1_index = 0;
	u32 gmbus5 = 0;
	int ret;

	if (msgs[0].len == 2)
		gmbus5 = GMBUS_2BYTE_INDEX_EN |
			 msgs[0].buf[1] | (msgs[0].buf[0] << 8);
	if (msgs[0].len == 1)
		gmbus1_index = GMBUS_CYCLE_INDEX |
			       (msgs[0].buf[0] << GMBUS_SLAVE_INDEX_SHIFT);

	/* GMBUS5 holds 16-bit index */
	if (gmbus5)
		intel_de_write_fw(i915, GMBUS5(i915), gmbus5);

	if (msgs[1].flags & I2C_M_RD)
		ret = gmbus_xfer_read(i915, &msgs[1], gmbus0_reg,
				      gmbus1_index);
	else
		ret = gmbus_xfer_write(i915, &msgs[1], gmbus1_index);

	/* Clear GMBUS5 after each index transfer */
	if (gmbus5)
		intel_de_write_fw(i915, GMBUS5(i915), 0);

	return ret;
}

static int
do_gmbus_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num,
	      u32 gmbus0_source)
{
	struct intel_gmbus *bus = to_intel_gmbus(adapter);
	struct drm_i915_private *i915 = bus->i915;
	int i = 0, inc, try = 0;
	int ret = 0;

	/* Display WA #0868: skl,bxt,kbl,cfl,glk */
	if (IS_GEMINILAKE(i915) || IS_BROXTON(i915))
		bxt_gmbus_clock_gating(i915, false);
	else if (HAS_PCH_SPT(i915) || HAS_PCH_CNP(i915))
		pch_gmbus_clock_gating(i915, false);

retry:
	intel_de_write_fw(i915, GMBUS0(i915), gmbus0_source | bus->reg0);

	for (; i < num; i += inc) {
		inc = 1;
		if (gmbus_is_index_xfer(msgs, i, num)) {
			ret = gmbus_index_xfer(i915, &msgs[i],
					       gmbus0_source | bus->reg0);
			inc = 2; /* an index transmission is two msgs */
		} else if (msgs[i].flags & I2C_M_RD) {
			ret = gmbus_xfer_read(i915, &msgs[i],
					      gmbus0_source | bus->reg0, 0);
		} else {
			ret = gmbus_xfer_write(i915, &msgs[i], 0);
		}

		if (!ret)
			ret = gmbus_wait(i915,
					 GMBUS_HW_WAIT_PHASE, GMBUS_HW_WAIT_EN);
		if (ret == -ETIMEDOUT)
			goto timeout;
		else if (ret)
			goto clear_err;
	}

	/* Generate a STOP condition on the bus. Note that gmbus can't generata
	 * a STOP on the very first cycle. To simplify the code we
	 * unconditionally generate the STOP condition with an additional gmbus
	 * cycle. */
	intel_de_write_fw(i915, GMBUS1(i915), GMBUS_CYCLE_STOP | GMBUS_SW_RDY);

	/* Mark the GMBUS interface as disabled after waiting for idle.
	 * We will re-enable it at the start of the next xfer,
	 * till then let it sleep.
	 */
	if (gmbus_wait_idle(i915)) {
		drm_dbg_kms(&i915->drm,
			    "GMBUS [%s] timed out waiting for idle\n",
			    adapter->name);
		ret = -ETIMEDOUT;
	}
	intel_de_write_fw(i915, GMBUS0(i915), 0);
	ret = ret ?: i;
	goto out;

clear_err:
	/*
	 * Wait for bus to IDLE before clearing NAK.
	 * If we clear the NAK while bus is still active, then it will stay
	 * active and the next transaction may fail.
	 *
	 * If no ACK is received during the address phase of a transaction, the
	 * adapter must report -ENXIO. It is not clear what to return if no ACK
	 * is received at other times. But we have to be careful to not return
	 * spurious -ENXIO because that will prevent i2c and drm edid functions
	 * from retrying. So return -ENXIO only when gmbus properly quiescents -
	 * timing out seems to happen when there _is_ a ddc chip present, but
	 * it's slow responding and only answers on the 2nd retry.
	 */
	ret = -ENXIO;
	if (gmbus_wait_idle(i915)) {
		drm_dbg_kms(&i915->drm,
			    "GMBUS [%s] timed out after NAK\n",
			    adapter->name);
		ret = -ETIMEDOUT;
	}

	/* Toggle the Software Clear Interrupt bit. This has the effect
	 * of resetting the GMBUS controller and so clearing the
	 * BUS_ERROR raised by the slave's NAK.
	 */
	intel_de_write_fw(i915, GMBUS1(i915), GMBUS_SW_CLR_INT);
	intel_de_write_fw(i915, GMBUS1(i915), 0);
	intel_de_write_fw(i915, GMBUS0(i915), 0);

	drm_dbg_kms(&i915->drm, "GMBUS [%s] NAK for addr: %04x %c(%d)\n",
		    adapter->name, msgs[i].addr,
		    (msgs[i].flags & I2C_M_RD) ? 'r' : 'w', msgs[i].len);

	/*
	 * Passive adapters sometimes NAK the first probe. Retry the first
	 * message once on -ENXIO for GMBUS transfers; the bit banging algorithm
	 * has retries internally. See also the retry loop in
	 * drm_do_probe_ddc_edid, which bails out on the first -ENXIO.
	 */
	if (ret == -ENXIO && i == 0 && try++ == 0) {
		drm_dbg_kms(&i915->drm,
			    "GMBUS [%s] NAK on first message, retry\n",
			    adapter->name);
		goto retry;
	}

	goto out;

timeout:
	drm_dbg_kms(&i915->drm,
		    "GMBUS [%s] timed out, falling back to bit banging on pin %d\n",
		    bus->adapter.name, bus->reg0 & 0xff);
	intel_de_write_fw(i915, GMBUS0(i915), 0);

	/*
	 * Hardware may not support GMBUS over these pins? Try GPIO bitbanging
	 * instead. Use EAGAIN to have i2c core retry.
	 */
	ret = -EAGAIN;

out:
	/* Display WA #0868: skl,bxt,kbl,cfl,glk */
	if (IS_GEMINILAKE(i915) || IS_BROXTON(i915))
		bxt_gmbus_clock_gating(i915, true);
	else if (HAS_PCH_SPT(i915) || HAS_PCH_CNP(i915))
		pch_gmbus_clock_gating(i915, true);

	return ret;
}

static int
gmbus_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num)
{
	struct intel_gmbus *bus = to_intel_gmbus(adapter);
	struct drm_i915_private *i915 = bus->i915;
	intel_wakeref_t wakeref;
	int ret;

	wakeref = intel_display_power_get(i915, POWER_DOMAIN_GMBUS);

	if (bus->force_bit) {
		ret = i2c_bit_algo.master_xfer(adapter, msgs, num);
		if (ret < 0)
			bus->force_bit &= ~GMBUS_FORCE_BIT_RETRY;
	} else {
		ret = do_gmbus_xfer(adapter, msgs, num, 0);
		if (ret == -EAGAIN)
			bus->force_bit |= GMBUS_FORCE_BIT_RETRY;
	}

	intel_display_power_put(i915, POWER_DOMAIN_GMBUS, wakeref);

	return ret;
}

void intel_gmbus_force_bit(struct i2c_adapter *adapter, bool force_bit)
{
	struct intel_gmbus *bus = to_intel_gmbus(adapter);
	struct drm_i915_private *i915 = bus->i915;

	mutex_lock(&i915->display.gmbus.mutex);

	bus->force_bit += force_bit ? 1 : -1;
	drm_dbg_kms(&i915->drm,
		    "%sabling bit-banging on %s. force bit now %d\n",
		    force_bit ? "en" : "dis", adapter->name,
		    bus->force_bit);

	mutex_unlock(&i915->display.gmbus.mutex);
}

bool intel_gmbus_is_forced_bit(struct i2c_adapter *adapter)
{
	struct intel_gmbus *bus = to_intel_gmbus(adapter);

	return bus->force_bit;
}

void intel_gmbus_irq_handler(struct drm_i915_private *i915)
{
	wake_up_all(&i915->display.gmbus.wait_queue);
}


#include "parity_gmbus_glue.inc"
