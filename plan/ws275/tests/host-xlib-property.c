/*
 * WS275 host test: XChangeProperty and XGetWindowProperty carry 16- and
 * 32-bit property units in the little-endian order the library announces,
 * and give them back in the CPU's order.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 */

#include "userland/x11/libX11/xlib.c"

#include <assert.h>

int
main(void)
{
	static const uint8_t wire32[8] = {0x04, 0x03, 0x02, 0x01, 0x08, 0x07, 0x06, 0x05};
	static const uint8_t wire16[4] = {0x02, 0x01, 0x04, 0x03};
	uint32_t units32[2] = {0x01020304U, 0x05060708U};
	uint16_t units16[2] = {0x0102U, 0x0304U};
	uint8_t request[64];
	uint8_t header[32] = {0};
	unsigned long count;
	unsigned long after;
	unsigned char *data;
	Display display = {0};
	Atom type;
	int format;
	int pair[2];

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
	display.fd = pair[0];

	/* Format 32 and 16: the units on the wire are little-endian. */
	assert(XChangeProperty(&display, 1, 2, 3, 32, PropModeReplace, (const unsigned char *)units32, 2) == 1);
	assert(recv(pair[1], request, 32, MSG_WAITALL) == 32);
	assert(memcmp(request + 24, wire32, sizeof(wire32)) == 0);
	assert(XChangeProperty(&display, 1, 2, 3, 16, PropModeReplace, (const unsigned char *)units16, 2) == 1);
	assert(recv(pair[1], request, 28, MSG_WAITALL) == 28);
	assert(memcmp(request + 24, wire16, sizeof(wire16)) == 0);

	/* A format 32 reply comes back in the CPU's order. */
	header[0] = 1;
	header[1] = 32;
	w16(header + 2, (uint16_t)(display.sequence + 1U));
	w32(header + 4, 2);
	w32(header + 8, 6);
	w32(header + 16, 2);
	assert(send(pair[1], header, sizeof(header), 0) == (ssize_t)sizeof(header));
	assert(send(pair[1], wire32, sizeof(wire32), 0) == (ssize_t)sizeof(wire32));
	assert(XGetWindowProperty(&display, 1, 2, 0, 2, False, AnyPropertyType, &type, &format, &count, &after, &data) == 0);
	assert(format == 32 && count == 2 && type == 6);
	assert(memcmp(data, units32, sizeof(units32)) == 0);
	free(data);
	assert(recv(pair[1], request, 24, MSG_WAITALL) == 24);

	/* A format 16 reply too. */
	header[1] = 16;
	w16(header + 2, (uint16_t)(display.sequence + 1U));
	w32(header + 4, 1);
	assert(send(pair[1], header, sizeof(header), 0) == (ssize_t)sizeof(header));
	assert(send(pair[1], wire16, sizeof(wire16), 0) == (ssize_t)sizeof(wire16));
	assert(XGetWindowProperty(&display, 1, 2, 0, 1, False, AnyPropertyType, &type, &format, &count, &after, &data) == 0);
	assert(format == 16 && count == 2);
	assert(memcmp(data, units16, sizeof(units16)) == 0);
	free(data);
	printf("host-xlib-property: PASS\n");
	return 0;
}
