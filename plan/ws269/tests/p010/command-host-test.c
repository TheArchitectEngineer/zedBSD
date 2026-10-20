/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of ws269-p010e: the registers render-command.c puts in a
 * fragment and a geometry command, at the offsets of Linux v7.3
 * pvr_rogue_fwif_client_check.h, with the words worked out by hand from
 * Mesa 26.2.5 cr.xml and vdm.xml.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drivers/gpu/powervr/render-command.h"

static int failures;
static int passes;

static void
expect(
	int good,
	const char *what)
{
	if (good) {
		passes++;
		return;
	}
	failures++;
	printf("FAIL %s\n", what);
}

static uint64_t
read64(
	const uint8_t *bytes,
	unsigned offset)
{
	uint64_t value;
	unsigned index;

	value = 0;
	for (index = 0; index < 8U; index++)
		value |= (uint64_t)bytes[offset + index] << (8U * index);
	return value;
}

static uint32_t
read32(
	const uint8_t *bytes,
	unsigned offset)
{
	return (uint32_t)read64(bytes, offset) ;
}

int
main(void)
{
	static uint8_t fragment[POWERVR_FRAGMENT_BYTES];
	static uint8_t geometry[POWERVR_GEOMETRY_BYTES];
	struct powervr_fragment_setup setup;
	struct powervr_geometry_setup geo;
	unsigned index;

	/* A 64x64 frame: 4x4 tiles. */
	memset(&setup, 0, sizeof(setup));
	setup.tiles_x = 4;
	setup.tiles_y = 4;
	for (index = 0; index < 3U; index++) {
		setup.background[index] = 0x100U + index;
		setup.partial_background[index] = 0x200U + index;
		setup.pbe[index] = 0x300U + index;
	}
	setup.event_data = 0x1230;
	setup.event_info = 0x201;
	memset(fragment, 0, sizeof(fragment));
	powervr_fragment_command(&setup, fragment);

	/* regs at 16: pixel output 0 (mask 0x3ff << 3, 2 registers 0), bgobjdepth 36, bgobjvals 40, aa 44, isp_ctl 52, event info 60, data 72, pbe 152, bgnd 352, pr_bgnd 400. */
	expect(read32(fragment, 16) == 0x1ff8U, "pixel output control");
	expect(read32(fragment, 52) == 0x3f800000U, "background depth 1.0");
	expect(read32(fragment, 56) == 0x300U, "background tag and mask");
	expect(read32(fragment, 60) == 0, "no antialiasing");
	expect(read32(fragment, 68) == 0x80033000U, "isp control: skip, empty tiles, sample positions, pipe 3");
	expect(read32(fragment, 76) == 0x201U && read32(fragment, 88) == 0x1230U, "event info and data");
	expect(read64(fragment, 168) == 0x300U && read64(fragment, 184) == 0x302U, "pbe words of attachment 0");
	expect(read64(fragment, 192) == 0, "attachment 1 empty");
	expect(read64(fragment, 368) == 0x100U && read64(fragment, 384) == 0x102U, "background");
	expect(read64(fragment, 416) == 0x200U && read64(fragment, 432) == 0x202U, "partial render background");
	expect(read32(fragment, 464) == 0, "fragment flags");

	/* The quirks: no skip for 2 tiles wide with 1 or 3 tile group rows, or 2 rows. */
	expect(powervr_fragment_isp_control(2, 2) == 0x00033000U, "2x2 tiles: no skip");
	expect(powervr_fragment_isp_control(2, 4) == 0x00033000U, "2x4 tiles: two group rows, no skip");
	expect(powervr_fragment_isp_control(2, 8) == 0x80033000U, "2x8 tiles: four group rows, skip");
	expect(powervr_fragment_isp_control(1, 6) == 0x00033000U, "1x6 tiles: three group rows, no skip");
	expect(powervr_fragment_isp_control(3, 2) == 0x80033000U, "3 tiles wide: skip");

	/* Geometry: stream 16, ppp_ctrl 44 (0x202), te_psg 48 (bit 17 | stride), resume size 56 (1 << 29 | 1 << 16), flags 80. */
	geo.stream = 0x8000001000ULL;
	geo.region_header_bytes = 0x800;
	geo.shared_registers = 1;
	memset(geometry, 0, sizeof(geometry));
	powervr_geometry_command(&geo, geometry);
	expect(read64(geometry, 16) == 0x8000001000ULL, "stream base");
	expect(read32(geometry, 44) == 0x202U, "ppp control");
	expect(read32(geometry, 48) == 0x20000U, "te_psg, stride 0 under 4096");
	expect(read32(geometry, 56) == 0x20010000U, "resume size: one common unit");
	expect(read32(geometry, 80) == 3U, "first and last kick");
	geo.region_header_bytes = 0x5000;
	geo.shared_registers = 17;
	powervr_geometry_command(&geo, geometry);
	expect(read32(geometry, 48) == 0x20005U && read32(geometry, 56) == 0x20020000U, "stride 5, two common units");

	/* Transfer: regs at 8; bgobjvals 0, pixel output 4, clear 8..20, isp_render 56, frag_screen 64, bgnd 72..88, mtile base 96, pbe 104..168; flags 184. */
	{
		static uint8_t transfer[POWERVR_TRANSFER_COMMAND_BYTES];
		struct powervr_transfer_regs regs;

		memset(&regs, 0, sizeof(regs));
		regs.isp_bgobjvals = 0x200;
		regs.usc_pixel_output_ctrl = 0x1ff8;
		regs.usc_clear[3] = 0x44;
		regs.isp_render = 3;
		regs.frag_screen = 0x3f003f;
		regs.event_pixel_pds_data = 0x1230;
		regs.pds_bgnd[2] = 0x99;
		regs.isp_mtile_base = 0x77;
		regs.pbe[0] = 0x11;
		regs.pbe[8] = 0x88;
		memset(transfer, 0, sizeof(transfer));
		powervr_transfer_command(&regs, transfer);
		expect(read32(transfer, 8) == 0x200 && read32(transfer, 12) == 0x1ff8 && read32(transfer, 28) == 0x44, "transfer: bgobjvals, pixel output, clear 3");
		expect(read32(transfer, 64) == 3 && read32(transfer, 72) == 0x3f003f && read32(transfer, 60) == 0x1230, "transfer: isp_render, frag_screen, event data");
		expect(read64(transfer, 96) == 0x99 && read64(transfer, 104) == 0x77 && read64(transfer, 112) == 0x11 && read64(transfer, 176) == 0x88, "transfer: bgnd 3, mtile base, pbe 0 and 8");
		expect(read32(transfer, 184) == 0 && read32(transfer, 0) == 0, "transfer: flags and common part zero");
		expect(POWERVR_CLIENT_TRANSFER == 0x2abc80caU, "transfer: TQ_3D 202");
	}

	printf("command-host-test: %d passed, %d failed\n", passes, failures);
	return failures != 0;
}
