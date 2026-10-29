/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The CDC NCM codec's maximum datagram above the standard 1514: the
 * longest the limits ask for that fits both blocks, three jumbo datagrams
 * in one receive block, refusal past the maximum, and the standard size
 * when the limits ask for none larger.  Built and run on the host by
 * run-ncm-jumbo-wire-test.sh, with src/drivers/usb/usb-cdc-ncm.c.
 */

#include <drivers/usb/usb-cdc-ncm.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JUMBO_DATAGRAM 9014U
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

struct device_parameters {
	uint32_t in_max;
	uint32_t out_max;
	uint16_t divisor;
	uint16_t remainder;
	uint16_t alignment;
};

struct delivered {
	size_t count;
	size_t lengths[DRV_USB_CDC_NCM_MAX_RX_DATAGRAMS];
	int content_ok;
};

static uint8_t block[BLOCK_MAX];
static uint8_t frame[BLOCK_MAX];

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

/* GET_NTB_PARAMETERS as a device with these limits answers it. */
static void
make_parameters(const struct device_parameters *device, uint8_t *parameters)
{
	memset(parameters, 0, DRV_USB_CDC_NCM_NTB_PARAMETERS_SIZE);
	put_le16(parameters, DRV_USB_CDC_NCM_NTB_PARAMETERS_SIZE);
	put_le16(parameters + 2U, DRV_USB_CDC_NCM_NTB16_SUPPORTED);
	put_le32(parameters + 4U, device->in_max);
	put_le16(parameters + 8U, device->divisor);
	put_le16(parameters + 10U, device->remainder);
	put_le16(parameters + 12U, device->alignment);
	put_le32(parameters + 16U, device->out_max);
	put_le16(parameters + 20U, device->divisor);
	put_le16(parameters + 22U, device->remainder);
	put_le16(parameters + 24U, device->alignment);
	put_le16(parameters + 26U, 1U);
}

/* The driver's limits for jumbo blocks, asking for datagrams up to maximum. */
static struct drv_usb_cdc_ncm_limits
make_limits(uint32_t in_max, uint32_t out_max, uint16_t maximum)
{
	struct drv_usb_cdc_ncm_limits limits;

	/* The limits start from zero. */
	memset(&limits, 0, sizeof(limits));
	limits.ntb_in_max_size = in_max;
	limits.ntb_out_max_size = out_max;
	limits.rx_max_datagrams = DRV_USB_CDC_NCM_MAX_RX_DATAGRAMS;
	limits.tx_max_datagrams = 1U;
	limits.ndp_chain_max = DRV_USB_CDC_NCM_MAX_NDP_CHAIN;
	limits.bulk_out_max_packet_size = 512U;
	limits.max_datagram_size = maximum;
	return limits;
}

/* Negotiates; the profile's maximum datagram, or 0 on an error (in *error). */
static uint16_t
negotiate(const struct device_parameters *device, uint32_t in_max, uint32_t out_max, uint16_t maximum, int *error,
	  struct drv_usb_cdc_ncm_profile *profile)
{
	uint8_t parameters[DRV_USB_CDC_NCM_NTB_PARAMETERS_SIZE];
	struct drv_usb_cdc_ncm_limits limits;
	struct drv_usb_cdc_ncm_profile local;

	/* The parameters the device reports, and the limits the driver asks within. */
	make_parameters(device, parameters);
	limits = make_limits(in_max, out_max, maximum);
	if (profile == NULL)
		profile = &local;

	/* The negotiation. */
	*error = drv_usb_cdc_ncm_negotiate_nth16(parameters, sizeof(parameters), &limits, profile);
	if (*error != 0)
		return 0;

	/* Succeeded: the datagram size the profile settled on. */
	return profile->max_datagram_size;
}

/*
 * (a) For every limit from 1514 to the jumbo datagram, the profile's
 * maximum is no more than the limit, never falls as the limit grows, and
 * at the jumbo limit is the longest that a limit of its own size keeps.
 */
static void
test_derivation(const struct device_parameters *device, uint32_t in_max, uint32_t out_max, uint16_t expect_low,
		uint16_t expect_high)
{
	uint16_t previous;
	uint16_t result;
	uint16_t longest_kept;
	unsigned limit;
	int error;

	previous = 0;
	longest_kept = 0;
	for (limit = DRV_USB_CDC_NCM_MAX_DATAGRAM_SIZE; limit <= JUMBO_DATAGRAM; limit++) {
		result = negotiate(device, in_max, out_max, (uint16_t)limit, &error, NULL);
		if (error != 0 || result > limit || result < previous) {
			printf("FAIL derivation: device in %u out %u divisor %u remainder %u, limit %u: error %d result %u "
			       "previous %u\n",
			       device->in_max, device->out_max, device->divisor, device->remainder, limit, error, result,
			       previous);
			failures++;
			return;
		}
		if (result == limit)
			longest_kept = result;
		previous = result;
	}
	CHECK(previous == longest_kept);
	CHECK(previous >= expect_low && previous <= expect_high);
	printf("derivation: device in %u out %u divisor %u remainder %u alignment %u, driver in %u out %u: %u\n",
	       device->in_max, device->out_max, device->divisor, device->remainder, device->alignment, in_max, out_max,
	       previous);
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
		if (bytes[index] != (uint8_t)(index * 7U + log->count))
			log->content_ok = 0;
	}
	log->lengths[log->count++] = length;
	return 0;
}

