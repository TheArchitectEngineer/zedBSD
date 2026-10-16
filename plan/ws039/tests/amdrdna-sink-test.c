/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of ws039-p014's pure parts: the display path lookup in the
 * board's video BIOS (amdrdna_vbios_connector) and the EDID decode
 * (edid.c) on a synthetic EDID with a CTA-861 extension, and a damaged
 * one.
 *
 * usage: amdrdna-sink-test [IMAGE]   (the board's video BIOS; without it the lookup part is skipped)
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/gpu/amdrdna/amdrdna-hw.h"
#include "drivers/gpu/amdrdna/amdrdna-sink.h"

static unsigned checks;
static unsigned failures;

static void
check(int ok, const char *format, ...)
{
	va_list arguments;

	checks++;
	if (ok)
		return;
	failures++;
	printf("FAIL ");
	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
	printf("\n");
}

static void
test_connectors(const char *path)
{
	struct amdrdna_vbios_info info;
	struct amdrdna_vbios_connector connector;
	FILE *file;
	uint8_t *image;
	long size;
	int error;

	file = fopen(path, "rb");
	if (file == NULL) {
		printf("SKIP connectors: no %s\n", path);
		return;
	}
	fseek(file, 0L, SEEK_END);
	size = ftell(file);
	fseek(file, 0L, SEEK_SET);
	image = malloc((size_t)size);
	if (image == NULL || fread(image, 1U, (size_t)size, file) != (size_t)size) {
		fclose(file);
		check(0, "connectors: read the image");
		return;
	}
	fclose(file);
	error = amdrdna_vbios_parse(image, (size_t)size, &info);
	check(error == 0, "connectors: the image parses (%d)", error);

	/* Transmitter E: the first DisplayPort connector, I2C 0x95 on the sixth line's pad, hot-plug pin 6. */
	error = amdrdna_vbios_connector(image, (size_t)size, &info, 4U, &connector);
	check(error == 0 && connector.connector_object == 0x3113U && connector.kind == AMDRDNA_CONNECTOR_DP &&
	    connector.encoder_object == 0x2121U && connector.ddc_identifier == 0x95U && connector.ddc_pad_register == 0x5da5U &&
	    connector.hotplug_pin == 6U, "connectors: transmitter E (%d 0x%x %d 0x%x 0x%x 0x%x %u)", error, connector.connector_object,
	    (int)connector.kind, connector.encoder_object, connector.ddc_identifier, connector.ddc_pad_register, connector.hotplug_pin);

	/* Transmitter A: the HDMI connector on the fourth line's pad, hot-plug pin 4. */
	error = amdrdna_vbios_connector(image, (size_t)size, &info, 0U, &connector);
	check(error == 0 && connector.connector_object == 0x310cU && connector.kind == AMDRDNA_CONNECTOR_HDMI &&
	    connector.ddc_identifier == 0x93U && connector.ddc_pad_register == 0x5d9dU && connector.hotplug_pin == 4U,
	    "connectors: transmitter A (%d 0x%x 0x%x 0x%x)", error, connector.connector_object, connector.ddc_identifier,
	    connector.ddc_pad_register);

	/* Transmitters C and D: the other two DisplayPort connectors; F: none. */
	error = amdrdna_vbios_connector(image, (size_t)size, &info, 2U, &connector);
	check(error == 0 && connector.connector_object == 0x3213U && connector.ddc_identifier == 0x91U, "connectors: transmitter C");
	error = amdrdna_vbios_connector(image, (size_t)size, &info, 3U, &connector);
	check(error == 0 && connector.connector_object == 0x3313U && connector.ddc_identifier == 0x90U, "connectors: transmitter D");
	error = amdrdna_vbios_connector(image, (size_t)size, &info, 5U, &connector);
	check(error == ENOENT && connector.transmitter == AMDRDNA_TRANSMITTER_NONE, "connectors: transmitter F has no path (%d)", error);
	free(image);
}

/* Writes a detailed timing descriptor. */
static void
put_detail(uint8_t *at, uint32_t pixel_khz, unsigned h_active, unsigned h_blank, unsigned h_front, unsigned h_sync,
    unsigned v_active, unsigned v_blank, unsigned v_front, unsigned v_sync, unsigned width_mm, unsigned height_mm)
{
	unsigned clock;

	clock = pixel_khz / 10U;
	at[0] = (uint8_t)clock;
	at[1] = (uint8_t)(clock >> 8);
	at[2] = (uint8_t)h_active;
	at[3] = (uint8_t)h_blank;
	at[4] = (uint8_t)(((h_active >> 8) << 4) | (h_blank >> 8));
	at[5] = (uint8_t)v_active;
	at[6] = (uint8_t)v_blank;
	at[7] = (uint8_t)(((v_active >> 8) << 4) | (v_blank >> 8));
	at[8] = (uint8_t)h_front;
	at[9] = (uint8_t)h_sync;
	at[10] = (uint8_t)(((v_front & 0xfU) << 4) | (v_sync & 0xfU));
	at[11] = (uint8_t)(((h_front >> 8) << 6) | ((h_sync >> 8) << 4) | ((v_front >> 4) << 2) | (v_sync >> 4));
	at[12] = (uint8_t)width_mm;
	at[13] = (uint8_t)height_mm;
	at[14] = (uint8_t)(((width_mm >> 8) << 4) | (height_mm >> 8));
	at[17] = 0x1eU;
}

/* Makes a block's checksum. */
static void
seal(uint8_t *block)
{
	unsigned index;
	uint8_t sum;

	sum = 0U;
	for (index = 0U; index < 127U; index++)
		sum = (uint8_t)(sum + block[index]);
	block[127] = (uint8_t)(0x100U - sum);
}

static void
test_edid(void)
{
	static uint8_t bytes[256];
	struct amdrdna_edid edid;
	const struct amdrdna_timing *mode;
	unsigned index;
	int error;

	/* The base block: "DEL" 0xa0ff, version 1.4, digital, 53x30 cm. */
	memset(bytes, 0, sizeof(bytes));
	memcpy(bytes, "\x00\xff\xff\xff\xff\xff\xff\x00", 8U);
	bytes[8] = (uint8_t)((('D' - 'A' + 1) << 2) | (('E' - 'A' + 1) >> 3));
	bytes[9] = (uint8_t)(((('E' - 'A' + 1) & 7) << 5) | ('L' - 'A' + 1));
	bytes[10] = 0xffU;
	bytes[11] = 0xa0U;
	bytes[12] = 0x78U;
	bytes[13] = 0x56U;
	bytes[14] = 0x34U;
	bytes[15] = 0x12U;
	bytes[18] = 1U;
	bytes[19] = 4U;
	bytes[20] = 0xa5U;
	bytes[21] = 53U;
	bytes[22] = 30U;

	/* Established: 640x480@60, 800x600@60, 1024x768@60, and the interlaced bit (not listed). */
	bytes[35] = 0x21U;
	bytes[36] = 0x18U;

	/* Standard: 1280x1024@60 (5:4), 1600x900@60 (16:9), 1440x900@75 (16:10, not a known timing); the rest unused. */
	for (index = 0U; index < 8U; index++) {
		bytes[38U + index * 2U] = 1U;
		bytes[39U + index * 2U] = 1U;
	}
	bytes[38] = 0x81U;
	bytes[39] = 0x80U;
	bytes[40] = 0xa9U;
	bytes[41] = 0xc0U;
	bytes[42] = 0x95U;
	bytes[43] = 0x0fU;

	/* Descriptors: 1920x1080@60 preferred, the range limits (170 MHz), the name, a dummy. */
	put_detail(bytes + 54, 148500U, 1920U, 280U, 88U, 44U, 1080U, 45U, 4U, 5U, 527U, 296U);
	bytes[72 + 3] = 0xfdU;
	bytes[72 + 9] = 17U;
	bytes[90 + 3] = 0xfcU;
	memcpy(bytes + 90 + 5, "DELL P2419H\n ", 13U);
	bytes[108 + 3] = 0x10U;
	bytes[126] = 1U;
	seal(bytes);

	/* A CTA-861 extension with 1280x720@60 at offset 4 + 0 data blocks. */
	bytes[128] = 0x02U;
	bytes[129] = 3U;
	bytes[130] = 4U;
	put_detail(bytes + 132, 74250U, 1280U, 370U, 110U, 40U, 720U, 30U, 5U, 5U, 527U, 296U);
	seal(bytes + 128);

	error = amdrdna_edid_decode(bytes, sizeof(bytes), &edid);
	check(error == 0 && edid.blocks == 2U && edid.extensions == 1U, "edid: decodes two blocks (%d, %u)", error, edid.blocks);
	check(strcmp(edid.vendor, "DEL") == 0 && edid.product == 0xa0ffU && edid.serial == 0x12345678U && edid.version == 1U &&
	    edid.revision == 4U && edid.digital && edid.width_mm == 530U && edid.height_mm == 300U,
	    "edid: vendor %s product 0x%x serial 0x%x %u.%u size %ux%u", edid.vendor, edid.product, edid.serial, edid.version,
	    edid.revision, edid.width_mm, edid.height_mm);
	check(strcmp(edid.name, "DELL P2419H") == 0 && edid.max_pixel_khz == 170000U, "edid: name '%s', range %u kHz", edid.name,
	    edid.max_pixel_khz);
	mode = &edid.modes[0];
	check(edid.mode_count == 8U, "edid: eight modes (%u)", edid.mode_count);
	check(mode->source == AMDRDNA_TIMING_PREFERRED && mode->width == 1920U && mode->height == 1080U &&
	    mode->pixel_khz == 148500U && mode->h_blanking == 280U && mode->h_porch == 88U && mode->h_pulse == 44U &&
	    mode->v_blanking == 45U && mode->v_porch == 4U && mode->v_pulse == 5U && mode->refresh_millihz == 60000U &&
	    mode->h_positive && mode->v_positive && mode->width_mm == 527U && mode->height_mm == 296U,
	    "edid: the preferred 1920x1080@60 timing (%u %u %u)", mode->width, mode->height, mode->refresh_millihz);
	index = amdrdna_edid_find(&edid, 640U, 480U, 60000U);
	check(index < edid.mode_count && edid.modes[index].pixel_khz == 25175U && edid.modes[index].source == AMDRDNA_TIMING_ESTABLISHED,
	    "edid: 640x480@60 established (%u)", index);
	check(index < edid.mode_count && edid.modes[index].h_porch == 16U && edid.modes[index].h_pulse == 96U &&
	    edid.modes[index].v_porch == 10U && edid.modes[index].v_pulse == 2U && !edid.modes[index].h_positive &&
	    !edid.modes[index].v_positive, "edid: 640x480@60 established with DMT's porches, syncs and negative polarities");
	index = amdrdna_edid_find(&edid, 1280U, 1024U, 0U);
	check(index < edid.mode_count && edid.modes[index].pixel_khz == 108000U && edid.modes[index].source == AMDRDNA_TIMING_STANDARD,
	    "edid: 1280x1024 standard with its clock (%u)", index);
	check(index < edid.mode_count && edid.modes[index].h_porch == 48U && edid.modes[index].h_pulse == 112U &&
	    edid.modes[index].v_porch == 1U && edid.modes[index].v_pulse == 3U && edid.modes[index].h_positive &&
	    edid.modes[index].v_positive, "edid: 1280x1024 standard with DMT's porches, syncs and positive polarities");
	index = amdrdna_edid_find(&edid, 1600U, 900U, 60000U);
	check(index < edid.mode_count && edid.modes[index].pixel_khz == 108000U, "edid: 1600x900@60 standard");
	index = amdrdna_edid_find(&edid, 1440U, 900U, 75000U);
	check(index < edid.mode_count && edid.modes[index].pixel_khz == 0U && edid.modes[index].refresh_millihz == 75000U,
	    "edid: 1440x900@75 listed without a clock");
	index = amdrdna_edid_find(&edid, 1280U, 720U, 60000U);
	check(index < edid.mode_count && edid.modes[index].source == AMDRDNA_TIMING_DETAILED && edid.modes[index].pixel_khz == 74250U,
	    "edid: 1280x720@60 from the extension");
	index = amdrdna_edid_find(&edid, 1920U, 1080U, 0U);
	check(index == 0U, "edid: no refresh asked for gives the preferred mode");
	index = amdrdna_edid_find(&edid, 1920U, 1080U, 50000U);
	check(index == AMDRDNA_EDID_MODES, "edid: no 1920x1080@50");

	/* A damaged extension keeps the base block; a damaged base block fails. */
	bytes[200] ^= 1U;
	error = amdrdna_edid_decode(bytes, sizeof(bytes), &edid);
	check(error == 0 && edid.blocks == 1U && edid.mode_count == 7U, "edid: a damaged extension is dropped (%u, %u)", edid.blocks,
	    edid.mode_count);
	bytes[60] ^= 1U;
	error = amdrdna_edid_decode(bytes, sizeof(bytes), &edid);
	check(error == EINVAL, "edid: a damaged base block fails (%d)", error);
	error = amdrdna_edid_decode(bytes, 100U, &edid);
	check(error == EINVAL, "edid: a short one fails");
}

int
main(int argc, char **argv)
{
	test_connectors(argc > 1 ? argv[1] : "");
	test_edid();
	printf("%s amdrdna sink: %u checks, %u failures\n", failures == 0U ? "PASS" : "FAIL", checks, failures);
	return failures == 0U ? 0 : 1;
}
