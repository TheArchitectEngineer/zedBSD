/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the nvrtx GSP parts that touch no hardware (ws037-p004 4a).
 *
 * usage: nvrtx-gsp-host-test [FIRMWARE-DIR]
 * FIRMWARE-DIR is a verified nvidia-gsp-firmware cache; without it the
 * checks of the real files' containers are skipped.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-gsp.h"

static unsigned failures;
static unsigned checks;

static void
check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

static uint8_t *
read_file(const char *dir, const char *name, size_t *size)
{
	char path[1024];
	FILE *f;
	uint8_t *data;
	long length;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	f = fopen(path, "rb");
	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	length = ftell(f);
	fseek(f, 0, SEEK_SET);
	data = malloc((size_t)length);
	if (data == NULL || fread(data, 1, (size_t)length, f) != (size_t)length) {
		fclose(f);
		free(data);
		return NULL;
	}
	fclose(f);
	*size = (size_t)length;
	return data;
}

/* Chunked pages with made-up bus addresses: chunk c at 0x40000000 + c * 2 * chunk_bytes. */
static void
make_pages(struct nvrtx_pages *pages, size_t page_count, size_t chunk_bytes)
{
	size_t c;

	pages->chunk_bytes = chunk_bytes;
	pages->page_count = page_count;
	pages->chunk_count = (page_count * NVRTX_GSP_PAGE_SIZE + chunk_bytes - 1) / chunk_bytes;
	pages->chunk_cpu = calloc(pages->chunk_count, sizeof(uint8_t *));
	pages->chunk_bus = calloc(pages->chunk_count, sizeof(uint64_t));
	for (c = 0; c < pages->chunk_count; c++) {
		pages->chunk_cpu[c] = aligned_alloc(NVRTX_GSP_PAGE_SIZE, chunk_bytes);
		memset(pages->chunk_cpu[c], 0xa5, chunk_bytes);
		pages->chunk_bus[c] = 0x40000000ULL + (uint64_t)c * 2U * chunk_bytes;
	}
}

static void
test_wire(void)
{
	uint8_t b[16];
	uint8_t element[2 * NVRTX_GSP_PAGE_SIZE];
	unsigned i;

	nvrtx_put64(b, 0, 0x1122334455667788ULL);
	check(b[0] == 0x88 && b[7] == 0x11, "wire: put64 little-endian");
	check(nvrtx_get64(b, 0) == 0x1122334455667788ULL, "wire: get64");
	nvrtx_put32(b, 8, 0xdeadbeefU);
	check(nvrtx_get32(b, 8) == 0xdeadbeefU, "wire: get32");

	for (i = 0; i < sizeof(element); i++)
		element[i] = (uint8_t)(i * 7U + 3U);
	nvrtx_put32(element, NVRTX_ELEMENT_CHECKSUM_AT, 0);
	nvrtx_put32(element, NVRTX_ELEMENT_CHECKSUM_AT, nvrtx_fold_checksum(element, sizeof(element)));
	check(nvrtx_fold_checksum(element, sizeof(element)) == 0, "wire: finished element folds to zero");
}