/* An NTH16 block with count datagrams of length bytes each, aligned for divisor 4 remainder 0, and its NDP16 after them. */
static size_t
make_block(unsigned count, size_t length)
{
	size_t offsets[8];
	size_t place;
	size_t ndp;
	size_t ndp_length;
	unsigned index;
	size_t byte;

	memset(block, 0, sizeof(block));
	place = DRV_USB_CDC_NCM_NTH16_SIZE;
	for (index = 0; index < count; index++) {
		/* The payload after the Ethernet header on a 4-byte boundary (divisor 4, remainder 0). */
		while ((place + DRV_USB_CDC_NCM_ETHERNET_HEADER_SIZE) % 4U != 0)
			place++;
		offsets[index] = place;
		for (byte = 0; byte < length; byte++)
			block[place + byte] = (uint8_t)(byte * 7U + index);
		place += length;
	}
	ndp = (place + 3U) & ~(size_t)3U;
	ndp_length = DRV_USB_CDC_NCM_NDP16_HEADER_SIZE + (count + 1U) * DRV_USB_CDC_NCM_DPE16_SIZE;
	put_le32(block + ndp, DRV_USB_CDC_NCM_NDP16_NOCRC_SIGNATURE);
	put_le16(block + ndp + 4U, (uint16_t)ndp_length);
	for (index = 0; index < count; index++) {
		put_le16(block + ndp + 8U + index * 4U, (uint16_t)offsets[index]);
		put_le16(block + ndp + 10U + index * 4U, (uint16_t)length);
	}
	place = ndp + ndp_length;
	put_le32(block, DRV_USB_CDC_NCM_NTH16_SIGNATURE);
	put_le16(block + 4U, DRV_USB_CDC_NCM_NTH16_SIZE);
	put_le16(block + 6U, 0);
	put_le16(block + 8U, (uint16_t)place);
	put_le16(block + 10U, (uint16_t)ndp);
	return place;
}

/* (b) three jumbo datagrams in one 32 KB receive block; (c) one past the maximum refused. */
static void
test_jumbo_blocks(void)
{
	static const struct device_parameters device = {65536U, 65536U, 4U, 0, 4U};
	struct drv_usb_cdc_ncm_profile profile;
	struct drv_usb_cdc_ncm_rx_state state;
	struct delivered log;
	size_t length;
	size_t count;
	size_t built;
	int error;

	CHECK(negotiate(&device, 32768U, 16384U, JUMBO_DATAGRAM, &error, &profile) == JUMBO_DATAGRAM);
	CHECK(error == 0);

	/* Three of 9014 bytes in one block, each delivered whole. */
	length = make_block(3U, JUMBO_DATAGRAM);
	CHECK(length <= profile.ntb_in_max_size);
	memset(&log, 0, sizeof(log));
	log.content_ok = 1;
	drv_usb_cdc_ncm_rx_reset(&state);
	count = 0;
	error = drv_usb_cdc_ncm_parse_ntb16(&profile, &state, block, length, deliver, &log, &count);
	CHECK(error == 0);
	CHECK(log.count == 3U && count == 3U);
	CHECK(log.lengths[0] == JUMBO_DATAGRAM && log.lengths[2] == JUMBO_DATAGRAM);
	CHECK(log.content_ok);

	/* One of 9015 bytes: the block is refused and nothing delivered. */
	length = make_block(1U, JUMBO_DATAGRAM + 1U);
	memset(&log, 0, sizeof(log));
	drv_usb_cdc_ncm_rx_reset(&state);
	error = drv_usb_cdc_ncm_parse_ntb16(&profile, &state, block, length, deliver, &log, &count);
	CHECK(error != 0);
	CHECK(log.count == 0);

	/* A 9014-byte frame builds into the 16 KB transmit block, a 9015-byte one does not. */
	memset(frame, 0x5a, sizeof(frame));
	built = 0;
	CHECK(drv_usb_cdc_ncm_build_ntb16(&profile, 1U, frame, JUMBO_DATAGRAM, block, profile.ntb_out_max_size,
					  &built) == 0);
	CHECK(built > JUMBO_DATAGRAM && built <= profile.ntb_out_max_size);
	CHECK(drv_usb_cdc_ncm_build_ntb16(&profile, 2U, frame, JUMBO_DATAGRAM + 1U, block, profile.ntb_out_max_size,
					  &built) != 0);
	printf("jumbo blocks: three of %u in %zu bytes, %u refused\n", JUMBO_DATAGRAM, make_block(3U, JUMBO_DATAGRAM),
	       JUMBO_DATAGRAM + 1U);
}

