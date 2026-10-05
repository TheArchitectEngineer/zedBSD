/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the CCID messages and class descriptor (ws161-p003,
 * src/drivers/usb/usb-ccid-proto.c): a class descriptor of a contactless
 * reader of extended APDUs with two slots, the exchange levels, a
 * command's header, and answers whole, short and with a length past their
 * end.
 */

#include <drivers/usb/usb-ccid.h>

#include <stdio.h>
#include <string.h>
#include <uapi/errno.h>

/* The checks that failed. */
static unsigned failures;

/* Counts and reports a check that does not hold. */
static void
expect(
	int condition,
	const char *what)
{
	if (condition)
		return;
	failures++;
	printf("FAIL: %s\n", what);
}

int
main(void)
{
	/*
	 * A class descriptor: CCID 1.10, two slots, T=0 and T=1, automatic
	 * voltage and extended APDUs (dwFeatures 0x000404BA), messages of 271
	 * bytes, one busy slot.
	 */
	static const unsigned char descriptor[54] = {
		0x36, 0x21, 0x10, 0x01, 0x01, 0x07, 0x03, 0x00, 0x00, 0x00, 0xc0, 0x12, 0x00, 0x00,
		0xc0, 0x12, 0x00, 0x00, 0x00, 0x67, 0x32, 0x00, 0x00, 0xce, 0x99, 0x0c, 0x00, 0x00,
		0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xba, 0x04,
		0x04, 0x00, 0x0f, 0x01, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x01
	};
	/* An answer of an XfrBlock: DataBlock, 4 bytes, slot 1, bSeq 7, status 0, chain 0x01. */
	static const unsigned char answer[14] = {
		0x80, 0x04, 0x00, 0x00, 0x00, 0x01, 0x07, 0x00, 0x00, 0x01, 0x90, 0x00, 0xaa, 0xbb
	};
	struct ccid_class class;
	struct ccid_reply reply;
	unsigned char header[CCID_HEADER];
	unsigned char wrong[54];
	int error;

	/* The class descriptor. */
	error = drv_ccid_parse_class(descriptor, sizeof(descriptor), &class);
	expect(error == 0, "class: read");
	expect(class.version == 0x0110U, "class: version");
	expect(class.max_slot_index == 1U, "class: two slots");
	expect(class.protocols == 3U, "class: protocols");
	expect(class.features == 0x000404baU, "class: features");
	expect(class.max_message == 271U, "class: longest message");
	expect(class.max_busy_slots == 1U, "class: busy slots");
	error = drv_ccid_parse_class(descriptor, 40U, &class);
	expect(error == EINVAL, "class: too short");
	memcpy(wrong, descriptor, sizeof(wrong));
	wrong[1] = 0x24U;
	error = drv_ccid_parse_class(wrong, sizeof(wrong), &class);
	expect(error == EINVAL, "class: other type");

	/* The exchange levels. */
	expect(drv_ccid_level(0x000404baU) == CCID_LEVEL_EXTENDED, "level: extended");
	expect(drv_ccid_level(0x000204baU) == CCID_LEVEL_SHORT, "level: short");
	expect(drv_ccid_level(0x000104baU) == CCID_LEVEL_NONE, "level: TPDU");
	expect(drv_ccid_level(0x000004baU) == CCID_LEVEL_NONE, "level: characters");

	/* A command's header: XfrBlock of 300 bytes, slot 1, bSeq 9, wLevelParameter 0x0010. */
	drv_ccid_header(header, CCID_PC_TO_RDR_XFR_BLOCK, 300U, 1U, 9U, 0U, 0x10U, 0x00U);
	expect(header[0] == 0x6fU && header[1] == 0x2cU && header[2] == 0x01U && header[3] == 0U && header[4] == 0U,
	    "header: type and length");
	expect(header[5] == 1U && header[6] == 9U && header[7] == 0U && header[8] == 0x10U && header[9] == 0U,
	    "header: slot, sequence and parameters");

	/* An answer, whole. */
	error = drv_ccid_parse_reply(answer, sizeof(answer), &reply);
	expect(error == 0, "reply: read");
	expect(reply.type == CCID_RDR_TO_PC_DATA_BLOCK && reply.length == 4U, "reply: type and length");
	expect(reply.slot == 1U && reply.sequence == 7U && reply.status == 0U, "reply: slot, sequence, status");
	expect(reply.parameter == CCID_CHAIN_BEGINS, "reply: chain");
	expect(reply.data == answer + CCID_HEADER && reply.data[0] == 0x90U, "reply: data");

	/* Answers that do not hold what they say. */
	error = drv_ccid_parse_reply(answer, 13U, &reply);
	expect(error == EIO, "reply: data cut short");
	error = drv_ccid_parse_reply(answer, 9U, &reply);
	expect(error == EIO, "reply: header cut short");

	/* The verdict. */
	if (failures != 0U) {
		printf("ccid-proto-host-test: FAIL (%u)\n", failures);
		return 1;
	}
	printf("ccid-proto-host-test: PASS\n");
	return 0;
}