static void
test_layout(void)
{
	struct nvrtx_wpr_layout l;
	struct nvrtx_bootloader bl;
	struct nvrtx_wpr_sources src;
	uint8_t record[NVRTX_WPRM_BYTES];
	uint8_t page[NVRTX_GSP_PAGE_SIZE];
	uint8_t args[NVRTX_RMARGS_BYTES];
	uint8_t log[NVRTX_GSP_LOG_BYTES];
	uint64_t vram;
	int error;

	vram = nvrtx_vram_size((8U << 4) | 10U);
	check(vram == 8ULL << 30, "layout: VRAM size 8 GiB decoded");
	check(nvrtx_vram_size(0x40000000U | (8U << 4) | 10U) == (8ULL << 30) / 16 * 15, "layout: fifteen sixteenths");
	check(nvrtx_vga_workspace(vram, 0) == vram - 0x100000, "layout: invalid VGA register gives the last MiB");
	check(nvrtx_vga_workspace(vram, 0x01fff008U) == 0x1fff00000ULL, "layout: valid VGA register address");
	check(nvrtx_vga_workspace(vram, 0x00100008U) == vram - 0x20000, "layout: low VGA address gives the last 128 KiB");

	error = nvrtx_wpr_layout(vram, vram - 0x100000, 0x1000, 0x1b34ea0, &l);
	check(error == 0, "layout: 8 GiB fits");
	check(l.vga == 0x1fff00000ULL, "layout: VGA workspace");
	check(l.frts == 0x1ffe00000ULL && l.frts_size == 0x100000, "layout: FRTS");
	check(l.boot == 0x1ffdff000ULL, "layout: bootloader");
	check(l.image == 0x1fe2c0000ULL, "layout: image");
	check(l.heap == 0x1f7900000ULL && l.heap_size == 0x6900000ULL, "layout: heap 105 MiB");
	check(l.region == 0x1f7800000ULL && l.region_size == 0x8700000ULL, "layout: region 135 MiB");
	check(l.outer_heap == 0x1f7700000ULL && l.outer_heap_size == 0x100000, "layout: outer heap");
	check(nvrtx_wpr_layout(vram, vram - 0x100000, 0x1000, 0x40000000ULL, &l) == 0, "layout: (large image still computes)");
	check(nvrtx_wpr_layout(64ULL << 20, (64ULL << 20) - 0x100000, 0x1000, 0x1b34ea0, &l) == EINVAL, "layout: 64 MiB VRAM refused");

	nvrtx_wpr_layout(vram, vram - 0x100000, 0x1000, 0x1b34ea0, &l);
	memset(&bl, 0, sizeof(bl));
	bl.monitor_code_at = 0x11;
	bl.monitor_data_at = 0x22;
	bl.manifest_at = 0x33;
	src.image_table = 0x1000;
	src.boot_address = 0x2000;
	src.boot_size = 0x1000;
	src.signature = 0x3000;
	src.signature_size = 0x1000;
	src.bootloader = &bl;
	nvrtx_wpr_record(record, &l, &src);
	check(nvrtx_get64(record, 0) == 0xdc3aae21371a60b3ULL, "record: magic");
	check(nvrtx_get64(record, 8) == 1, "record: revision");
	check(nvrtx_get64(record, 24) == 0x1b34ea0, "record: image size");
	check(nvrtx_get64(record, 48) == 0x11 && nvrtx_get64(record, 56) == 0x22 && nvrtx_get64(record, 64) == 0x33, "record: bootloader offsets");
	check(nvrtx_get64(record, 88) == 0x1f7700000ULL && nvrtx_get64(record, 96) == 0x1f7700000ULL, "record: reserved start and outer heap");
	check(nvrtx_get64(record, 112) == 0x1f7800000ULL, "record: region start");
	check(nvrtx_get64(record, 168) == 0x1fff00000ULL, "record: region end");
	check(nvrtx_get64(record, 176) == vram, "record: VRAM size");
	check(nvrtx_get64(record, 192) == 0x100000, "record: VGA size");
	check(nvrtx_get64(record, 248) == 0, "record: tail zero");

	memset(page, 0, sizeof(page));
	nvrtx_libos_region(page, 0, "LOGINIT", 0x5000, 0x10000);
	nvrtx_libos_region(page, 3, "RMARGS", 0x6000, NVRTX_RMARGS_BYTES);
	check(nvrtx_get64(page, 0) == 0x4c4f47494e4954ULL, "libos: name packed first character highest");
	check(nvrtx_get64(page, 8) == 0x5000 && nvrtx_get64(page, 16) == 0x10000, "libos: address and size");
	check(page[24] == 1 && page[25] == 1, "libos: contiguous in system memory");
	check(nvrtx_get64(page, 96) == 0x524d41524753ULL, "libos: fourth entry at 96");

	nvrtx_rmargs(args, 0x7000, 129);
	check(nvrtx_get64(args, 0) == 0x7000 && nvrtx_get32(args, 8) == 129, "rmargs: queue pages");
	check(nvrtx_get64(args, 16) == 0x1000 && nvrtx_get64(args, 24) == 0x41000, "rmargs: queue offsets");
	check(args[48] == 1, "rmargs: stack in DMEM");
	check(nvrtx_get32(args, 32) == 0 && args[40] == 0, "rmargs: cold start");

	nvrtx_log_buffer(log, 0x80000, sizeof(log));
	check(nvrtx_get64(log, 0) == 0 && nvrtx_get64(log, 8) == 0x80000 && nvrtx_get64(log, 8 + 15 * 8) == 0x8f000, "log: put pointer and page list");
}