/* (d) the standard size when the limits ask for none larger; a limit under it refused. */
static void
test_standard(void)
{
	static const struct device_parameters device = {16384U, 8192U, 4U, 0, 4U};
	struct drv_usb_cdc_ncm_profile profile;
	struct drv_usb_cdc_ncm_control_request request;
	size_t built;
	int error;

	CHECK(negotiate(&device, 16384U, 8192U, 0, &error, &profile) == DRV_USB_CDC_NCM_MAX_DATAGRAM_SIZE);
	CHECK(error == 0);
	memset(frame, 0x33, sizeof(frame));
	CHECK(drv_usb_cdc_ncm_build_ntb16(&profile, 1U, frame, DRV_USB_CDC_NCM_MAX_DATAGRAM_SIZE, block,
					  profile.ntb_out_max_size, &built) == 0);
	CHECK(drv_usb_cdc_ncm_build_ntb16(&profile, 1U, frame, DRV_USB_CDC_NCM_MAX_DATAGRAM_SIZE + 1U, block,
					  profile.ntb_out_max_size, &built) != 0);
	CHECK(negotiate(&device, 16384U, 8192U, 1000U, &error, NULL) == 0 && error == EINVAL);

	/* SET_MAX_DATAGRAM_SIZE carries the profile's maximum. */
	profile.max_datagram_size = JUMBO_DATAGRAM;
	CHECK(drv_usb_cdc_ncm_make_control_request(&profile, DRV_USB_CDC_NCM_CONTROL_SET_MAX_DATAGRAM_SIZE,
						   &request) == 0);
	CHECK(request.request == DRV_USB_CDC_NCM_SET_MAX_DATAGRAM_SIZE && request.length == 2U);
	CHECK(request.payload[0] == (uint8_t)JUMBO_DATAGRAM && request.payload[1] == (uint8_t)(JUMBO_DATAGRAM >> 8));
	printf("standard: limit 0 gives %u, limit 1000 refused\n", DRV_USB_CDC_NCM_MAX_DATAGRAM_SIZE);
}

/* (e) a limit up to the NTH16 bound, the blocks the only bound. */
static void
test_largest(void)
{
	static const struct device_parameters device = {65536U, 65536U, 4U, 0, 4U};
	uint16_t result;
	int error;

	result = negotiate(&device, 65536U, 65536U, UINT16_MAX, &error, NULL);
	CHECK(error == 0);
	CHECK(result > 65000U);
	printf("largest: limit %u in 64 KB blocks gives %u\n", UINT16_MAX, result);
}

int
main(void)
{
	static const struct device_parameters wide = {65536U, 65536U, 4U, 0, 4U};
	static const struct device_parameters offset_two = {65536U, 65536U, 4U, 2U, 4U};
	static const struct device_parameters byte_aligned = {65536U, 65536U, 1U, 0, 4U};
	static const struct device_parameters in_8k = {8192U, 65536U, 4U, 0, 4U};
	static const struct device_parameters out_8k = {65536U, 8192U, 4U, 0, 4U};

	/* The driver's 32 KB / 16 KB blocks take the jumbo datagram whole. */
	test_derivation(&wide, 32768U, 16384U, JUMBO_DATAGRAM, JUMBO_DATAGRAM);
	test_derivation(&offset_two, 32768U, 16384U, JUMBO_DATAGRAM, JUMBO_DATAGRAM);
	test_derivation(&byte_aligned, 32768U, 16384U, JUMBO_DATAGRAM, JUMBO_DATAGRAM);
	test_derivation(&wide, 65536U, 65536U, JUMBO_DATAGRAM, JUMBO_DATAGRAM);

	/* A device's 8 KB block bounds the datagram below 8192. */
	test_derivation(&in_8k, 32768U, 16384U, 8100U, 8164U);
	test_derivation(&out_8k, 32768U, 16384U, 8100U, 8164U);

	/* The driver's own 16 KB / 8 KB blocks do the same. */
	test_derivation(&wide, 16384U, 8192U, 8100U, 8164U);

	test_jumbo_blocks();
	test_standard();
	test_largest();

	if (failures != 0) {
		printf("ncm-jumbo-wire: FAIL (%u)\n", failures);
		return 1;
	}
	puts("ncm-jumbo-wire: PASS");
	return 0;
}
