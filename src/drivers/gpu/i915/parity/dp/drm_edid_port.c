/*
 * Copyright (c) 2006 Luc Verhaegen (quirks list)
 * Copyright (c) 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 * Copyright 2010 Red Hat, Inc.
 *
 * DDC probing routines (drm_ddc_read & drm_do_probe_ddc_edid) originally from
 * FB layer.
 *   Copyright (C) 2006 Dennis Munsie <dmunsie@cecropia.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/drm_edid.c
 * (sha256 a01138078180d234149ac4403839a99b9c85232955a9830ad5552637cd8661a7) by plan/ws031/handover/tools/port_dp_aux_pps.py.
 * The function bodies are the reference text.  Changes:
 *  - only these parts of the 7386-line file are kept: edid_header[], drm_edid_header_is_valid,
 *    edid_block_compute_checksum, edid_block_get_checksum, DDC_SEGMENT_ADDR and
 *    drm_do_probe_ddc_edid;
 *  - the includes are replaced by dp_compat.h plus the two drm_edid.h constants EDID_LENGTH and
 *    DDC_ADDR; EXPORT_SYMBOL lines are dropped;
 *  - parity_drm_edid_glue.inc (zedBSD code) is included at the end of the file.
 */

#include "dp_compat.h"	/* zedBSD: replaces the linux/ and drm/ includes */
#define EDID_LENGTH 128	/* drm_edid.h */
#define DDC_ADDR 0x50	/* drm_edid.h */

static const u8 edid_header[] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
};

/**
 * drm_edid_header_is_valid - sanity check the header of the base EDID block
 * @_edid: pointer to raw base EDID block
 *
 * Sanity check the header of the base EDID block.
 *
 * Return: 8 if the header is perfect, down to 0 if it's totally wrong.
 */
int drm_edid_header_is_valid(const void *_edid)
{
	const struct edid *edid = _edid;
	int i, score = 0;

	for (i = 0; i < sizeof(edid_header); i++) {
		if (edid->header[i] == edid_header[i])
			score++;
	}

	return score;
}

static int edid_block_compute_checksum(const void *_block)
{
	const u8 *block = _block;
	int i;
	u8 csum = 0, crc = 0;

	for (i = 0; i < EDID_LENGTH - 1; i++)
		csum += block[i];

	crc = 0x100 - csum;

	return crc;
}

static int edid_block_get_checksum(const void *_block)
{
	const struct edid *block = _block;

	return block->checksum;
}

#define DDC_SEGMENT_ADDR 0x30
/**
 * drm_do_probe_ddc_edid() - get EDID information via I2C
 * @data: I2C device adapter
 * @buf: EDID data buffer to be filled
 * @block: 128 byte EDID block to start fetching from
 * @len: EDID data buffer length to fetch
 *
 * Try to fetch EDID information by calling I2C driver functions.
 *
 * Return: 0 on success or -1 on failure.
 */
static int
drm_do_probe_ddc_edid(void *data, u8 *buf, unsigned int block, size_t len)
{
	struct i2c_adapter *adapter = data;
	unsigned char start = block * EDID_LENGTH;
	unsigned char segment = block >> 1;
	unsigned char xfers = segment ? 3 : 2;
	int ret, retries = 5;

	/*
	 * The core I2C driver will automatically retry the transfer if the
	 * adapter reports EAGAIN. However, we find that bit-banging transfers
	 * are susceptible to errors under a heavily loaded machine and
	 * generate spurious NAKs and timeouts. Retrying the transfer
	 * of the individual block a few times seems to overcome this.
	 */
	do {
		struct i2c_msg msgs[] = {
			{
				.addr	= DDC_SEGMENT_ADDR,
				.flags	= 0,
				.len	= 1,
				.buf	= &segment,
			}, {
				.addr	= DDC_ADDR,
				.flags	= 0,
				.len	= 1,
				.buf	= &start,
			}, {
				.addr	= DDC_ADDR,
				.flags	= I2C_M_RD,
				.len	= len,
				.buf	= buf,
			}
		};

		/*
		 * Avoid sending the segment addr to not upset non-compliant
		 * DDC monitors.
		 */
		ret = i2c_transfer(adapter, &msgs[3 - xfers], xfers);

		if (ret == -ENXIO) {
			DRM_DEBUG_KMS("drm: skipping non-existent adapter %s\n",
					adapter->name);
			break;
		}
	} while (ret != xfers && --retries);

	return ret == xfers ? 0 : -1;
}

#include "parity_drm_edid_glue.inc"	/* zedBSD: base + extension block read */
