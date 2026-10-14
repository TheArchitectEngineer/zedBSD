/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws236-p003: the MMAL message builders (rpi4-mmal-wire.c) against the
 * byte layout measured from the Raspberry Pi userland's BSD-3-Clause
 * headers compiled for 32-bit ARM (design.md §14, m3): message sizes, field
 * offsets, the four-character codes and the parameter ids.
 */

#include "drivers/platform/rpi4/rpi4-mmal-wire.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;

void *kern_memset(void *destination, int value, size_t bytes) { return memset(destination, value, bytes); }
void *kern_memcpy(void *destination, const void *source, size_t bytes) { return memcpy(destination, source, bytes); }

static void check(int condition, const char *what)
{
	if (!condition) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static unsigned fourcc_le(char a, char b, char c, char d)
{
	return (unsigned)(unsigned char)a | ((unsigned)(unsigned char)b << 8) | ((unsigned)(unsigned char)c << 16) | ((unsigned)(unsigned char)d << 24);
}

int main(void)
{
	uint8_t message[DRV_RPI4_MMAL_MAX_MESSAGE];
	uint8_t info[DRV_RPI4_MMAL_INFO_BYTES];
	size_t size;
	unsigned index;

	/* The measured sizes. */
	check(DRV_RPI4_MMAL_HEADER_BYTES == 24U, "header 24");
	check(DRV_RPI4_MMAL_BUFFER_BYTES == 296U, "buffer message 296");
	check(DRV_RPI4_MMAL_EVENT_BYTES == 304U, "event message 304");
	check(DRV_RPI4_MMAL_INFO_BYTES == 316U, "port info 316");
	check(DRV_RPI4_MMAL_INFO_SET_BYTES == 304U, "port info set 304");
	check(DRV_RPI4_MMAL_ACTION_BYTES == 100U, "port action 100");
	check(DRV_RPI4_MMAL_CREATE_BYTES == 160U && DRV_RPI4_MMAL_CREATE_REPLY_BYTES == 44U, "create 160, reply 44");
	check(DRV_RPI4_MMAL_VERSION_BYTES == 40U && DRV_RPI4_MMAL_REPLY_BYTES == 28U, "version 40, reply 28");
	check(DRV_RPI4_MMAL_INFO_PORT == 48U && DRV_RPI4_MMAL_INFO_SET_PORT == 36U, "port blocks at 48 and 36");
	check(DRV_RPI4_MMAL_INFO_PORT + DRV_RPI4_MMAL_PORT_BYTES == 112U, "info format at 112");
	check(DRV_RPI4_MMAL_INFO_PORT + DRV_RPI4_MMAL_PORT_BYTES + DRV_RPI4_MMAL_FORMAT_BYTES == 144U, "info es at 144");
	check(DRV_RPI4_MMAL_INFO_PORT + DRV_RPI4_MMAL_PORT_BYTES + DRV_RPI4_MMAL_FORMAT_BYTES + DRV_RPI4_MMAL_ES_BYTES == 188U, "info extradata at 188");
	check(DRV_RPI4_MMAL_PORT_BUFFER_NUM_MIN == 24U && DRV_RPI4_MMAL_PORT_BUFFER_SIZE_RECOMMENDED == 40U, "port buffer needs");
	check(DRV_RPI4_MMAL_PORT_BUFFER_NUM == 44U && DRV_RPI4_MMAL_PORT_BUFFER_SIZE == 48U, "port buffer choice");
	check(DRV_RPI4_MMAL_BUFFER_DRIVER_MAGIC == 24U && DRV_RPI4_MMAL_BUFFER_CMD == 56U + 8U, "buffer driver area and header");
	check(DRV_RPI4_MMAL_BUFFER_PTS == 56U + 32U && DRV_RPI4_MMAL_BUFFER_PLANES == 112U, "pts and planes");
	check(DRV_RPI4_MMAL_BUFFER_PAYLOAD_IN_MESSAGE == 160U, "payload in message at 160");
	check(DRV_RPI4_MMAL_EVENT_DATA == 44U, "event data at 44");
	check(DRV_RPI4_MMAL_PARAMETER_VALUE == 40U, "parameter value at 40");
	check(DRV_RPI4_MMAL_CHANGED_FORMAT == 20U && DRV_RPI4_MMAL_CHANGED_ES == 52U, "format change blocks");

	/* The four-character codes. */
	check(DRV_RPI4_MMAL_MAGIC == fourcc_le('m', 'm', 'a', 'l'), "magic mmal little endian");
	check(DRV_RPI4_MMAL_SERVICE_FOURCC == 0x6d6d616cU, "service mmal big endian");
	check(DRV_RPI4_MMAL_ENCODING_H264 == fourcc_le('H', '2', '6', '4'), "H264");
	check(DRV_RPI4_MMAL_ENCODING_NV12 == fourcc_le('N', 'V', '1', '2'), "NV12");
	check(DRV_RPI4_MMAL_ENCODING_I420 == fourcc_le('I', '4', '2', '0'), "I420");
	check(DRV_RPI4_MMAL_EVENT_FORMAT_CHANGED == fourcc_le('E', 'F', 'C', 'H'), "EFCH");

	/* A buffer message. */
	size = drv_rpi4_mmal_build_buffer(message, DRV_RPI4_MMAL_MSG_BUFFER_FROM_HOST, 7, 0x11, 0x22, 0x5a012003U,
					  65536, 1234, DRV_RPI4_MMAL_FLAG_FRAME_END, 42);
	check(size == 296U, "buffer message length");
	check(drv_rpi4_mmal_get32(message, 0) == DRV_RPI4_MMAL_MAGIC, "buffer magic");
	check(drv_rpi4_mmal_get32(message, 4) == 11U && drv_rpi4_mmal_get32(message, 12) == 7U, "buffer id and waiter");
	check(drv_rpi4_mmal_get32(message, 24) == DRV_RPI4_MMAL_MAGIC, "driver magic");
	check(drv_rpi4_mmal_get32(message, 28) == 0x11U && drv_rpi4_mmal_get32(message, 32) == 0x22U, "component and port");
	check(drv_rpi4_mmal_get32(message, 36) == 0x5a012003U && drv_rpi4_mmal_get32(message, 68) == 0x5a012003U, "context in driver area and data");
	check(drv_rpi4_mmal_get32(message, 72) == 65536U && drv_rpi4_mmal_get32(message, 76) == 1234U, "alloc size and length");
	check(drv_rpi4_mmal_get32(message, 84) == 4U, "frame end flag");
	check(drv_rpi4_mmal_get64(message, 88) == 42U, "pts");
	check(drv_rpi4_mmal_get64(message, 96) == (uint64_t)1 << 63, "dts unknown");
	check(drv_rpi4_mmal_get32(message, 152) == 0 && drv_rpi4_mmal_get32(message, 160) == 0, "not zero copy, no payload in message");

	/* A component create. */
	size = drv_rpi4_mmal_build_create(message, 3, 0x5a030000U, "ril.video_decode");
	check(size == 160U && drv_rpi4_mmal_get32(message, 4) == 4U, "create id");
	check(drv_rpi4_mmal_get32(message, 24) == 0x5a030000U, "create client");
	check(strcmp((const char *)message + 28, "ril.video_decode") == 0, "create name");

	/* Port info set from a reply: type and index, then the blocks moved from 48 to 36. */
	for (index = 0; index < sizeof(info); index++)
		info[index] = (uint8_t)index;
	drv_rpi4_mmal_put32(info, 32, 3);
	drv_rpi4_mmal_put32(info, 36, 0);
	size = drv_rpi4_mmal_build_info_set(message, 5, 0x77, info);
	check(size == 304U && drv_rpi4_mmal_get32(message, 4) == 9U, "info set id");
	check(drv_rpi4_mmal_get32(message, 24) == 0x77U, "info set component");
	check(drv_rpi4_mmal_get32(message, 28) == 3U && drv_rpi4_mmal_get32(message, 32) == 0U, "info set type and index");
	check(memcmp(message + 36, info + 48, 268) == 0, "info set blocks");

	/* A port action with the port block, and a parameter. */
	size = drv_rpi4_mmal_build_action(message, 6, 0x77, 0x88, DRV_RPI4_MMAL_ACTION_ENABLE, info);
	check(size == 100U && drv_rpi4_mmal_get32(message, 32) == 1U, "action enable");
	check(memcmp(message + 36, info + 48, 64) == 0, "action port block");
	size = drv_rpi4_mmal_build_parameter32(message, 8, 0x77, 0x88, 0x20024U, 0);
	check(size == 44U && drv_rpi4_mmal_get32(message, 4) == 14U, "parameter id and length");
	check(drv_rpi4_mmal_get32(message, 32) == 0x20024U && drv_rpi4_mmal_get32(message, 36) == 12U, "parameter header");

	if (failures != 0) {
		printf("mmal-wire: %u check(s) failed\n", failures);
		return 1;
	}
	printf("mmal-wire: all PASS\n");
	return 0;
}
