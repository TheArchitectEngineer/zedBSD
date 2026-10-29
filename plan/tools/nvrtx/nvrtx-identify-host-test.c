/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the nvrtx chip identity decoding, the family records, the
 * product table and the attach's boot choice.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-private.h"

/* One identity pair and what the decoding must make of it. */
struct case_row {
	const char *what;
	unsigned ident0;
	unsigned ident1;
	int error;
	unsigned chipset;
	const char *name;
	/* The family record's name, or NULL for a chip without one. */
	const char *profile;
};

/*
 * What one family record must hold.
 *
 * Values referenced: Linux v6.19 drivers/gpu/drm/nouveau/nvkm/subdev/gsp/
 * (tu102.c, tu116.c, ga102.c, ad102.c: firmware and signatures),
 * nvkm/subdev/fb/ga102.c (the VRAM size register), nvkm/subdev/devinit/
 * ga100.c (the display fuse), nvkm/subdev/gsp/rm/r570/nvrm/gsp.h (heap),
 * nvkm/subdev/gsp/rm/r570/gr.c (the scrubber channel), NVIDIA open-gpu-doc
 * classes (the class numbers, clc3c0qmd.h and clc6c0qmd.h), and Mesa 25.3.6
 * src/nouveau/winsys/nouveau_device.c (warps and carve-outs).
 */
struct profile_row {
	const char *name;
	int verified;
	const char *image_file;
	const char *booter_load_file;
	const char *falcon_bootloader_file;
	const char *signature_section;
	enum nvrtx_falcon_style falcon_style;
	unsigned vram_size_register;
	int vram_size_in_mib;
	unsigned display_fuse_register;
	unsigned heap_os_mib;
	unsigned heap_min_mib;
	int gr_scrubber;
	unsigned classes[9];
	unsigned window_storage_pitch;
	unsigned qmd_major;
	unsigned qmd_minor;
	unsigned sm;
	unsigned warps_per_sm;
};

/* One combination the attach's boot choice is checked on. */
struct choice_row {
	const char *what;
	/* The chip's identity register. */
	unsigned ident0;
	int gop_on_gpu;
	bool gsp_wanted;
	bool gsp_forced;
	enum nvrtx_boot_choice choice;
};

static unsigned check_cases(void);
static unsigned check_profiles(void);
static unsigned check_products(void);
static unsigned check_choices(void);
static unsigned check_device_info(void);

/* The identity cases: every chip with a record, the named-only ones, and every refusal. */
static const struct case_row cases[] = {
	{ "TU102", 0x162000a1U, 0U, 0, 0x162U, "TU102", "TU10x" },
	{ "TU104", 0x164000a1U, 0U, 0, 0x164U, "TU104", "TU10x" },
	{ "TU106 rev a1", 0x166000a1U, 0U, 0, 0x166U, "TU106", "TU10x" },
	{ "TU117", 0x167000a1U, 0U, 0, 0x167U, "TU117", "TU11x" },
	{ "TU116", 0x168000a1U, 0U, 0, 0x168U, "TU116", "TU11x" },
	{ "GA100", 0x170000a1U, 0U, 0, 0x170U, "GA100", NULL },
	{ "GA102", 0x172000a1U, 0U, 0, 0x172U, "GA102", "GA10x" },
	{ "GA103", 0x173000a1U, 0U, 0, 0x173U, "GA103", "GA10x" },
	{ "GA104", 0x174000a1U, 0U, 0, 0x174U, "GA104", "GA10x" },
	{ "GA106", 0x176000a1U, 0U, 0, 0x176U, "GA106", "GA10x" },
	{ "GA107", 0x177000a1U, 0U, 0, 0x177U, "GA107", "GA10x" },
	{ "GH100", 0x180000a1U, 0U, 0, 0x180U, "GH100", NULL },
	{ "AD102", 0x192000a1U, 0U, 0, 0x192U, "AD102", "AD10x" },
	{ "AD103", 0x193000a1U, 0U, 0, 0x193U, "AD103", "AD10x" },
	{ "AD104", 0x194000a1U, 0U, 0, 0x194U, "AD104", "AD10x" },
	{ "AD106", 0x196000a1U, 0U, 0, 0x196U, "AD106", "AD10x" },
	{ "AD107", 0x197000a1U, 0U, 0, 0x197U, "AD107", "AD10x" },
	{ "GB202", 0x1b2000a1U, 0U, 0, 0x1b2U, "GB202", NULL },
	{ "GB207", 0x1b7000a1U, 0U, 0, 0x1b7U, "GB207", NULL },
	{ "unknown Turing number", 0x16f000a1U, 0U, ENOTSUP, 0x16fU, "unknown", NULL },
	{ "unknown Ampere number", 0x175000a1U, 0U, ENOTSUP, 0x175U, "unknown", NULL },
	{ "newer architecture", 0x1c2000a1U, 0U, ENOTSUP, 0x1c2U, "unknown", NULL },
	{ "Volta (older)", 0x140000a1U, 0U, ENODEV, 0x140U, "unknown", NULL },
	{ "all ones", 0xffffffffU, 0U, ENODEV, 0U, "unknown", NULL },
	{ "big-endian", 0x166000a1U, 0x01000001U, ENODEV, 0U, "unknown", NULL },
	{ "virtual function", 0x166000a1U, 0x00010000U, ENODEV, 0x166U, "unknown", NULL },
	{ "odd second register", 0x166000a1U, 0x00000100U, ENODEV, 0U, "unknown", NULL }
};

