/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The CDC NCM codec's transmit block of several datagrams
 * (drv_usb_cdc_ncm_build_ntb16_frames): for several OUT alignments it packs
 * frames of mixed lengths up to the block size and the datagram count,
 * the codec's own parser reads back each frame whole and in order, the
 * block length keeps the zero-length-packet rule, and a first frame that
 * cannot fit is refused.  Built and run on the host by
 * run-ncm-ntb-frames-test.sh, with src/drivers/usb/usb-cdc-ncm.c.
 */

#include <drivers/usb/usb-cdc-ncm.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAMES_MAX 40U
#define BLOCK_MAX 65536U

static unsigned failures;

#define CHECK(condition)                                                     \
	do {                                                                 \
		if (!(condition)) {                                          \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__,       \
			       #condition);                                  \
			failures++;                                          \
		}                                                            \
	} while (0)

static uint8_t block[BLOCK_MAX];
static uint8_t storage[FRAMES_MAX][2048];

struct delivered {
	size_t count;
	size_t lengths[DRV_USB_CDC_NCM_MAX_RX_DATAGRAMS];
	int content_ok;
};

static void
put_le16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

static void
put_le32(uint8_t *bytes, uint32_t value)
{
	put_le16(bytes, (uint16_t)value);
	put_le16(bytes + 2U, (uint16_t)(value >> 16));
}

/*
 * Negotiates a profile whose IN and OUT sides are the same (so that the
 * parser reads a block the builder made under the OUT rules).
 */
static int
make_profile(uint32_t block_max, uint16_t divisor, uint16_t remainder, uint16_t alignment, uint16_t tx_datagrams,
	     struct drv_usb_cdc_ncm_profile *profile)
{
	uint8_t parameters[DRV_USB_CDC_NCM_NTB_PARAMETERS_SIZE];
	struct drv_usb_cdc_ncm_limits limits;

	memset(parameters, 0, sizeof(parameters));
	put_le16(parameters, DRV_USB_CDC_NCM_NTB_PARAMETERS_SIZE);
	put_le16(parameters + 2U, DRV_USB_CDC_NCM_NTB16_SUPPORTED);
	put_le32(parameters + 4U, block_max);
	put_le16(parameters + 8U, divisor);
	put_le16(parameters + 10U, remainder);
	put_le16(parameters + 12U, alignment);
	put_le32(parameters + 16U, block_max);
	put_le16(parameters + 20U, divisor);
	put_le16(parameters + 22U, remainder);
	put_le16(parameters + 24U, alignment);
	put_le16(parameters + 26U, tx_datagrams);
	memset(&limits, 0, sizeof(limits));
	limits.ntb_in_max_size = block_max;
	limits.ntb_out_max_size = block_max;
	limits.rx_max_datagrams = DRV_USB_CDC_NCM_MAX_RX_DATAGRAMS;
	limits.tx_max_datagrams = DRV_USB_CDC_NCM_MAX_TX_DATAGRAMS;
	limits.ndp_chain_max = DRV_USB_CDC_NCM_MAX_NDP_CHAIN;
	limits.bulk_out_max_packet_size = 512U;
	return drv_usb_cdc_ncm_negotiate_nth16(parameters, sizeof(parameters), &limits, profile);
}

static int
deliver(const void *data, size_t length, void *argument)
{
	struct delivered *log = argument;
	const uint8_t *bytes = data;
	size_t index;

	if (log->count >= DRV_USB_CDC_NCM_MAX_RX_DATAGRAMS)
		return ENOBUFS;
	for (index = 0; index < length; index++) {
		if (bytes[index] != (uint8_t)(index * 5U + log->count * 11U))
			log->content_ok = 0;
	}
	log->lengths[log->count++] = length;
	return 0;
}