static void
test_radix3(void)
{
	struct nvrtx_pages image;
	struct nvrtx_pages table;
	size_t pages;
	int error;

	make_pages(&image, 6965, 0x100000);
	pages = nvrtx_radix3_pages(6965);
	check(pages == 16, "radix3: 6965 image pages need 16 table pages");
	make_pages(&table, pages, pages * NVRTX_GSP_PAGE_SIZE);
	error = nvrtx_radix3_fill(&image, &table);
	check(error == 0, "radix3: fill");
	check(nvrtx_get64(nvrtx_pages_cpu(&table, 0), 0) == nvrtx_pages_bus(&table, 1), "radix3: top names middle");
	check(nvrtx_get64(nvrtx_pages_cpu(&table, 0), 8) == 0, "radix3: top rest zero");
	check(nvrtx_get64(nvrtx_pages_cpu(&table, 1), 13 * 8) == nvrtx_pages_bus(&table, 15), "radix3: middle names last bottom");
	check(nvrtx_get64(nvrtx_pages_cpu(&table, 1), 14 * 8) == 0, "radix3: middle rest zero");
	check(nvrtx_get64(nvrtx_pages_cpu(&table, 2), 0) == 0x40000000ULL, "radix3: first image page");
	check(nvrtx_get64(nvrtx_pages_cpu(&table, 2), 256 * 8) == 0x40000000ULL + 2 * 0x100000, "radix3: second chunk");
	check(nvrtx_get64(nvrtx_pages_cpu(&table, 15), (6964 % 512) * 8) == nvrtx_pages_bus(&image, 6964), "radix3: last image page");
	check(nvrtx_get64(nvrtx_pages_cpu(&table, 15), (6965 % 512) * 8) == 0, "radix3: tail zero");
	table.page_count = 15;
	check(nvrtx_radix3_fill(&image, &table) == EINVAL, "radix3: wrong table size refused");
}