/*
 * The family records.  The classes are, in order: channel, copy, 3D,
 * compute, display, core, window, window-immediate and cursor.
 */
static const struct profile_row profiles[] = {
	{ "TU10x", 1, "tu102/gsp/gsp-570.144.bin", "tu102/gsp/booter_load-570.144.bin", "tu102/acr/bl.bin",
	  ".fwsignature_tu10x", NVRTX_FALCON_PORTS, 0x100ce0U, 0, 0x021c04U, 0U, 64U, 1,
	  { 0xc46fU, 0xc5b5U, 0xc597U, 0xc5c0U, 0xc570U, 0xc57dU, 0xc57eU, 0xc57bU, 0xc57aU },
	  0x10U, 2U, 2U, 75U, 32U },
	{ "TU11x", 0, "tu102/gsp/gsp-570.144.bin", "tu116/gsp/booter_load-570.144.bin", "tu102/acr/bl.bin",
	  ".fwsignature_tu11x", NVRTX_FALCON_PORTS, 0x100ce0U, 0, 0x021c04U, 0U, 64U, 0,
	  { 0xc46fU, 0xc5b5U, 0xc597U, 0xc5c0U, 0xc570U, 0xc57dU, 0xc57eU, 0xc57bU, 0xc57aU },
	  0x10U, 2U, 2U, 75U, 32U },
	{ "GA10x", 0, "ga102/gsp/gsp-570.144.bin", "ga102/gsp/booter_load-570.144.bin", NULL,
	  ".fwsignature_ga10x", NVRTX_FALCON_BOOT_ROM, 0x1183a4U, 1, 0x820c04U, 22U, 170U, 0,
	  { 0xc56fU, 0xc7b5U, 0xc797U, 0xc7c0U, 0xc670U, 0xc67dU, 0xc67eU, 0xc67bU, 0xc67aU },
	  0U, 3U, 0U, 86U, 48U },
	{ "AD10x", 0, "ga102/gsp/gsp-570.144.bin", "ad102/gsp/booter_load-570.144.bin", NULL,
	  ".fwsignature_ad10x", NVRTX_FALCON_BOOT_ROM, 0x1183a4U, 1, 0x820c04U, 22U, 170U, 0,
	  { 0xc56fU, 0xc7b5U, 0xc997U, 0xc9c0U, 0xc770U, 0xc77dU, 0xc67eU, 0xc67bU, 0xc67aU },
	  0U, 3U, 0U, 89U, 48U }
};

