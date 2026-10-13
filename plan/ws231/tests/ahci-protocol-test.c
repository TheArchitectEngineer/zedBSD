/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the AHCI command construction: the H2D FIS, the command
 * header, the PRDT split, IDENTIFY decoding and the completion classifier.
 */

#include <drivers/pci/pci-ahci-protocol.h>

#include <stdio.h>
#include <string.h>

/*
 * Records one failed expectation with its line.
 *
 * The macro names a real operation of the test (one checked expectation),
 * so it stays a macro and does not hide which field each line inspects.
 */
#define CHECK(expression) \
	check_record((expression) ? 1 : 0, __LINE__, #expression)

/* How many expectations failed so far. */
static unsigned failures;

static void check_record(int passed, int line, const char *text);
static void test_layout(void);
static void test_fis(void);
static void test_header(void);
static void test_prdt(void);
static void test_identify(void);
static void test_result(void);
static void test_chunks(void);
static void test_states(void);
static void test_signatures(void);

/*
 * Runs every group and reports the outcome.
 */
int
main(
	void)
{
	/* Runs each group of checks. */
	test_layout();
	test_fis();
	test_header();
	test_prdt();
	test_identify();
	test_result();
	test_chunks();
	test_states();
	test_signatures();

	/* Reports a failure count other than zero as a failed run. */
	if (failures != 0U) {
		printf("ahci-protocol: FAIL (%u)\n", failures);
		return 1;
	}

	/* Succeeded: every expectation held. */
	printf("ahci-protocol: PASS\n");
	return 0;
}

/* Counts and prints one failed expectation. */
static void
check_record(
	int passed,
	int line,
	const char *text)
{
	/* A passing expectation leaves no trace. */
	if (passed)
		return;

	printf("FAIL line %d: %s\n", line, text);
	failures++;
}

/* Checks the sizes and offsets AHCI fixes. */
static void
test_layout(void)
{
	/* The FIS, the header and a PRD have their architected sizes. */
	CHECK(sizeof(struct drv_ahci_fis_h2d) == 20U);
	CHECK(sizeof(struct drv_ahci_command_header) == DRV_AHCI_COMMAND_HEADER_SIZE);
	CHECK(sizeof(struct drv_ahci_prd) == 16U);

	/* 32 headers fill the 1 KiB command list. */
	CHECK(DRV_AHCI_SLOT_MAX * DRV_AHCI_COMMAND_HEADER_SIZE == DRV_AHCI_COMMAND_LIST_SIZE);

	/* Tables laid end to end keep their 128-byte alignment. */
	CHECK(DRV_AHCI_TABLE_SIZE % DRV_AHCI_TABLE_ALIGN == 0U);
	CHECK(DRV_AHCI_TABLE_SIZE == 0x180U);

	/* Port registers sit at 0x100 + 0x80 * port. */
	CHECK(drv_ahci_port_offset(0U) == 0x100U);
	CHECK(drv_ahci_port_offset(5U) == 0x380U);
	CHECK(drv_ahci_port_offset(31U) == 0x1080U);

	/* CAP.NCS is the slot count minus one. */
	CHECK(drv_ahci_slot_count(0x00001f00U) == 32U);
	CHECK(drv_ahci_slot_count(0x00000000U) == 1U);
	CHECK(drv_ahci_slot_count(0xc0141f05U) == 32U);
	CHECK(drv_ahci_slot_count(0x00000700U) == 8U);
}

/* Checks the H2D FIS of each command the driver issues. */
static void
test_fis(void)
{
	struct drv_ahci_fis_h2d fis;
	const uint8_t *bytes;

	bytes = (const uint8_t *)&fis;

	/* READ DMA EXT spreads a 48-bit LBA and a 16-bit count over the FIS. */
	memset(&fis, 0xa5, sizeof(fis));
	CHECK(drv_ahci_fis_h2d_build(&fis, DRV_AHCI_ATA_READ_DMA_EXT,
				     UINT64_C(0x0000a1b2c3d4e5f6) & 0xffffffffffffU, 128U));
	CHECK(bytes[0] == 0x27U);
	CHECK(bytes[1] == 0x80U);
	CHECK(bytes[2] == 0x25U);
	CHECK(bytes[3] == 0U);
	CHECK(bytes[4] == 0xf6U);
	CHECK(bytes[5] == 0xe5U);
	CHECK(bytes[6] == 0xd4U);
	CHECK(bytes[7] == 0x40U);
	CHECK(bytes[8] == 0xc3U);
	CHECK(bytes[9] == 0xb2U);
	CHECK(bytes[10] == 0xa1U);
	CHECK(bytes[11] == 0U);
	CHECK(bytes[12] == 128U);
	CHECK(bytes[13] == 0U);
	CHECK(bytes[14] == 0U);
	CHECK(bytes[15] == 0U);
	CHECK(bytes[16] == 0U && bytes[19] == 0U);

	/* WRITE DMA EXT of 65536 sectors encodes the count as zero. */
	CHECK(drv_ahci_fis_h2d_build(&fis, DRV_AHCI_ATA_WRITE_DMA_EXT, 0U, 65536U));
	CHECK(bytes[2] == 0x35U);
	CHECK(bytes[12] == 0U && bytes[13] == 0U);
	CHECK(drv_ahci_fis_h2d_build(&fis, DRV_AHCI_ATA_WRITE_DMA_EXT, 7U, 0x1234U));
	CHECK(bytes[4] == 7U);
	CHECK(bytes[12] == 0x34U && bytes[13] == 0x12U);

	/* The last addressable sectors of the 48-bit space are accepted. */
	CHECK(drv_ahci_fis_h2d_build(&fis, DRV_AHCI_ATA_READ_DMA_EXT,
				     (UINT64_C(1) << 48) - 8U, 8U));
	CHECK(bytes[10] == 0xffU && bytes[4] == 0xf8U);

	/* Commands without sectors carry no LBA, no count and no LBA bit. */
	memset(&fis, 0xa5, sizeof(fis));
	CHECK(drv_ahci_fis_h2d_build(&fis, DRV_AHCI_ATA_FLUSH_CACHE_EXT, 0U, 0U));
	CHECK(bytes[2] == 0xeaU);
	CHECK(bytes[7] == 0U && bytes[12] == 0U && bytes[4] == 0U);
	CHECK(drv_ahci_fis_h2d_build(&fis, DRV_AHCI_ATA_IDENTIFY, 0U, 0U));
	CHECK(bytes[2] == 0xecU && bytes[1] == 0x80U);
	CHECK(drv_ahci_fis_h2d_build(&fis, DRV_AHCI_ATA_FLUSH_CACHE, 0U, 0U));
	CHECK(bytes[2] == 0xe7U && bytes[7] == 0U);
	CHECK(!drv_ahci_fis_h2d_build(&fis, DRV_AHCI_ATA_FLUSH_CACHE, 0U, 1U));

	/* Refuses counts, ranges and commands outside what the driver issues. */
	CHECK(!drv_ahci_fis_h2d_build(&fis, DRV_AHCI_ATA_READ_DMA_EXT, 0U, 0U));
	CHECK(!drv_ahci_fis_h2d_build(&fis, DRV_AHCI_ATA_READ_DMA_EXT, 0U, 65537U));
	CHECK(!drv_ahci_fis_h2d_build(&fis, DRV_AHCI_ATA_READ_DMA_EXT,
				      (UINT64_C(1) << 48) - 7U, 8U));
	CHECK(!drv_ahci_fis_h2d_build(&fis, DRV_AHCI_ATA_READ_DMA_EXT,
				      UINT64_MAX, 1U));
	CHECK(!drv_ahci_fis_h2d_build(&fis, DRV_AHCI_ATA_FLUSH_CACHE_EXT, 1U, 0U));
	CHECK(!drv_ahci_fis_h2d_build(&fis, DRV_AHCI_ATA_IDENTIFY, 0U, 1U));
	CHECK(!drv_ahci_fis_h2d_build(&fis, 0x20U, 0U, 1U));
	CHECK(!drv_ahci_fis_h2d_build(NULL, DRV_AHCI_ATA_IDENTIFY, 0U, 0U));
}

/* Checks the command list entry. */
static void
test_header(void)
{
	struct drv_ahci_command_header header;

	/* A read: FIS length 5, no W bit, one PRD, 64-bit table address. */
	memset(&header, 0xa5, sizeof(header));
	CHECK(drv_ahci_command_header_build(&header, 5U, 0, 1U, UINT64_C(0x123456780)));
	CHECK(header.flags == 0x00010005U);
	CHECK(header.transferred == 0U);
	CHECK(header.table_low == 0x23456780U);
	CHECK(header.table_high == 0x1U);
	CHECK(header.reserved[0] == 0U && header.reserved[3] == 0U);

	/* A write sets W; a non-data command has no PRD. */
	CHECK(drv_ahci_command_header_build(&header, 5U, 1, 16U, 0x1000U));
	CHECK(header.flags == 0x00100045U);
	CHECK(drv_ahci_command_header_build(&header, 5U, 0, 0U, 0x1080U));
	CHECK(header.flags == 0x00000005U);

	/* Refuses a misaligned table, an oversized PRDT and a bad FIS length. */
	CHECK(!drv_ahci_command_header_build(&header, 5U, 0, 1U, 0x1040U));
	CHECK(!drv_ahci_command_header_build(&header, 5U, 0, 17U, 0x1000U));
	CHECK(!drv_ahci_command_header_build(&header, 1U, 0, 1U, 0x1000U));
	CHECK(!drv_ahci_command_header_build(&header, 17U, 0, 1U, 0x1000U));
	CHECK(!drv_ahci_command_header_build(NULL, 5U, 0, 1U, 0x1000U));
}

/* Checks the PRDT split of one contiguous region. */
static void
test_prdt(void)
{
	struct drv_ahci_prd entries[DRV_AHCI_TABLE_PRD_MAX];
	unsigned count;

	/* A 64 KiB bounce buffer is one entry with the interrupt bit. */
	memset(entries, 0xa5, sizeof(entries));
	CHECK(drv_ahci_prdt_build(entries, DRV_AHCI_TABLE_PRD_MAX,
				  UINT64_C(0x7fff0000), 65536U, &count));
	CHECK(count == 1U);
	CHECK(entries[0].address_low == 0x7fff0000U);
	CHECK(entries[0].address_high == 0U);
	CHECK(entries[0].reserved == 0U);
	CHECK(entries[0].count == (0x80000000U | 65535U));

	/* A 512-byte IDENTIFY answer. */
	CHECK(drv_ahci_prdt_build(entries, DRV_AHCI_TABLE_PRD_MAX, 0x2000U, 512U, &count));
	CHECK(count == 1U && entries[0].count == (0x80000000U | 511U));

	/* 9 MiB above 4 GiB: two full 4 MiB entries and a 1 MiB tail. */
	CHECK(drv_ahci_prdt_build(entries, DRV_AHCI_TABLE_PRD_MAX,
				  UINT64_C(0x100000000), 9U * 1024U * 1024U, &count));
	CHECK(count == 3U);
	CHECK(entries[0].address_high == 1U && entries[0].address_low == 0U);
	CHECK(entries[0].count == 0x3fffffU);
	CHECK(entries[1].address_low == 0x400000U && entries[1].count == 0x3fffffU);
	CHECK(entries[2].address_low == 0x800000U);
	CHECK(entries[2].count == (0x80000000U | 0xfffffU));

	/* Refuses an empty or odd region, an odd address, too few entries. */
	CHECK(!drv_ahci_prdt_build(entries, DRV_AHCI_TABLE_PRD_MAX, 0x2000U, 0U, &count));
	CHECK(count == 0U);
	CHECK(!drv_ahci_prdt_build(entries, DRV_AHCI_TABLE_PRD_MAX, 0x2000U, 511U, &count));
	CHECK(!drv_ahci_prdt_build(entries, DRV_AHCI_TABLE_PRD_MAX, 0x2001U, 512U, &count));
	CHECK(!drv_ahci_prdt_build(entries, 2U, 0x2000U, 9U * 1024U * 1024U, &count));
	CHECK(!drv_ahci_prdt_build(NULL, 1U, 0x2000U, 512U, &count));
	CHECK(!drv_ahci_prdt_build(entries, 1U, 0x2000U, 512U, NULL));
}

/* Checks IDENTIFY DEVICE decoding. */
static void
test_identify(void)
{
	struct drv_ahci_identity identity;
	uint16_t words[256];
	char text[41];
	char small[6];
	const char *model;
	unsigned index;

	/* A QEMU-like disk: LBA48, FLUSH EXT, write cache on, 512-byte sectors. */
	memset(words, 0, sizeof(words));
	words[1] = 16383U;
	words[3] = 16U;
	words[6] = 63U;
	words[83] = 0x7400U;
	words[85] = 0x0020U;
	words[100] = 0x0000U;
	words[101] = 0x0020U;
	words[102] = 0U;
	words[103] = 0U;
	model = "QEMU HARDDISK                           ";
	for (index = 0; index < 20U; index++) {
		words[27U + index] = (uint16_t)(((unsigned)(unsigned char)model[index * 2U] << 8) |
						(unsigned)(unsigned char)model[index * 2U + 1U]);
	}
	CHECK(drv_ahci_identify_parse(words, &identity) == 0U);
	CHECK(identity.sector_count == UINT64_C(0x200000));
	CHECK(identity.sector_size == 512U);
	CHECK(identity.flush_ext == 1U);
	CHECK(identity.flush == 1U);
	CHECK(identity.write_cache == 1U);
	CHECK(identity.cylinders == 16383U && identity.heads == 16U);
	CHECK(identity.sectors_per_track == 63U);
	drv_ahci_identify_text(words, 27U, 20U, text, sizeof(text));
	CHECK(strcmp(text, "QEMU HARDDISK") == 0);
	drv_ahci_identify_text(words, 27U, 20U, small, sizeof(small));
	CHECK(strcmp(small, "QEMU") == 0);

	/* A 48-bit capacity uses all four words. */
	words[100] = 0x5678U;
	words[101] = 0x1234U;
	words[102] = 0x0002U;
	words[103] = 0x0001U;
	CHECK(drv_ahci_identify_parse(words, &identity) == 0U);
	CHECK(identity.sector_count == UINT64_C(0x0001000212345678));

	/* A 4 KiB logical sector is reported in words 117 and 118 as 2048 words. */
	words[106] = 0x5000U;
	words[117] = 2048U;
	words[118] = 0U;
	CHECK(drv_ahci_identify_parse(words, &identity) == 0U);
	CHECK(identity.sector_size == 4096U);

	/* 512e: word 106 names larger physical sectors only (bits 13 and 0-3). */
	words[106] = 0x6003U;
	words[117] = 0U;
	words[118] = 0U;
	CHECK(drv_ahci_identify_parse(words, &identity) == 0U);
	CHECK(identity.sector_size == 512U);

	/* Word 106 without its validity pattern keeps 512 bytes. */
	words[106] = 0x9000U;
	CHECK(drv_ahci_identify_parse(words, &identity) == 0U);
	CHECK(identity.sector_size == 512U);

	/* An odd logical sector size is refused. */
	words[106] = 0x5000U;
	words[117] = 260U;
	CHECK(drv_ahci_identify_parse(words, &identity) == DRV_AHCI_IDENTIFY_SECTOR_SIZE);
	words[117] = 0U;
	words[118] = 1U;
	CHECK(drv_ahci_identify_parse(words, &identity) == DRV_AHCI_IDENTIFY_SECTOR_SIZE);
	words[106] = 0U;

	/* A disk without LBA48 or without capacity is refused. */
	words[83] = 0x2000U;
	CHECK(drv_ahci_identify_parse(words, &identity) == DRV_AHCI_IDENTIFY_NO_LBA48);
	words[83] = 0x0400U;
	words[100] = 0U;
	words[101] = 0U;
	words[102] = 0U;
	words[103] = 0U;
	CHECK(drv_ahci_identify_parse(words, &identity) == DRV_AHCI_IDENTIFY_NO_CAPACITY);
	CHECK(identity.flush_ext == 0U);
	CHECK(drv_ahci_identify_parse(NULL, &identity) == DRV_AHCI_IDENTIFY_INVALID);
	CHECK(drv_ahci_identify_parse(words, NULL) == DRV_AHCI_IDENTIFY_INVALID);
}

/* Checks how a port's status maps to a command's outcome. */
static void
test_result(void)
{
	/* Still issued and nothing raised: running. */
	CHECK(drv_ahci_command_result(0U, 0x50U, 1U, 1U) == DRV_AHCI_RESULT_RUNNING);

	/* Slot back with a clean status: done, even with another slot busy. */
	CHECK(drv_ahci_command_result(DRV_AHCI_PX_IS_DHRS, 0x50U, 0U, 1U) == DRV_AHCI_RESULT_DONE);
	CHECK(drv_ahci_command_result(0U, 0x50U, 2U, 1U) == DRV_AHCI_RESULT_DONE);

	/* TFES, or ERR in the final status: a device error. */
	CHECK(drv_ahci_command_result(DRV_AHCI_PX_IS_TFES, 0x51U, 1U, 1U) == DRV_AHCI_RESULT_DEVICE_ERROR);
	CHECK(drv_ahci_command_result(DRV_AHCI_PX_IS_DHRS, 0x51U, 0U, 1U) == DRV_AHCI_RESULT_DEVICE_ERROR);

	/* HBA and interface faults outrank everything else. */
	CHECK(drv_ahci_command_result(DRV_AHCI_PX_IS_HBFS, 0x50U, 0U, 1U) == DRV_AHCI_RESULT_LINK_ERROR);
	CHECK(drv_ahci_command_result(DRV_AHCI_PX_IS_IFS | DRV_AHCI_PX_IS_TFES, 0x51U, 1U, 1U) ==
	      DRV_AHCI_RESULT_LINK_ERROR);
	CHECK(drv_ahci_command_result(DRV_AHCI_PX_IS_PRCS, 0x50U, 1U, 1U) == DRV_AHCI_RESULT_LINK_ERROR);

	/* A non-fatal interface error alone does not end the command. */
	CHECK(drv_ahci_command_result(DRV_AHCI_PX_IS_INFS, 0x50U, 1U, 1U) == DRV_AHCI_RESULT_RUNNING);
}

/* Checks how a request is cut into bounce-buffer-sized commands. */
static void
test_chunks(void)
{
	/* 512-byte sectors: 128 per 64 KiB command. */
	CHECK(drv_ahci_chunk_blocks(1000U, 512U, 65536U) == 128U);
	CHECK(drv_ahci_chunk_blocks(5U, 512U, 65536U) == 5U);

	/* 4 KiB sectors: 16 per command. */
	CHECK(drv_ahci_chunk_blocks(100U, 4096U, 65536U) == 16U);

	/* A sector larger than the buffer does not fit at all. */
	CHECK(drv_ahci_chunk_blocks(1U, 131072U, 65536U) == 0U);
	CHECK(drv_ahci_chunk_blocks(1U, 0U, 65536U) == 0U);

	/* No command moves more than 65536 sectors. */
	CHECK(drv_ahci_chunk_blocks(UINT32_MAX, 512U, (size_t)64U * 1024U * 1024U) == 65536U);
}

/*
 * Checks the command state machine, including the race between a timeout
 * and a late completion, and the engine stop that clears PxCI.
 */
static void
test_states(void)
{
	unsigned state;

	/* An active command stays active until the slot comes back. */
	state = drv_ahci_command_advance(DRV_AHCI_COMMAND_ACTIVE, 0U, 0x50U, 1U, 1U);
	CHECK(state == DRV_AHCI_COMMAND_ACTIVE);

	/* The slot coming back finishes it. */
	state = drv_ahci_command_advance(state, DRV_AHCI_PX_IS_DHRS, 0x50U, 0U, 1U);
	CHECK(state == DRV_AHCI_COMMAND_DONE);

	/* Later status, even an error, does not change a finished command. */
	CHECK(drv_ahci_command_advance(state, DRV_AHCI_PX_IS_TFES, 0x51U, 0U, 1U) ==
	      DRV_AHCI_COMMAND_DONE);
	CHECK(drv_ahci_command_expire(state) == DRV_AHCI_COMMAND_DONE);

	/* A completion seen in the last look before the deadline keeps its outcome. */
	state = drv_ahci_command_advance(DRV_AHCI_COMMAND_ACTIVE, 0U, 0x50U, 0U, 1U);
	state = drv_ahci_command_expire(state);
	CHECK(state == DRV_AHCI_COMMAND_DONE);

	/* A command still running at the deadline is aborted. */
	state = drv_ahci_command_advance(DRV_AHCI_COMMAND_ACTIVE, 0U, 0xd0U, 1U, 1U);
	state = drv_ahci_command_expire(state);
	CHECK(state == DRV_AHCI_COMMAND_ABORTED);

	/* Stopping the engine then clears PxCI; that is not a completion. */
	CHECK(drv_ahci_command_advance(state, 0U, 0x50U, 0U, 1U) == DRV_AHCI_COMMAND_ABORTED);
	CHECK(drv_ahci_command_expire(state) == DRV_AHCI_COMMAND_ABORTED);

	/* Device and link errors are outcomes that stick. */
	state = drv_ahci_command_advance(DRV_AHCI_COMMAND_ACTIVE, DRV_AHCI_PX_IS_TFES, 0x51U, 1U, 1U);
	CHECK(state == DRV_AHCI_COMMAND_DEVICE_ERROR);
	CHECK(drv_ahci_command_advance(state, 0U, 0x50U, 0U, 1U) == DRV_AHCI_COMMAND_DEVICE_ERROR);
	state = drv_ahci_command_advance(DRV_AHCI_COMMAND_ACTIVE, DRV_AHCI_PX_IS_HBFS, 0x50U, 1U, 1U);
	CHECK(state == DRV_AHCI_COMMAND_LINK_ERROR);
	CHECK(drv_ahci_command_expire(state) == DRV_AHCI_COMMAND_LINK_ERROR);

	/* An idle port is not completed by anything. */
	CHECK(drv_ahci_command_advance(DRV_AHCI_COMMAND_IDLE, DRV_AHCI_PX_IS_DHRS, 0x50U, 0U, 1U) ==
	      DRV_AHCI_COMMAND_IDLE);
}

/* Checks the device class taken from PxSIG. */
static void
test_signatures(void)
{
	/* The usual ATA signature, and one with another sector count and LBA low. */
	CHECK(drv_ahci_signature_class(0x00000101U) == DRV_AHCI_SIG_CLASS_ATA);
	CHECK(drv_ahci_signature_class(0x00000001U) == DRV_AHCI_SIG_CLASS_ATA);
	CHECK(drv_ahci_signature_class(0x0000ffffU) == DRV_AHCI_SIG_CLASS_ATA);

	/* ATAPI, port multiplier and enclosure bridge are other classes. */
	CHECK(drv_ahci_signature_class(0xeb140101U) == DRV_AHCI_SIG_CLASS_ATAPI);
	CHECK(drv_ahci_signature_class(0x96690101U) == DRV_AHCI_SIG_CLASS_PM);
	CHECK(drv_ahci_signature_class(0xc33c0101U) == DRV_AHCI_SIG_CLASS_SEMB);

	/* An absent device (all ones) is not ATA. */
	CHECK(drv_ahci_signature_class(0xffffffffU) != DRV_AHCI_SIG_CLASS_ATA);
}