static void
test_queue(void)
{
	struct nvrtx_pages shared;
	struct nvrtx_channel driver;
	struct nvrtx_channel gsp;
	uint8_t *staging_driver;
	uint8_t *staging_gsp;
	uint8_t payload[20000];
	uint8_t received[20000];
	uint8_t *slot;
	uint32_t function;
	uint32_t sequence;
	uint32_t result;
	size_t length;
	unsigned i;
	unsigned round;
	int error;
	int ok;

	make_pages(&shared, NVRTX_QUEUE_SHARED_BYTES / NVRTX_GSP_PAGE_SIZE, NVRTX_QUEUE_SHARED_BYTES);
	check(shared.page_count == 129, "queue: shared region is 129 pages");
	nvrtx_queue_shared_init(&shared);
	check(nvrtx_get64(nvrtx_pages_cpu(&shared, 0), 128 * 8) == nvrtx_pages_bus(&shared, 128), "queue: page list");
	slot = nvrtx_pages_cpu(&shared, 1);
	check(nvrtx_get32(slot, 4) == 0x40000 && nvrtx_get32(slot, 8) == 0x1000 && nvrtx_get32(slot, 12) == 63, "queue: command header size, slot, count");
	check(nvrtx_get32(slot, 20) == 1 && nvrtx_get32(slot, 24) == 32 && nvrtx_get32(slot, 28) == 0x1000, "queue: command header flags, reader, slots");

	staging_driver = malloc(NVRTX_STAGING_BYTES);
	staging_gsp = malloc(NVRTX_STAGING_BYTES);
	nvrtx_channel_init(&driver, &shared, staging_driver);
	/* The GSP's mirror: it sends in the status queue and receives from the command queue. */
	nvrtx_channel_init(&gsp, &shared, staging_gsp);
	gsp.command.at = NVRTX_QUEUE_STATUS_AT;
	gsp.command.peer_at = NVRTX_QUEUE_COMMAND_AT;
	gsp.status.at = NVRTX_QUEUE_COMMAND_AT;
	gsp.status.peer_at = NVRTX_QUEUE_STATUS_AT;

	for (i = 0; i < sizeof(payload); i++)
		payload[i] = (uint8_t)(i * 13U + 1U);

	error = nvrtx_channel_send(&driver, 72, 0, payload, 100);
	check(error == 0, "queue: send one slot");
	slot = nvrtx_pages_cpu(&shared, 2);
	check(nvrtx_fold_checksum(slot, NVRTX_GSP_PAGE_SIZE) == 0, "queue: element checksum folds to zero");
	check(nvrtx_get32(slot, 40) == 1 && nvrtx_get32(slot, 36) == 0, "queue: element slots and number");
	check(nvrtx_get32(slot, 48) == 0x03000000 && memcmp(slot + 52, "VRPC", 4) == 0, "queue: call version and signature");
	check(nvrtx_get32(slot, 56) == 132 && nvrtx_get32(slot, 60) == 72, "queue: call length and function");
	check(nvrtx_get32(slot, 64) == 0xffffffffU, "queue: result pending");
	check(nvrtx_get32(nvrtx_pages_cpu(&shared, 1), 16) == 1, "queue: write pointer moved");

	error = nvrtx_channel_receive(&gsp, received, sizeof(received), &length);
	check(error == 0 && length == 100 && memcmp(received, payload, 100) == 0, "queue: GSP side receives");
	check(nvrtx_get32(nvrtx_pages_cpu(&shared, 65), 32) == 1, "queue: GSP read pointer in the status page");

	check(nvrtx_channel_send(&driver, 1, 1, payload, NVRTX_CALL_PAYLOAD_MAX + 1) == E2BIG, "queue: too large refused");

	/* Fills the command queue without the GSP reading: 62 slots usable. */
	ok = 1;
	for (i = 0; i < 62; i++) {
		if (nvrtx_channel_send(&driver, 76, i, payload, 10) != 0)
			ok = 0;
	}
	check(ok, "queue: 62 one-slot messages fit");
	check(nvrtx_channel_send(&driver, 76, 99, payload, 10) == EAGAIN, "queue: full queue says EAGAIN");
	ok = 1;
	for (i = 0; i < 62; i++) {
		error = nvrtx_channel_peek(&gsp, &function, &sequence, &length, &result);
		if (error != 0 || sequence != i || function != 76)
			ok = 0;
		nvrtx_channel_receive(&gsp, received, sizeof(received), &length);
	}
	check(ok, "queue: GSP drains in order across the wrap");

	/* Multi-slot replies that wrap, answered by the GSP into the status queue. */
	ok = 1;
	for (round = 0; round < 20; round++) {
		error = nvrtx_channel_send(&gsp, 0x1000 + round, round, payload, 9000 + round * 400);
		if (error != 0)
			ok = 0;
		error = nvrtx_channel_peek(&driver, &function, &sequence, &length, &result);
		if (error != 0 || function != 0x1000 + round || sequence != round || length != 9000 + round * 400)
			ok = 0;
		memset(received, 0, sizeof(received));
		error = nvrtx_channel_receive(&driver, received, sizeof(received), &length);
		if (error != 0 || memcmp(received, payload, length) != 0)
			ok = 0;
	}
	check(ok, "queue: multi-slot messages wrap and arrive whole");
	check(nvrtx_channel_peek(&driver, &function, &sequence, &length, &result) == EAGAIN, "queue: empty status queue says EAGAIN");

	error = nvrtx_channel_send(&gsp, 5, 5, payload, 5000);
	error = nvrtx_channel_receive(&driver, received, 100, &length);
	check(error == 0 && length == 5000 && memcmp(received, payload, 100) == 0, "queue: a short buffer takes the head and the message");
}