/* Every combination of family, firmware picture and nvrtx.gsp the attach can meet. */
static const struct choice_row choices[] = {
	{ "TU10x picture default", 0x166000a1U, 1, true, false, NVRTX_BOOT_START },
	{ "TU10x picture gsp=1", 0x166000a1U, 1, true, true, NVRTX_BOOT_START },
	{ "TU10x picture gsp=0", 0x166000a1U, 1, false, false, NVRTX_BOOT_LEAVE_OFF },
	{ "TU10x no picture default", 0x166000a1U, 0, true, false, NVRTX_BOOT_LEAVE_NO_PICTURE },
	{ "TU10x no picture gsp=1", 0x166000a1U, 0, true, true, NVRTX_BOOT_START },
	{ "TU10x no picture gsp=0", 0x166000a1U, 0, false, false, NVRTX_BOOT_LEAVE_OFF },
	{ "TU11x picture default", 0x168000a1U, 1, true, false, NVRTX_BOOT_LEAVE_UNSEEN },
	{ "TU11x picture gsp=1", 0x168000a1U, 1, true, true, NVRTX_BOOT_START_UNSEEN },
	{ "TU11x picture gsp=0", 0x168000a1U, 1, false, false, NVRTX_BOOT_LEAVE_OFF },
	{ "TU11x no picture default", 0x168000a1U, 0, true, false, NVRTX_BOOT_LEAVE_UNSEEN },
	{ "TU11x no picture gsp=1", 0x168000a1U, 0, true, true, NVRTX_BOOT_START_UNSEEN },
	{ "GA10x picture default", 0x174000a1U, 1, true, false, NVRTX_BOOT_LEAVE_UNSEEN },
	{ "GA10x picture gsp=1", 0x174000a1U, 1, true, true, NVRTX_BOOT_START_UNSEEN },
	{ "GA10x no picture gsp=1", 0x174000a1U, 0, true, true, NVRTX_BOOT_START_UNSEEN },
	{ "GA10x picture gsp=0", 0x174000a1U, 1, false, false, NVRTX_BOOT_LEAVE_OFF },
	{ "AD10x picture default", 0x194000a1U, 1, true, false, NVRTX_BOOT_LEAVE_UNSEEN },
	{ "AD10x picture gsp=1", 0x194000a1U, 1, true, true, NVRTX_BOOT_START_UNSEEN },
	{ "AD10x no picture default", 0x194000a1U, 0, true, false, NVRTX_BOOT_LEAVE_UNSEEN },
	{ "AD10x picture gsp=0", 0x194000a1U, 1, false, false, NVRTX_BOOT_LEAVE_OFF },
	{ "GB202 picture default", 0x1b2000a1U, 1, true, false, NVRTX_BOOT_LEAVE_NO_PATH },
	{ "GB202 picture gsp=1", 0x1b2000a1U, 1, true, true, NVRTX_BOOT_LEAVE_NO_PATH },
	{ "GB202 picture gsp=0", 0x1b2000a1U, 1, false, false, NVRTX_BOOT_LEAVE_OFF },
	{ "GA100 gsp=1", 0x170000a1U, 0, true, true, NVRTX_BOOT_LEAVE_NO_PATH }
};

/* The product table's size and laptop count, from pci.ids of 2026-10-15 (the same on 2026-10-29). */
#define EXPECTED_PRODUCTS	288U
#define EXPECTED_LAPTOPS	121U

int
main(void)
{
	unsigned failures;

	/* Runs every group and adds up the failures. */
	failures = check_cases();
	failures += check_profiles();
	failures += check_products();
	failures += check_choices();
	failures += check_device_info();

	/* Reports the verdict. */
	if (failures != 0U) {
		printf("FAIL nvrtx identify: %u failures\n", failures);
		return 1;
	}

	printf("PASS nvrtx identify: %zu identities, %zu families, %zu products, %zu boot choices\n",
	    sizeof(cases) / sizeof(cases[0]),
	    sizeof(profiles) / sizeof(profiles[0]),
	    nvrtx_product_count(),
	    sizeof(choices) / sizeof(choices[0]));
	return 0;
}