/* Packs count frames (lengths from the pattern), parses the block, and checks it. */
static void
test_pack(uint32_t block_max, uint16_t divisor, uint16_t remainder, uint16_t alignment, uint16_t tx_datagrams,
	  size_t count, size_t length_step)
{
	struct drv_usb_cdc_ncm_profile profile;
	struct drv_usb_cdc_ncm_rx_state state;
	struct delivered log;
	const void *frames[FRAMES_MAX];
	size_t lengths[FRAMES_MAX];
	size_t ntb_length;
	size_t taken;
	size_t parsed;
	size_t index;
	size_t byte;
	int error;

	error = make_profile(block_max, divisor, remainder, alignment, tx_datagrams, &profile);
	CHECK(error == 0);
	if (error != 0)
		return;

	/* Frames of lengths from 60 up, by length_step, no longer than the profile's datagram. */
	for (index = 0; index < count; index++) {
		lengths[index] = 60U + (index * length_step) % (profile.max_datagram_size - 59U);
		for (byte = 0; byte < lengths[index]; byte++)
			storage[index][byte] = (uint8_t)(byte * 5U + index * 11U);
		frames[index] = storage[index];
	}

	/* The block. */
	memset(block, 0xa5, sizeof(block));
	error = drv_usb_cdc_ncm_build_ntb16_frames(&profile, 7U, frames, lengths, count, block, sizeof(block),
						  &ntb_length, &taken);
	CHECK(error == 0);
	if (error != 0)
		return;
	CHECK(taken >= 1U && taken <= count && taken <= profile.tx_max_datagrams);
	CHECK(ntb_length <= profile.ntb_out_max_size);
	CHECK(ntb_length % 512U != 0 || ntb_length == profile.ntb_out_max_size);

	/* Read back by the codec's own parser: every frame carried, whole and in order. */
	memset(&log, 0, sizeof(log));
	log.content_ok = 1;
	drv_usb_cdc_ncm_rx_reset(&state);
	parsed = 0;
	error = drv_usb_cdc_ncm_parse_ntb16(&profile, &state, block, ntb_length, deliver, &log, &parsed);
	CHECK(error == 0);
	CHECK(parsed == taken && log.count == taken);
	CHECK(log.content_ok);
	for (index = 0; index < log.count && index < taken; index++)
		CHECK(log.lengths[index] == lengths[index]);

	/* One more frame would not have fitted, unless the count or the datagram limit stopped it. */
	if (taken < count && taken < profile.tx_max_datagrams) {
		size_t more_length;
		size_t more_taken;

		error = drv_usb_cdc_ncm_build_ntb16_frames(&profile, 8U, frames, lengths, taken + 1U, block,
							  sizeof(block), &more_length, &more_taken);
		CHECK(error == 0 && more_taken == taken);
	}
	printf("pack: block %u divisor %u remainder %u alignment %u datagrams %u: %zu of %zu frames, %zu bytes\n",
	       block_max, divisor, remainder, alignment, tx_datagrams, taken, count, ntb_length);
}

/* A first frame that does not fit the buffer is refused, and a frame longer than the datagram maximum too. */
static void
test_refusals(void)
{
	struct drv_usb_cdc_ncm_profile profile;
	const void *frames[1];
	size_t lengths[1];
	size_t ntb_length;
	size_t taken;
	int error;

	error = make_profile(16384U, 4U, 0U, 4U, 32U, &profile);
	CHECK(error == 0);
	frames[0] = storage[0];
	lengths[0] = 1514U;
	error = drv_usb_cdc_ncm_build_ntb16_frames(&profile, 1U, frames, lengths, 1U, block, 1024U, &ntb_length, &taken);
	CHECK(error == EMSGSIZE && taken == 0);
	lengths[0] = (size_t)profile.max_datagram_size + 1U;
	error = drv_usb_cdc_ncm_build_ntb16_frames(&profile, 1U, frames, lengths, 1U, block, sizeof(block), &ntb_length,
						  &taken);
	CHECK(error == EINVAL);
	lengths[0] = 10U;
	error = drv_usb_cdc_ncm_build_ntb16_frames(&profile, 1U, frames, lengths, 1U, block, sizeof(block), &ntb_length,
						  &taken);
	CHECK(error == EINVAL);
}

int
main(void)
{
	test_pack(16384U, 4U, 0U, 4U, 32U, 40U, 1454U);
	test_pack(16384U, 4U, 0U, 4U, 32U, 40U, 97U);
	test_pack(16384U, 4U, 0U, 4U, 3U, 40U, 97U);
	test_pack(16384U, 8U, 2U, 8U, 32U, 40U, 333U);
	test_pack(32768U, 4U, 0U, 4U, 32U, 40U, 1454U);
	test_pack(2048U, 4U, 0U, 4U, 32U, 40U, 97U);
	test_pack(16384U, 4U, 0U, 4U, 32U, 1U, 1454U);
	test_refusals();
	if (failures != 0) {
		printf("ncm-ntb-frames: FAIL (%u)\n", failures);
		return 1;
	}
	printf("ncm-ntb-frames: PASS\n");
	return 0;
}