static void
test_files(const char *dir)
{
	struct nvrtx_booter booter;
	struct nvrtx_bootloader bl;
	struct nvrtx_section section;
	uint8_t *data;
	size_t size;
	size_t at;
	int error;

	data = read_file(dir, "booter_load-570.144.bin", &size);
	check(data != NULL, "files: booter_load present");
	if (data == NULL)
		return;
	error = nvrtx_booter_parse(data, size, &booter);
	check(error == 0, "files: booter_load parses");
	check(booter.payload_at == 0x88 && booter.payload_size == 0xe700, "files: booter_load payload");
	check(booter.os_code_at == 0 && booter.os_code_size == 0x100, "files: booter_load code");
	check(booter.os_data_at == 0x8500 && booter.os_data_size == 0x6200, "files: booter_load data");
	check(booter.app_at == 0x100 && booter.app_size == 0x8400, "files: booter_load application");
	check(booter.signature_count >= 1 && booter.signature_size > 0, "files: booter_load signatures");
	check(nvrtx_booter_signature(&booter, 0, &at) == 0 && at + booter.signature_size <= size, "files: production signature in the file");
	printf("info booter_load: %u signatures of %zu bytes, patch at payload +0x%x\n",
	    booter.signature_count, booter.signature_size, booter.patch_location);
	check(nvrtx_booter_parse(data, 100, &booter) == EINVAL, "files: truncated booter refused");
	free(data);

	data = read_file(dir, "booter_unload-570.144.bin", &size);
	check(data != NULL && nvrtx_booter_parse(data, size, &booter) == 0, "files: booter_unload parses");
	free(data);

	data = read_file(dir, "bootloader-570.144.bin", &size);
	check(data != NULL && nvrtx_bootloader_parse(data, size, &bl) == 0, "files: bootloader parses");
	check(bl.payload_at == 0x64 && bl.payload_size == 0x1000, "files: bootloader payload");
	free(data);

	data = read_file(dir, "bl.bin", &size);
	{
		struct nvrtx_falcon_bl fbl;

		check(data != NULL && nvrtx_falcon_bl_parse(data, size, &fbl) == 0, "files: falcon bootloader parses");
		check(fbl.start_tag == 0xfd && fbl.code_size == 0x200 && fbl.code == data + 0x200, "files: falcon bootloader start tag and code");
	}
	free(data);

	data = read_file(dir, "gsp-570.144.bin", &size);
	check(data != NULL, "files: gsp present");
	if (data == NULL)
		return;
	error = nvrtx_elf_section(data, size, NVRTX_GSP_IMAGE_SECTION, &section);
	check(error == 0 && section.size == 0x1b34ea0, "files: .fwimage");
	error = nvrtx_elf_section(data, size, NVRTX_GSP_SIGNATURE_SECTION, &section);
	check(error == 0 && section.size == 0x1000, "files: .fwsignature_tu10x");
	check(nvrtx_elf_section(data, size, ".nosuch", &section) == ENOENT, "files: missing section");
	check(nvrtx_elf_section(data, 63, NVRTX_GSP_IMAGE_SECTION, &section) == EINVAL, "files: truncated ELF refused");
	free(data);
}

static void
put_image(uint8_t *rom, size_t base, unsigned units, unsigned type, int last)
{
	rom[base] = 0x55;
	rom[base + 1] = 0xaa;
	rom[base + 0x18] = 0x40;
	memcpy(rom + base + 0x40, "PCIR", 4);
	rom[base + 0x40 + 0x0a] = 0x18;
	rom[base + 0x40 + 0x10] = (uint8_t)units;
	rom[base + 0x40 + 0x14] = (uint8_t)type;
	memcpy(rom + base + 0x60, "NPDE", 4);
	rom[base + 0x60 + 8] = (uint8_t)units;
	rom[base + 0x60 + 0x0a] = last ? 0x80 : 0;
}