/* Decodes every identity case and compares the chip and its family record. */
static unsigned
check_cases(void)
{
	struct nvrtx_chip chip;
	const char *profile;
	unsigned index;
	unsigned failures;
	int error;
	int same;

	/* Decodes each case and compares the answer. */
	failures = 0U;
	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		error = nvrtx_identify(cases[index].ident0, cases[index].ident1, &chip);

		/* Names the record the chip points at, if any. */
		profile = NULL;
		if (chip.profile != NULL)
			profile = chip.profile->name;

		/* Compares the record's name, both absent counting as the same. */
		same = 0;
		if (profile == NULL && cases[index].profile == NULL)
			same = 1;
		if (profile != NULL && cases[index].profile != NULL && strcmp(profile, cases[index].profile) == 0)
			same = 1;

		/* Any field that differs fails the case. */
		if (error != cases[index].error ||
		    chip.chipset != cases[index].chipset ||
		    strcmp(chip.name, cases[index].name) != 0 ||
		    same == 0) {
			printf("FAIL %s: error %d chip %03x %s profile %s\n",
			    cases[index].what,
			    error,
			    chip.chipset,
			    chip.name,
			    profile != NULL ? profile : "none");
			failures++;
		}
	}

	/* The count of failed cases. */
	return failures;
}

/* Compares every field of each family record with the expected table. */
static unsigned
check_profiles(void)
{
	static const unsigned idents[] = { 0x166000a1U, 0x168000a1U, 0x174000a1U, 0x194000a1U };
	const struct nvrtx_profile *profile;
	const struct profile_row *row;
	struct nvrtx_chip chip;
	unsigned classes[9];
	unsigned index;
	unsigned failures;
	int error;
	int bootloader_same;

	/* Finds each family's record through one of its chips. */
	failures = 0U;
	for (index = 0U; index < sizeof(profiles) / sizeof(profiles[0]); index++) {
		row = &profiles[index];
		error = nvrtx_identify(idents[index], 0U, &chip);
		profile = chip.profile;
		if (error != 0 || profile == NULL || strcmp(profile->name, row->name) != 0) {
			printf("FAIL profile %s: not found through %08x\n", row->name, idents[index]);
			failures++;
			continue;
		}

		/* Gathers the classes in the table's order. */
		classes[0] = profile->class_channel;
		classes[1] = profile->class_copy;
		classes[2] = profile->class_threed;
		classes[3] = profile->class_compute;
		classes[4] = profile->class_display;
		classes[5] = profile->class_core;
		classes[6] = profile->class_window;
		classes[7] = profile->class_wimm;
		classes[8] = profile->class_cursor;

		/* The falcon bootloader is absent where the falcon loads FWSEC itself. */
		bootloader_same = 0;
		if (profile->falcon_bootloader_file == NULL && row->falcon_bootloader_file == NULL)
			bootloader_same = 1;
		if (profile->falcon_bootloader_file != NULL && row->falcon_bootloader_file != NULL &&
		    strcmp(profile->falcon_bootloader_file, row->falcon_bootloader_file) == 0)
			bootloader_same = 1;

		/* Any field that differs fails the record. */
		if (profile->verified != row->verified ||
		    strcmp(profile->image_file, row->image_file) != 0 ||
		    strcmp(profile->booter_load_file, row->booter_load_file) != 0 ||
		    bootloader_same == 0 ||
		    strcmp(profile->signature_section, row->signature_section) != 0 ||
		    profile->falcon_style != row->falcon_style ||
		    profile->vram_size_register != row->vram_size_register ||
		    profile->vram_size_in_mib != row->vram_size_in_mib ||
		    profile->display_fuse_register != row->display_fuse_register ||
		    profile->heap_os_bytes != (uint64_t)row->heap_os_mib << 20 ||
		    profile->heap_min_bytes != (uint64_t)row->heap_min_mib << 20 ||
		    profile->gr_scrubber != row->gr_scrubber ||
		    memcmp(classes, row->classes, sizeof(classes)) != 0 ||
		    profile->window_storage_pitch != row->window_storage_pitch ||
		    profile->qmd_major != row->qmd_major ||
		    profile->qmd_minor != row->qmd_minor ||
		    profile->sm != row->sm ||
		    profile->warps_per_sm != row->warps_per_sm) {
			printf("FAIL profile %s: a field differs from the expected record\n", row->name);
			failures++;
		}
	}

	/* The count of failed records. */
	return failures;
}