static void
test_vbios(void)
{
	static uint8_t rom[0x1600];
	struct nvrtx_vbios vbios;
	struct nvrtx_fwsec fwsec;
	uint8_t program[0x200];
	unsigned sum;
	unsigned i;
	int error;

	memset(rom, 0, sizeof(rom));
	put_image(rom, 0, 2, 0x00, 0);
	memcpy(rom + 0x100, "\xff\xb8" "BIT", 5);
	rom[0x109] = 6;
	rom[0x10a] = 2;
	rom[0x10c] = 'x';
	rom[0x112] = 'p';
	rom[0x113] = 2;
	rom[0x114] = 4;
	rom[0x116] = 0x00;
	rom[0x117] = 0x02;
	nvrtx_put32(rom, 0x200, 0x500);
	sum = 0;
	for (i = 0; i < 0x400; i++)
		sum += rom[i];
	rom[0x3ff] = (uint8_t)(0x100 - (sum & 0xff));
	put_image(rom, 0x400, 1, 0x03, 0);
	put_image(rom, 0x600, 8, 0xe0, 1);
	rom[0x700] = 1;
	rom[0x701] = 4;
	rom[0x702] = 6;
	rom[0x703] = 2;
	rom[0x704] = 0x10;
	rom[0x70a] = 0x85;
	nvrtx_put32(rom, 0x70c, 0x600);
	nvrtx_put32(rom, 0x800, 1U | (2U << 8) | (0x40U << 16));
	nvrtx_put32(rom, 0x800 + 16, 0x10);
	nvrtx_put32(rom, 0x800 + 20, 0x0);
	nvrtx_put32(rom, 0x800 + 24, 0x100);
	nvrtx_put32(rom, 0x800 + 32, 0x80);
	nvrtx_put32(rom, 0x800 + 36, 0x80);
	nvrtx_put32(rom, 0x800 + 40, 0x100);
	nvrtx_put32(rom, 0x800 + 44, 0x0);
	nvrtx_put32(rom, 0x800 + 48, 0x100);
	rom[0x940 + 0x10] = 1;
	rom[0x940 + 0x11] = 4;
	rom[0x940 + 0x12] = 8;
	rom[0x940 + 0x13] = 2;
	nvrtx_put32(rom, 0x940 + 0x14, 3);
	nvrtx_put32(rom, 0x940 + 0x1c, 4);
	nvrtx_put32(rom, 0x940 + 0x20, 0x40);
	nvrtx_put32(rom, 0x940 + 0x40 + 8, 0x80);

	error = nvrtx_vbios_walk(rom, sizeof(rom), &vbios);
	check(error == 0, "vbios: chain walks");
	check(vbios.image0_size == 0x400 && vbios.falcon_image == 0x600 && vbios.end == 0x1600, "vbios: first image, falcon image, end");
	error = nvrtx_vbios_fwsec(&vbios, &fwsec);
	check(error == 0, "vbios: FWSEC found through BIT and the falcon table");
	check(fwsec.version == 2 && fwsec.program_at == 0x840 && fwsec.program_size == 0x200, "vbios: FWSEC program place");
	check(fwsec.imem_size == 0x100 && fwsec.imem_secure_size == 0x80 && fwsec.dmem_at == 0x100 && fwsec.dmem_size == 0x100, "vbios: FWSEC descriptor");

	memcpy(program, rom + fwsec.program_at, sizeof(program));
	error = nvrtx_fwsec_command(program, &fwsec, NVRTX_FWSEC_FRTS, 0x1ffe00000ULL, 0x100000);
	check(error == 0, "fwsec: FRTS command written");
	check(nvrtx_get32(program, 0x100 + 0x40 + 44) == 0x15, "fwsec: command word");
	check(nvrtx_get32(program, 0x180) == 1 && nvrtx_get32(program, 0x184) == 24 && nvrtx_get32(program, 0x194) == 2, "fwsec: read request");
	check(nvrtx_get32(program, 0x180 + 24) == 1 && nvrtx_get32(program, 0x180 + 28) == 20, "fwsec: region header");
	check(nvrtx_get32(program, 0x180 + 32) == 0x1ffe00 && nvrtx_get32(program, 0x180 + 36) == 0x100 && nvrtx_get32(program, 0x180 + 40) == 2, "fwsec: region in 4 KiB units of the VRAM");

	rom[0x3ff] ^= 1;
	check(nvrtx_vbios_walk(rom, sizeof(rom), &vbios) == EINVAL, "vbios: bad checksum refused");
	rom[0x3ff] ^= 1;
	nvrtx_put32(rom, 0x800, 1U | (3U << 8) | (0x40U << 16));
	nvrtx_vbios_walk(rom, sizeof(rom), &vbios);
	check(nvrtx_vbios_fwsec(&vbios, &fwsec) == ENOTSUP, "vbios: descriptor version 3 refused");
}

static void
test_records(void)
{
	struct nvrtx_sysinfo_values v;
	struct nvrtx_seq_command c;
	uint8_t record[NVRTX_SYSINFO_BYTES];
	uint8_t registry[512];
	uint8_t seq[40 + 13 * 4];
	uint32_t cursor;
	size_t length;
	int error;

	memset(&v, 0, sizeof(v));
	v.registers = 0xf6000000ULL;
	v.vram = 0xe0000000ULL;
	v.instance = 0xf0000000ULL;
	v.bus_device_function = 0x0100;
	v.user_address_limit = 0x00007ffffffff000ULL;
	v.vendor = 0x10de;
	v.device = 0x1f02;
	v.subvendor = 0x1458;
	v.subdevice = 0x37c2;
	v.revision = 0xa1;
	v.primary = 1;
	nvrtx_sysinfo(record, &v);
	check(nvrtx_get64(record, 0) == 0xf6000000ULL && nvrtx_get64(record, 8) == 0xe0000000ULL && nvrtx_get64(record, 16) == 0xf0000000ULL, "sysinfo: windows");
	check(nvrtx_get64(record, 32) == 0x100 && nvrtx_get64(record, 72) == 0x00007ffffffff000ULL, "sysinfo: BDF and user limit");
	check(nvrtx_get32(record, 80) == 0x88000 && nvrtx_get32(record, 84) == 0x1000, "sysinfo: config mirror");
	check(nvrtx_get32(record, 88) == 0x1f0210de && nvrtx_get32(record, 92) == 0x37c21458 && nvrtx_get32(record, 96) == 0xa1, "sysinfo: PCI identity");
	check(record[896] == 1 && record[908] == 0 && nvrtx_get64(record, 920) == 0, "sysinfo: primary, no preserved memory");

	error = nvrtx_registry(registry, sizeof(registry), &length);
	check(error == 0 && length == 8 + 3 * 16 + 20 + 22 + 19, "registry: length");
	check(nvrtx_get32(registry, 0) == length && nvrtx_get32(registry, 4) == 3, "registry: header");
	check(nvrtx_get32(registry, 8) == 56 && registry[12] == 1 && nvrtx_get32(registry, 16) == 1 && nvrtx_get32(registry, 20) == 4, "registry: first entry");
	check(memcmp(registry + 56, "RMSecBusResetEnable", 20) == 0, "registry: first name");
	check(nvrtx_get32(registry, 40) == 56 + 20 + 22 && memcmp(registry + 98, "RMDevidCheckIgnore", 19) == 0, "registry: third entry");
	check(nvrtx_registry(registry, 60, &length) == E2BIG, "registry: small buffer refused");

	memset(seq, 0, sizeof(seq));
	nvrtx_put32(seq, 4, 11);
	nvrtx_put32(seq, 40, NVRTX_SEQ_WRITE);
	nvrtx_put32(seq, 44, 0x1234);
	nvrtx_put32(seq, 48, 0x5678);
	nvrtx_put32(seq, 52, NVRTX_SEQ_STORE);
	nvrtx_put32(seq, 56, 0x9abc);
	nvrtx_put32(seq, 60, 3);
	nvrtx_put32(seq, 64, NVRTX_SEQ_CORE_RESET);
	nvrtx_put32(seq, 68, NVRTX_SEQ_DELAY);
	nvrtx_put32(seq, 72, 50);
	nvrtx_put32(seq, 76, NVRTX_SEQ_CORE_START);
	nvrtx_put32(seq, 80, NVRTX_SEQ_CORE_WAIT_HALT);
	cursor = 0;
	check(nvrtx_sequencer_next(seq, sizeof(seq), &cursor, &c) == 0 && c.opcode == 0 && c.argument[0] == 0x1234 && c.argument[1] == 0x5678 && cursor == 3, "sequencer: write");
	check(nvrtx_sequencer_next(seq, sizeof(seq), &cursor, &c) == 0 && c.opcode == 4 && c.argument[1] == 3 && cursor == 6, "sequencer: store");
	check(nvrtx_sequencer_save(seq, sizeof(seq), 3, 0xfeed) == 0 && nvrtx_get32(seq, 8 + 12) == 0xfeed, "sequencer: save slot");
	check(nvrtx_sequencer_next(seq, sizeof(seq), &cursor, &c) == 0 && c.opcode == 5 && cursor == 7, "sequencer: core reset");
	check(nvrtx_sequencer_next(seq, sizeof(seq), &cursor, &c) == 0 && c.opcode == 3 && c.argument[0] == 50 && cursor == 9, "sequencer: delay");
	check(nvrtx_sequencer_next(seq, sizeof(seq), &cursor, &c) == 0 && cursor == 10, "sequencer: core start");
	check(nvrtx_sequencer_next(seq, sizeof(seq), &cursor, &c) == 0 && cursor == 11, "sequencer: wait halt");
	check(nvrtx_sequencer_next(seq, sizeof(seq), &cursor, &c) == ENOENT, "sequencer: end");
	{
		struct nvrtx_object o = { 0xc0de0001U, 0xc0de0001U, 0xc0de0080U, 0x80U };
		uint8_t params[56];
		uint8_t call[200];
		size_t n;

		memset(params, 0x5a, sizeof(params));
		n = nvrtx_alloc_call(call, sizeof(call), &o, params, sizeof(params));
		check(n == 100 && nvrtx_get32(call, 0) == 0xc0de0001U && nvrtx_get32(call, 8) == 0xc0de0080U && nvrtx_get32(call, 12) == 0x80, "alloc: header handles and class");
		check(nvrtx_get32(call, 16) == 0 && nvrtx_get32(call, 20) == 56 && nvrtx_get32(call, 24) == 0 && call[44] == 0x5a && call[99] == 0x5a, "alloc: status, length, parameters");
		check(nvrtx_alloc_call(call, 60, &o, params, sizeof(params)) == 0, "alloc: small buffer refused");
	}
	nvrtx_put32(seq, 40, 99);
	cursor = 0;
	check(nvrtx_sequencer_next(seq, sizeof(seq), &cursor, &c) == EINVAL, "sequencer: unknown opcode");
	check(nvrtx_sequencer_save(seq, sizeof(seq), 8, 0) == EINVAL, "sequencer: save slot range");
}

int
main(int argc, char **argv)
{
	test_wire();
	test_layout();
	test_radix3();
	test_queue();
	test_vbios();
	test_records();
	if (argc > 1)
		test_files(argv[1]);
	else
		printf("skip files: no firmware directory given\n");

	if (failures != 0) {
		printf("FAIL nvrtx gsp: %u of %u checks\n", failures, checks);
		return 1;
	}
	printf("PASS nvrtx gsp: %u checks\n", checks);
	return 0;
}