/* Checks the product table: sorted, unique, every product on a chip with a family record. */
static unsigned
check_products(void)
{
	const struct nvrtx_product *product;
	const struct nvrtx_product *found;
	struct nvrtx_chip chip;
	unsigned laptops;
	unsigned previous;
	unsigned failures;
	size_t count;
	size_t index;
	int error;

	/* The table's size against pci.ids. */
	failures = 0U;
	count = nvrtx_product_count();
	if (count != EXPECTED_PRODUCTS) {
		printf("FAIL products: %zu products, expected %u\n", count, EXPECTED_PRODUCTS);
		failures++;
	}

	/* Walks the table in order. */
	laptops = 0U;
	previous = 0U;
	for (index = 0U; index < count; index++) {
		product = nvrtx_product_at(index);

		/* Strictly increasing IDs: sorted for the search and without duplicates. */
		if (index != 0U && product->device <= previous) {
			printf("FAIL products: %04x after %04x\n", product->device, previous);
			failures++;
		}
		previous = product->device;

		/* The search finds the row itself. */
		found = nvrtx_product_find(product->device);
		if (found != product) {
			printf("FAIL products: %04x not found by the search\n", product->device);
			failures++;
		}

		/* The chip is one the identity names with a family record. */
		error = nvrtx_identify(((unsigned)product->chipset << 20) | 0xa1U, 0U, &chip);
		if (error != 0 || chip.profile == NULL) {
			printf("FAIL products: %04x on chip %03x without a family record\n", product->device, product->chipset);
			failures++;
		}

		/* Counts the laptop parts. */
		if (product->laptop != 0U)
			laptops++;
	}

	/* The laptop count against pci.ids. */
	if (laptops != EXPECTED_LAPTOPS) {
		printf("FAIL products: %u laptops, expected %u\n", laptops, EXPECTED_LAPTOPS);
		failures++;
	}

	/* An unlisted ID and an index past the end find nothing. */
	found = nvrtx_product_find(0x0000U);
	if (found != NULL) {
		printf("FAIL products: ID 0000 found\n");
		failures++;
	}
	found = nvrtx_product_at(count);
	if (found != NULL) {
		printf("FAIL products: an index past the end names a product\n");
		failures++;
	}

	/* The count of failed checks. */
	return failures;
}

/* Checks the attach's boot choice on every combination. */
static unsigned
check_choices(void)
{
	struct nvrtx_chip chip;
	enum nvrtx_boot_choice choice;
	unsigned index;
	unsigned failures;

	/* Decides each combination and compares the choice. */
	failures = 0U;
	for (index = 0U; index < sizeof(choices) / sizeof(choices[0]); index++) {
		(void)nvrtx_identify(choices[index].ident0, 0U, &chip);
		choice = nvrtx_boot_choose(chip.profile,
		    choices[index].gop_on_gpu,
		    choices[index].gsp_wanted,
		    choices[index].gsp_forced);
		if (choice != choices[index].choice) {
			printf("FAIL boot choice %s: %d, expected %d\n", choices[index].what, (int)choice, (int)choices[index].choice);
			failures++;
		}
	}

	/* The count of failed combinations. */
	return failures;
}

/* Finds the secure processor's reset bit in a device information table. */
static unsigned
check_device_info(void)
{
	uint32_t words[NVRTX_DEVICE_INFO_WORDS];
	uint32_t bit;
	unsigned failures;
	int error;

	/* Two engines: one of another type, then SEC2 with reset bit 13. */
	failures = 0U;
	memset(words, 0, sizeof(words));
	words[0] = 0x80000001U | (0x5U << 12);
	words[1] = 0x80000002U | 0x4U | (3U << 9);
	words[2] = 0x00000003U | (0x0U << 2);
	words[3] = 0x80000001U;
	words[4] = 0x80000002U | 0x4U | (13U << 9);
	words[5] = 0x00000003U | (0xdU << 2);
	error = nvrtx_device_info_reset(words, NVRTX_ENGINE_SEC2, &bit);
	if (error != 0 || bit != 13U) {
		printf("FAIL device info: SEC2 reset bit\n");
		failures++;
	}

	/* An engine type the table does not list. */
	error = nvrtx_device_info_reset(words, 0x14U, &bit);
	if (error != ENOENT) {
		printf("FAIL device info: absent engine\n");
		failures++;
	}

	/* The count of failed checks. */
	return failures;
}
