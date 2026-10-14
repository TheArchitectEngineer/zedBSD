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

#include "drivers/gpu/nvrtx/nvrtx-disp.h"
#include "drivers/gpu/nvrtx/nvrtx-gsp.h"
#include "drivers/gpu/nvrtx/nvrtx-mem.h"

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
	nvrtx_libos_region(page, 3, "RMARGS", 0x6000, NVRTX_RMARGS_REGION_BYTES);
	check(nvrtx_get64(page, 0) == 0x4c4f47494e4954ULL, "libos: name packed first character highest");
	check(nvrtx_get64(page, 8) == 0x5000 && nvrtx_get64(page, 16) == 0x10000, "libos: address and size");
	check(page[24] == 1 && page[25] == 1, "libos: contiguous in system memory");
	check(nvrtx_get64(page, 96) == 0x524d41524753ULL, "libos: fourth entry at 96");
	check(nvrtx_get64(page, 112) == 0x1000, "libos: the arguments' region is one whole page (R3)");
	check(NVRTX_RMARGS_BYTES <= NVRTX_RMARGS_REGION_BYTES, "libos: the arguments fit their region");

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
		check(n == 88 && nvrtx_get32(call, 0) == 0xc0de0001U && nvrtx_get32(call, 8) == 0xc0de0080U && nvrtx_get32(call, 12) == 0x80, "alloc: header handles and class");
		check(nvrtx_get32(call, 16) == 0 && nvrtx_get32(call, 20) == 56 && nvrtx_get32(call, 24) == 0 && nvrtx_get32(call, 28) == 0, "alloc: status, length, flags, reserved");
		check(call[31] == 0 && call[32] == 0x5a && call[87] == 0x5a, "alloc: parameters right after the 32-byte header (R5)");
		check(nvrtx_alloc_call(call, 60, &o, params, sizeof(params)) == 0, "alloc: small buffer refused");
	}
	nvrtx_put32(seq, 40, 99);
	cursor = 0;
	check(nvrtx_sequencer_next(seq, sizeof(seq), &cursor, &c) == EINVAL, "sequencer: unknown opcode");
	check(nvrtx_sequencer_save(seq, sizeof(seq), 8, 0) == EINVAL, "sequencer: save slot range");
}

static void
test_mmu(void)
{
	static uint8_t image[NVRTX_MMU_TABLES_BYTES];
	static uint8_t inst[NVRTX_INSTANCE_BYTES];
	static uint8_t info[NVRTX_STATIC_INFO_BYTES];
	uint64_t pages[3] = { 0x10000000ULL, 0x10001000ULL, 0x2345f000ULL };
	uint64_t block;
	uint64_t va;
	uint32_t region;
	size_t r;

	block = 0x1f6f00000ULL;
	va = 0x20000000ULL;
	check(nvrtx_mmu_tables(image, block, va, pages, 3) == 0, "mmu: tables built");
	check(nvrtx_get64(image, 0) == (((block + 0x1000) >> 4) | 2) && nvrtx_get64(image, 8) == 0, "mmu: root entry 0 to dir2 in VRAM");
	check(nvrtx_get64(image, 0x1000) == (((block + 0x2000) >> 4) | 2), "mmu: dir2 entry 0");
	check(nvrtx_get64(image, 0x2000 + 8) == (((block + 0x3000) >> 4) | 2) && nvrtx_get64(image, 0x2000) == 0, "mmu: dir1 entry 1 (VA 512 MiB)");
	check(nvrtx_get64(image, 0x3000) == 0 && nvrtx_get64(image, 0x3008) == (((block + 0x4000) >> 4) | 2), "mmu: dual entry, small half");
	check(nvrtx_get64(image, 0x4000) == ((0x10000000ULL >> 4) | 0xd) && nvrtx_get64(image, 0x4010) == ((0x2345f000ULL >> 4) | 0xd), "mmu: page entries valid, system coherent, volatile");
	check(nvrtx_get64(image, 0x4018) == 0, "mmu: unused page entry invalid");
	check(nvrtx_mmu_tables(image, block, va + 0x1000, pages, 3) == EINVAL, "mmu: unaligned range refused");

	memset(inst, 0, sizeof(inst));
	nvrtx_mmu_instance(inst, block, 1ULL << 47);
	check(nvrtx_get64(inst, 0x200) == (block | 0xc00) && nvrtx_get64(inst, 0x208) == (1ULL << 47) - 1, "inst: root and limit");
	check(nvrtx_get32(inst, 0x2a0) == (uint32_t)(block | 0xc00) && nvrtx_get32(inst, 0x2a4) == (uint32_t)((block | 0xc00) >> 32), "inst: subcontext 0");
	check(nvrtx_get32(inst, 0x2b0) == 1 && nvrtx_get32(inst, 0x2b4) == 1 && nvrtx_get32(inst, 0x298) == 1, "inst: other subcontexts invalid, mask");

	memset(info, 0, sizeof(info));
	nvrtx_put32(info, 344, 3);
	r = 344 + 8;
	nvrtx_put64(info, r, 0);
	nvrtx_put64(info, r + 8, 0x0fffffffULL);
	info[r + 28] = 1;
	info[r + 29] = 1;
	r += 48;
	nvrtx_put64(info, r, 0x10000000ULL);
	nvrtx_put64(info, r + 8, 0x1f76fffffULL);
	info[r + 28] = 1;
	info[r + 29] = 1;
	r += 48;
	nvrtx_put64(info, r, 0x1f7700000ULL);
	nvrtx_put64(info, r + 8, 0x1ffffffffULL);
	nvrtx_put64(info, r + 16, 1);
	check(nvrtx_mmu_vram_block(info, 0x10000, &block, &region) == 0 && region == 1 && block == 0x1f7600000ULL, "vram: top of the last usable region, 1 MiB aligned");
	info[344 + 8 + 48 + 30] = 1;
	check(nvrtx_mmu_vram_block(info, 0x10000, &block, &region) == 0 && region == 0 && block == 0x0ff00000ULL, "vram: protected region passed over");
	nvrtx_put32(info, 344, 0);
	check(nvrtx_mmu_vram_block(info, 0x10000, &block, &region) == ENOSPC, "vram: no region");
}

/* P4m: the range allocator, the usable region's bounds and the VRAM page entry. */
static void
test_heap(void)
{
	static uint8_t info[NVRTX_STATIC_INFO_BYTES];
	static struct nvrtx_heap heap;
	uint64_t a;
	uint64_t b;
	uint64_t c;
	uint64_t base;
	uint64_t top;
	uint32_t region;
	unsigned i;
	int ok;
	size_t r;

	nvrtx_heap_init(&heap, 0x100000ULL, 0x1100000ULL);
	check(heap.count == 1 && heap.free_bytes == 0x1000000ULL, "heap: one free run");
	check(nvrtx_heap_alloc(&heap, 0x1000, 0x1000, &a) == 0 && a == 0x10ff000ULL, "heap: first run from the top");
	check(nvrtx_heap_alloc(&heap, 0x3000, 0x10000, &b) == 0 && b == 0x10f0000ULL, "heap: aligned below the first");
	check(heap.count == 2 && heap.free[1].base == 0x10f3000ULL && heap.free[1].size == 0xc000ULL, "heap: alignment leaves a hole");
	check(nvrtx_heap_alloc(&heap, 0x2000, 0x1000, &c) == 0 && c == 0x10fd000ULL, "heap: the hole above is used first");
	check(nvrtx_heap_free(&heap, b, 0x3000) == 0, "heap: free a middle run");
	check(nvrtx_heap_free(&heap, b, 0x3000) == EINVAL, "heap: double free refused");
	check(nvrtx_heap_free(&heap, a, 0x1000) == 0 && nvrtx_heap_free(&heap, c, 0x2000) == 0, "heap: free the rest");
	check(heap.count == 1 && heap.free[0].base == 0x100000ULL && heap.free_bytes == 0x1000000ULL, "heap: runs merge back into one");
	check(nvrtx_heap_alloc(&heap, 0x2000000, 1, &a) == ENOSPC, "heap: too large refused");
	check(nvrtx_heap_alloc(&heap, 0, 1, &a) == EINVAL && nvrtx_heap_alloc(&heap, 0x1000, 3, &a) == EINVAL, "heap: empty size and odd align refused");
	check(nvrtx_heap_reserve(&heap, 0x200000ULL, 0x100000ULL) == 0 && heap.count == 2, "heap: reserve splits");
	check(nvrtx_heap_reserve(&heap, 0x280000ULL, 0x1000ULL) == EBUSY, "heap: reserve of a taken run refused");
	check(nvrtx_heap_free(&heap, 0x80000ULL, 0x1000ULL) == EINVAL, "heap: free outside the range refused");
	check(nvrtx_heap_free(&heap, 0x200000ULL, 0x100000ULL) == 0 && heap.count == 1, "heap: reserved run freed and merged");

	nvrtx_heap_init(&heap, 0, 0x10000000ULL);
	ok = 1;
	for (i = 0; i < 300; i++) {
		if (nvrtx_heap_alloc(&heap, 0x1000, 0x1000, &a) != 0)
			ok = 0;
	}
	for (i = 0; i < 300; i += 2) {
		if (nvrtx_heap_free(&heap, 0x10000000ULL - (uint64_t)(i + 1) * 0x1000, 0x1000) != 0 && i / 2 < NVRTX_HEAP_EXTENTS_MAX - 1)
			ok = 0;
	}
	check(ok && heap.count <= NVRTX_HEAP_EXTENTS_MAX, "heap: many holes stay bounded");

	nvrtx_heap_init(&heap, 5, 5);
	check(heap.count == 0 && nvrtx_heap_alloc(&heap, 1, 1, &a) == ENOSPC, "heap: empty range");

	memset(info, 0, sizeof(info));
	nvrtx_put32(info, 344, 2);
	r = 344 + 8;
	nvrtx_put64(info, r, 0x10000000ULL);
	nvrtx_put64(info, r + 8, 0x1f76fffffULL);
	info[r + 28] = 1;
	info[r + 29] = 1;
	r += 48;
	nvrtx_put64(info, r, 0x1f7700000ULL);
	nvrtx_put64(info, r + 8, 0x1ffffffffULL);
	nvrtx_put64(info, r + 16, 1);
	check(nvrtx_mmu_vram_region(info, &base, &top, &region) == 0 && region == 0 && base == 0x10000000ULL && top == 0x1f7700000ULL, "vram: usable region bounds");
	nvrtx_put32(info, 344, 0);
	check(nvrtx_mmu_vram_region(info, &base, &top, &region) == ENOSPC, "vram: no usable region");
	check(nvrtx_mmu_vram_pte(0x1f3f05000ULL) == ((0x1f3f05000ULL >> 4) | 1), "vram: page entry valid, VRAM, not volatile");
}

static void
test_objects(void)
{
	static uint8_t params[NVRTX_DEVINFO_PARAMS_BYTES];
	uint8_t call[64];
	uint8_t chan[NVRTX_CHANNEL_PARAMS_BYTES];
	uint8_t push[128];
	uint8_t entry[8];
	struct nvrtx_engine_pick pick;
	struct nvrtx_channel_setup setup;
	size_t n;

	nvrtx_pdir_params(params, 0x1f6f00000ULL, 0xc0de90f1U);
	n = nvrtx_control_call(call, sizeof(call), 0xc0de0001U, 0xc0de0080U, NVRTX_CTL_SET_PAGE_DIRECTORY, params, NVRTX_PDIR_PARAMS_BYTES);
	check(n == 56 && nvrtx_get32(call, 0) == 0xc0de0001U && nvrtx_get32(call, 4) == 0xc0de0080U && nvrtx_get32(call, 8) == 0x801813 && nvrtx_get32(call, 16) == 32, "control: header");
	check(nvrtx_get64(call, 24) == 0x1f6f00000ULL && nvrtx_get32(call, 32) == 4 && nvrtx_get32(call, 36) == 0 && nvrtx_get32(call, 40) == 0xc0de90f1U, "control: page directory");
	check(nvrtx_control_call(call, 40, 0, 0, 0, params, 32) == 0, "control: small buffer refused");
	nvrtx_space_params(params);
	check(nvrtx_get32(params, 0) == 0 && nvrtx_get32(params, 4) == 8 && nvrtx_get32(params, 32) == 0, "space: externally owned");

	memset(params, 0, sizeof(params));
	nvrtx_put32(params, 4, 4);
	nvrtx_put32(params, 12 + 0 * 100 + 8, 0x01);
	nvrtx_put32(params, 12 + 0 * 100 + 12, 0);
	nvrtx_put32(params, 12 + 1 * 100 + 8, 0x09);
	nvrtx_put32(params, 12 + 1 * 100 + 12, 0);
	nvrtx_put32(params, 12 + 2 * 100 + 8, 0x13);
	nvrtx_put32(params, 12 + 2 * 100 + 12, 2);
	nvrtx_put32(params, 12 + 3 * 100 + 8, 0x0a);
	nvrtx_put32(params, 12 + 3 * 100 + 12, 3);
	check(nvrtx_devinfo_pick(params, sizeof(params), &pick) == 0 && pick.engine == 0x0a && pick.runlist == 3 && pick.graphics_runlist == 0, "devinfo: copy engine of its own runlist");
	nvrtx_put32(params, 4, 3);
	check(nvrtx_devinfo_pick(params, sizeof(params), &pick) == ENOENT, "devinfo: none");
	nvrtx_put32(params, 4, 40);
	check(nvrtx_devinfo_pick(params, sizeof(params), &pick) == EINVAL, "devinfo: count too large");

	setup.ring_va = 0x20000000ULL;
	setup.ring_entries = 512;
	setup.chid = 8;
	setup.space = 0xc0de90f1U;
	setup.engine = 0x0a;
	setup.instance = 0x1f6f05000ULL;
	setup.userd = 0x1f6f06000ULL;
	setup.methods = 0x30000000ULL;
	setup.methods_size = 0x8000;
	setup.privileged = 0;
	nvrtx_channel_params(chan, &setup);
	check(nvrtx_get64(chan, 8) == 0x20000000ULL && nvrtx_get32(chan, 16) == 512 && nvrtx_get32(chan, 28) == 0xc0de90f1U && nvrtx_get32(chan, 128) == 0x0a, "channel: ring, space, engine");
	check(nvrtx_get32(chan, 20) == ((1u << 12) | (1u << 21)), "channel: flags (USERD index 0, page 1 fixed)");
	check(nvrtx_get64(chan, 144) == 0x1f6f05000ULL && nvrtx_get64(chan, 152) == 0x1000 && nvrtx_get32(chan, 160) == 2 && nvrtx_get32(chan, 164) == 1, "channel: instance in VRAM");
	check(nvrtx_get64(chan, 168) == 0x1f6f06000ULL && nvrtx_get64(chan, 176) == 0x200 && nvrtx_get64(chan, 200) == 0x200, "channel: USERD and RAMFC");
	check(nvrtx_get64(chan, 216) == 0x30000000ULL && nvrtx_get64(chan, 224) == 0x8000 && nvrtx_get32(chan, 232) == 1 && nvrtx_get32(chan, 236) == 0, "channel: method buffer in system memory");
	check(nvrtx_get32(chan, 244) == 0x14 && nvrtx_get32(chan, 360) == 0, "channel: internal flags, tail zero");

	n = nvrtx_push_fence(push, 0x20003000ULL, 0x1234);
	check(n == 52, "push: fence length");
	check(nvrtx_get32(push, 0) == ((1u << 29) | (3u << 16) | (0x5c >> 2)) && nvrtx_get32(push, 4) == 0x20003000U && nvrtx_get32(push, 8) == 0 && nvrtx_get32(push, 12) == 0x1234, "push: semaphore address and value");
	check(nvrtx_get32(push, 16) == ((1u << 29) | (1u << 16) | (0x6c >> 2)) && nvrtx_get32(push, 20) == 0x100001, "push: release with WFI");
	check(nvrtx_get32(push, 24) == ((1u << 29) | (4u << 16) | (0x28 >> 2)) && nvrtx_get32(push, 40) == (5u << 27), "push: membar");
	check(nvrtx_get32(push, 44) == ((1u << 29) | (1u << 16) | (0x20 >> 2)), "push: non-stall");
	nvrtx_ring_entry(entry, 0x120004000ULL, 13);
	check(nvrtx_get32(entry, 0) == 0x20004000U && nvrtx_get32(entry, 4) == (1u | (13u << 10)), "ring: entry");

	n = nvrtx_push_copy_fill(push, 4, 0x20005100ULL, 64, 0xc0ffee01U);
	check(n == 56, "copy: fill length");
	check(nvrtx_get32(push, 0) == ((1u << 29) | (1u << 16) | (4u << 13)) && nvrtx_get32(push, 4) == 0xc5b5, "copy: class on subchannel 4");
	check(nvrtx_get32(push, 8) == ((1u << 29) | (1u << 16) | (4u << 13) | (0x700 >> 2)) && nvrtx_get32(push, 12) == 0xc0ffee01U, "copy: constant");
	check(nvrtx_get32(push, 20) == (4u | (3u << 16)), "copy: remap constant A, 4-byte components");
	check(nvrtx_get32(push, 24) == ((1u << 29) | (2u << 16) | (4u << 13) | (0x408 >> 2)) && nvrtx_get32(push, 28) == 0 && nvrtx_get32(push, 32) == 0x20005100U, "copy: destination");
	check(nvrtx_get32(push, 40) == 64 && nvrtx_get32(push, 44) == 1, "copy: one line of 64");
	check(nvrtx_get32(push, 48) == ((1u << 29) | (1u << 16) | (4u << 13) | (0x300 >> 2)) && nvrtx_get32(push, 52) == (2u | 4u | 0x80u | 0x100u | 0x400u), "copy: launch");

	nvrtx_free_params(params, 0xc0de0001U, 0xc0dec46fU);
	check(nvrtx_get32(params, 0) == 0xc0de0001U && nvrtx_get32(params, 4) == 0 && nvrtx_get32(params, 8) == 0xc0dec46fU && nvrtx_get32(params, 12) == 0, "free: params");

	setup.privileged = 1;
	setup.chid = 1;
	nvrtx_channel_params(chan, &setup);
	check(nvrtx_get32(chan, 20) == ((1u << 8) | (1u << 21) | (1u << 5)), "channel: privileged flag, USERD index 1 page 0");
	check(nvrtx_get32(chan, 244) == 0x15, "channel: admin privilege");
}

/* The graphics golden context's parameters and tables (ws037-p005 5f). */
static void
test_gr(void)
{
	static uint8_t info[1664];
	static uint8_t promote[560];
	static uint8_t pdes[184];
	static uint8_t space[48];
	struct nvrtx_gr_buffer buffers[NVRTX_GR_BUFFER_MAX];
	struct nvrtx_promote_entry entries[2];
	uint8_t *image;
	size_t count;
	size_t length;
	uint64_t pte;

	/* Engine 0's sizes: main, pagepool, patch, bundle, attribute, rtv, fecs event, access map. */
	memset(info, 0, sizeof(info));
	nvrtx_put32(info, 0x00 * 8, 0x48123);
	nvrtx_put32(info, 0x0d * 8, 0x80000);
	nvrtx_put32(info, 0x10 * 8, 0x8000);
	nvrtx_put32(info, 0x11 * 8, 0x300000);
	nvrtx_put32(info, 0x13 * 8, 0x2c0000);
	nvrtx_put32(info, 0x14 * 8, 0x1000);
	nvrtx_put32(info, 0x17 * 8, 0x2000);
	nvrtx_put32(info, 0x18 * 8, 0x3000);
	count = nvrtx_gr_buffers(info, sizeof(info), buffers, NVRTX_GR_BUFFER_MAX);
	check(count == 9, "gr: nine buffers with the unrestricted map");
	check(buffers[0].buffer_id == 0 && buffers[0].size == 0x49000 + 0x40000 && buffers[0].page_shift == 16 && buffers[0].initialize && !buffers[0].global, "gr: main with subcontext headers, 64 KiB pages");
	check(buffers[1].buffer_id == 4 && buffers[1].global && !buffers[1].initialize && buffers[1].page_shift == 16, "gr: pagepool");
	check(buffers[2].buffer_id == 2 && !buffers[2].global && buffers[2].initialize && buffers[2].page_shift == 12, "gr: patch");
	check(buffers[3].buffer_id == 3 && buffers[3].page_shift == 21 && buffers[3].align_shift == 21, "gr: bundle, 2 MiB pages");
	check(buffers[4].buffer_id == 5 && buffers[4].page_shift == 21 && buffers[4].align_shift == 22, "gr: attribute aligned to its size");
	check(buffers[6].buffer_id == 9 && buffers[6].global && buffers[6].initialize, "gr: fecs event");
	check(buffers[7].buffer_id == 10 && buffers[7].read_only && buffers[8].buffer_id == 11 && buffers[8].read_only && buffers[8].size == 0x3000, "gr: the access map and its unrestricted twin");
	nvrtx_put32(info, 0x14 * 8, 0);
	check(nvrtx_gr_buffers(info, sizeof(info), buffers, NVRTX_GR_BUFFER_MAX) == 0, "gr: a missing buffer refuses the list");
	check(nvrtx_gr_buffers(info, 100, buffers, NVRTX_GR_BUFFER_MAX) == 0, "gr: short information refused");

	/* The promote: header and two entries. */
	entries[0].buffer_id = 0;
	entries[0].initialize = 1;
	entries[0].nonmapped = 0;
	entries[0].virtual_address = 0x20000000ULL;
	entries[0].physical = 0x1e8400000ULL;
	entries[0].size = 0x89000;
	entries[1].buffer_id = 10;
	entries[1].initialize = 1;
	entries[1].nonmapped = 1;
	entries[1].virtual_address = 0x20400000ULL;
	entries[1].physical = 0x1e8800000ULL;
	entries[1].size = 0x3000;
	nvrtx_promote_params(promote, 0xc1d00000U, 0xf1f00001U, entries, 2);
	check(nvrtx_get32(promote, 0) == 1 && nvrtx_get32(promote, 4) == 0 && nvrtx_get32(promote, 12) == 0xc1d00000U && nvrtx_get32(promote, 16) == 0xf1f00001U && nvrtx_get32(promote, 40) == 2, "promote: engine, client, channel, count");
	check(nvrtx_get64(promote, 48) == 0x1e8400000ULL && nvrtx_get64(promote, 56) == 0x20000000ULL && nvrtx_get64(promote, 64) == 0x89000 && nvrtx_get32(promote, 72) == 4 && promote[76] == 0 && promote[77] == 0 && promote[78] == 1 && promote[79] == 0, "promote: main entry");
	check(nvrtx_get64(promote, 80) == 0x1e8800000ULL && nvrtx_get64(promote, 88) == 0 && promote[108] == 10 && promote[110] == 1 && promote[111] == 1, "promote: access map entry, not mapped");

	/* The copy of the server's directories. */
	nvrtx_copy_pdes_params(pdes, 0x1e8000000ULL, 0x1e8001000ULL, 0x1e8002000ULL);
	check(nvrtx_get64(pdes, 8) == 0x20000000ULL && nvrtx_get64(pdes, 16) == 0x100000000ULL && nvrtx_get64(pdes, 24) == 0x11fffffffULL && nvrtx_get32(pdes, 32) == 3, "pdes: 512 MiB at 4 GiB, three levels");
	check(nvrtx_get64(pdes, 40) == 0x1e8000000ULL && nvrtx_get64(pdes, 48) == 32 && nvrtx_get32(pdes, 56) == 1 && nvrtx_get32(pdes, 60) == 47, "pdes: root");
	check(nvrtx_get64(pdes, 64) == 0x1e8001000ULL && nvrtx_get64(pdes, 72) == 4096 && nvrtx_get32(pdes, 84) == 38, "pdes: second level");
	check(nvrtx_get64(pdes, 88) == 0x1e8002000ULL && nvrtx_get64(pdes, 96) == 4096 && nvrtx_get32(pdes, 108) == 29 && nvrtx_get32(pdes, 0) == 0 && nvrtx_get32(pdes, 4) == 0, "pdes: first level, subdevice 0");

	nvrtx_space_params_shared(space);
	check(nvrtx_get32(space, 0) == 0 && nvrtx_get32(space, 4) == 0, "space: shared, no flags");

	/* The tables: 8 MiB at 0x20000000, then a privileged read-only page. */
	length = nvrtx_mmu_gr_tables_bytes(0x800000ULL);
	check(length == 0x4000 + 4 * 0x1000, "gr tables: four page tables");
	image = calloc(1, length);
	check(nvrtx_mmu_gr_tables(image, 0x1e8000000ULL, 0x20000000ULL, 0x800000ULL) == 0, "gr tables: built");
	check(nvrtx_get64(image, 0) == ((0x1e8001000ULL >> 4) | 2) && nvrtx_get64(image, 0x1000) == ((0x1e8002000ULL >> 4) | 2), "gr tables: root and second level on index 0");
	check(nvrtx_get64(image, 0x2000 + 1 * 8) == ((0x1e8003000ULL >> 4) | 2) && nvrtx_get64(image, 0x2000 + 8 * 8) == 0, "gr tables: first level entry 1, the server's entry 8 left");
	check(nvrtx_get64(image, 0x3000 + 0 * 16 + 8) == ((0x1e8004000ULL >> 4) | 2) && nvrtx_get64(image, 0x3000 + 3 * 16 + 8) == ((0x1e8007000ULL >> 4) | 2) && nvrtx_get64(image, 0x3000 + 4 * 16 + 8) == 0, "gr tables: one page table per 2 MiB");
	check(nvrtx_mmu_gr_map(image, 0x20000000ULL, 0x800000ULL, 0x20201000ULL, 0x1e8400000ULL, 0x2000, 1) == 0, "gr map: two pages");
	pte = nvrtx_get64(image, 0x4000 + (0x201000 >> 12) * 8);
	check(pte == ((0x1e8400000ULL >> 4) | 1 | (1u << 5) | (1u << 6)), "gr map: valid, VRAM, privileged, read-only");
	check(nvrtx_get64(image, 0x4000 + (0x202000 >> 12) * 8) == ((0x1e8401000ULL >> 4) | 1 | (1u << 5) | (1u << 6)), "gr map: second page");
	check(nvrtx_mmu_gr_map(image, 0x20000000ULL, 0x800000ULL, 0x207ff000ULL, 0x1e8400000ULL, 0x2000, 0) == EINVAL, "gr map: run past the range refused");
	check(nvrtx_mmu_gr_tables(image, 0x1e8000000ULL, 0xf0000000ULL, 0x20000000ULL) == EINVAL, "gr tables: range into the server's refused");
	check(nvrtx_mmu_gr_tables(image, 0x1e8000000ULL, 0x20100000ULL, 0x200000ULL) == EINVAL, "gr tables: unaligned range refused");
	free(image);
}

/* The display records of P8b (ws037-p006): push words, hash-table entries, context DMA, parameters. */
static void
test_disp(void)
{
	uint8_t memory[64 * 4];
	uint8_t small[3 * 4];
	uint8_t entry[NVRTX_DISP_RAMHT_ENTRY_BYTES];
	uint8_t object[NVRTX_DISP_CONTEXT_DMA_BYTES];
	uint8_t pb[NVRTX_DISP_PUSHBUFFER_BYTES];
	uint8_t chan[NVRTX_DISP_CHANNEL_BYTES];
	uint32_t armed[NVRTX_DISP_HEAD_COPIED];
	uint8_t assign[NVRTX_DISP_ASSIGN_SOR_BYTES];
	struct nvrtx_disp_mode mode;
	struct nvrtx_push push;
	struct nvrtx_disp_surface surface;
	uint32_t i;

	/* One method: header (count 1 at bit 18, method offset), then the word. */
	nvrtx_push_init(&push, memory, 64);
	nvrtx_push_one(&push, NVRTX_CORE_SET_CONTEXT_DMA_NOTIFIER, NVRTX_DISP_HANDLE_SYNC);
	check(push.count == 2 && nvrtx_get32(memory, 0) == 0x00040208U && nvrtx_get32(memory, 4) == 0xf0000000U, "disp push one method");

	/* A run that does not fit writes nothing and marks the overflow. */
	nvrtx_push_init(&push, small, 3);
	nvrtx_push_method(&push, 0x1004, armed, 3);
	check(push.count == 0 && push.overflow == 1, "disp push overflow");

	/* The hash folds the handle by 10 bits and moves the channel up by 6. */
	check(nvrtx_disp_ramht_hash(0, 0xf0000000U) == 0x303U, "disp ramht hash core sync");
	check(nvrtx_disp_ramht_hash(0, 0xf0000001U) == 0x302U, "disp ramht hash core vram");
	check(nvrtx_disp_ramht_hash(1, 0xf0000000U) == 0x343U, "disp ramht hash window 0 sync");

	/* The entry: the handle, then chid 25+, object offset 9+, client's low 14 bits. */
	nvrtx_disp_ramht_entry(entry, 0xf0000001U, 1, 0xc1d00000U, 0x2040);
	check(nvrtx_get32(entry, 0) == 0xf0000001U && nvrtx_get32(entry, 4) == 0x02408000U, "disp ramht entry");

	/* The context DMA object over the VRAM: flags 0x45, start and limit in 256-byte units. */
	nvrtx_disp_context_dma(object, 0x1f3e10000ULL, 0x1f3e10fffULL);
	check(nvrtx_get32(object, 0) == 0x45U && nvrtx_get32(object, 4) == 0x01f3e100U && nvrtx_get32(object, 8) == 0 &&
	    nvrtx_get32(object, 12) == 0x01f3e10fU && nvrtx_get32(object, 16) == 0 && nvrtx_get32(object, 20) == 0, "disp context dma");

	/* The push buffer's registration (r570 layout: 56 bytes). */
	nvrtx_disp_pushbuffer_params(pb, 0x123456000ULL, 0x1000, NVRTX_DISP_CLASS_WINDOW, 2);
	check(nvrtx_get32(pb, 0) == 1 && nvrtx_get32(pb, 8) == 0x23456000U && nvrtx_get32(pb, 12) == 1 &&
	    nvrtx_get32(pb, 16) == 0xfffU && nvrtx_get32(pb, 24) == 1 && nvrtx_get32(pb, 28) == 0xc57eU &&
	    nvrtx_get32(pb, 32) == 2 && pb[36] == 1 && nvrtx_get32(pb, 40) == 3 && nvrtx_get32(pb, 44) == 0 &&
	    nvrtx_get32(pb, 48) == 1, "disp pushbuffer params");

	/* The channel's allocation (40 bytes): instance at 0, put 0, subdevice mask at 32. */
	nvrtx_disp_channel_params(chan, 2);
	check(nvrtx_get32(chan, 0) == 2 && nvrtx_get32(chan, 12) == 0 && nvrtx_get32(chan, 32) == 1, "disp channel params");

	/* The mode from the armed state: the visible size from the blank's start less its end. */
	for (i = 0; i < NVRTX_DISP_HEAD_COPIED; i++)
		armed[i] = 0x1000U + i;
	armed[NVRTX_DISP_ARMED_RASTER_SIZE] = 0x042a0698U;
	armed[NVRTX_DISP_ARMED_BLANK_END] = 0x00290168U;
	armed[NVRTX_DISP_ARMED_BLANK_START] = 0x04290668U;
	armed[NVRTX_DISP_ARMED_PIXEL_CLOCK] = 0x066ff300U;
	nvrtx_disp_mode_from_armed(armed, &mode);
	check(mode.active == 0x04000500U && mode.raster_size == 0x042a0698U && mode.clock_hz == 108000000U &&
	    mode.output_resource == 0x100eU && mode.dither == 0x100bU, "disp mode from armed");

	/* The head's mode on head 1 (0x2400 on): view in and out, the raster run, then nouveau's fixed words. */
	nvrtx_push_init(&push, memory, 64);
	nvrtx_disp_core_head_mode(&push, 1, &mode);
	check(push.overflow == 0 && nvrtx_get32(memory, 0) == 0x0004244cU && nvrtx_get32(memory, 4) == 0x04000500U &&
	    nvrtx_get32(memory, 8) == 0x00042458U && nvrtx_get32(memory, 12) == 0x04000500U &&
	    nvrtx_get32(memory, 16) == 0x00102464U && nvrtx_get32(memory, 20) == 0x042a0698U &&
	    nvrtx_get32(memory, 32) == 0x04290668U, "disp head mode view and raster");
	check(nvrtx_get32(memory, 36) == 0x00042474U && nvrtx_get32(memory, 44) == 0x00042408U && nvrtx_get32(memory, 48) == 0 &&
	    nvrtx_get32(memory, 52) == 0x0004240cU && nvrtx_get32(memory, 56) == 108000000U &&
	    nvrtx_get32(memory, 60) == 0x00042428U && nvrtx_get32(memory, 68) == 0x00042430U && nvrtx_get32(memory, 72) == 0x1114U,
	    "disp head mode control, clock and usage");
	check(nvrtx_get32(memory, 76) == 0x00042418U && nvrtx_get32(memory, 80) == 0x100bU && nvrtx_get32(memory, 84) == 0x00042400U &&
	    nvrtx_get32(memory, 88) == 0 && nvrtx_get32(memory, 92) == 0x00042404U && nvrtx_get32(memory, 96) == 0x100eU &&
	    push.count == 25, "disp head mode dither, procamp, output resource");

	/* The display id of head 1 and the SOR control. */
	nvrtx_push_init(&push, memory, 64);
	nvrtx_disp_core_display_id(&push, 1, 0x200);
	nvrtx_disp_core_sor(&push, 1, (1U << NVRTX_DISP_SOR_PROTOCOL_SHIFT) | 1U);
	check(nvrtx_get32(memory, 0) == 0x00042420U && nvrtx_get32(memory, 4) == 0x200U &&
	    nvrtx_get32(memory, 8) == 0x00040320U && nvrtx_get32(memory, 12) == 0x101U, "disp display id and SOR control");

	/* The SOR assignment: 80 bytes, the display at 4, the exclusion at 8; the reply's tagged list at 40. */
	nvrtx_disp_assign_sor_params(assign, 0x200, 0x2);
	check(nvrtx_get32(assign, 0) == 0 && nvrtx_get32(assign, 4) == 0x200U && assign[8] == 2 && nvrtx_get32(assign, 76) == 0,
	    "disp assign SOR params");
	check(nvrtx_disp_assign_sor_result(assign, 0x200) == NVRTX_DISP_SOR_NONE, "disp assign SOR none");
	nvrtx_put32(assign, 40 + 8, 0x200);
	check(nvrtx_disp_assign_sor_result(assign, 0x200) == 1, "disp assign SOR 1");

	/* The jump back to the start of a push buffer. */
	nvrtx_push_init(&push, memory, 64);
	nvrtx_push_jump(&push, 0);
	check(push.count == 1 && nvrtx_get32(memory, 0) == 0x20000000U, "disp push jump");

	/* The core update: notifier on, interlocks (none, window 0), update, notifier off. */
	nvrtx_push_init(&push, memory, 64);
	nvrtx_disp_core_update(&push, 0, 1);
	check(push.count == 9 && nvrtx_get32(memory, 4) == 0x1000U && nvrtx_get32(memory, 8) == 0x00080218U &&
	    nvrtx_get32(memory, 12) == 0 && nvrtx_get32(memory, 16) == 1 && nvrtx_get32(memory, 20) == 0x00040200U &&
	    nvrtx_get32(memory, 24) == 1 && nvrtx_get32(memory, 32) == 0, "disp core update");

	/* The window's image: the pitch in 64-byte units, offset in 256-byte units. */
	surface.offset = 0x1e0000000ULL;
	surface.width = 1280;
	surface.height = 1024;
	surface.pitch = 5120;
	surface.handle = NVRTX_DISP_HANDLE_VRAM;
	surface.format = NVRTX_DISP_FORMAT_A8R8G8B8;
	nvrtx_push_init(&push, memory, 64);
	nvrtx_disp_window_image(&push, &surface);
	check(nvrtx_get32(memory, 0) == 0x00040308U && nvrtx_get32(memory, 4) == 1 && nvrtx_get32(memory, 8) == 0x00100224U &&
	    nvrtx_get32(memory, 12) == 0x04000500U && nvrtx_get32(memory, 16) == 0x10 && nvrtx_get32(memory, 20) == 0xcfU &&
	    nvrtx_get32(memory, 24) == 80 && nvrtx_get32(memory, 32) == 0xf0000001U && nvrtx_get32(memory, 40) == 0x01e00000U,
	    "disp window image");

	/* The window update with the core: interlock bit 0, the window's own bit. */
	nvrtx_push_init(&push, memory, 64);
	nvrtx_disp_window_update(&push, 1, 2);
	check(nvrtx_get32(memory, 0) == 0x00080370U && nvrtx_get32(memory, 4) == 1 && nvrtx_get32(memory, 8) == 4 &&
	    nvrtx_get32(memory, 12) == 0x00040200U, "disp window update");

	/* The bands wrap after eight. */
	check(nvrtx_disp_band_color(0) == 0xffffffffU && nvrtx_disp_band_color(8) == 0xffffffffU &&
	    nvrtx_disp_band_color(7) == 0xff000000U, "disp band colours");
}

/* The cursor and display-control records of P8c (ws037-p006): the PIO channel, the core's cursor methods, positions, pixels, frames. */
static void
test_disp_cursor(void)
{
	uint8_t memory[64 * 4];
	uint8_t pb[NVRTX_DISP_PUSHBUFFER_BYTES];
	uint8_t pio[NVRTX_DISP_PIO_CHANNEL_BYTES];
	struct nvrtx_push push;
	uint32_t i;
	int zero;

	/* The PIO channel's registration: class and instance, not valid, subdevice 0, no memory (r570 layout). */
	memset(pb, 0xa5, sizeof(pb));
	nvrtx_disp_pio_pushbuffer_params(pb, NVRTX_DISP_CLASS_CURSOR, 1);
	check(nvrtx_get32(pb, 0) == 0 && nvrtx_get32(pb, 8) == 0 && nvrtx_get32(pb, 12) == 0 && nvrtx_get32(pb, 16) == 0 &&
	    nvrtx_get32(pb, 24) == 0 && nvrtx_get32(pb, 28) == 0xc57aU && nvrtx_get32(pb, 32) == 1 && pb[36] == 0 &&
	    nvrtx_get32(pb, 40) == 0 && nvrtx_get32(pb, 48) == 1, "disp cursor pushbuffer params");

	/* The PIO channel's allocation (16 bytes): the instance, nothing else. */
	memset(pio, 0xa5, sizeof(pio));
	nvrtx_disp_pio_channel_params(pio, 1);
	zero = 1;
	for (i = 4; i < NVRTX_DISP_PIO_CHANNEL_BYTES; i++) {
		if (pio[i] != 0)
			zero = 0;
	}
	check(nvrtx_get32(pio, 0) == 1 && zero, "disp cursor channel params");

	/* The cursor on head 0: control and composition in one run, the object, the offset in 256-byte units. */
	nvrtx_push_init(&push, memory, 64);
	nvrtx_disp_core_cursor_set(&push, 0, NVRTX_DISP_HANDLE_VRAM, 0x1e0040000ULL);
	check(push.count == 7 && nvrtx_get32(memory, 0) == 0x0008209cU && nvrtx_get32(memory, 4) == 0x800001cfU &&
	    nvrtx_get32(memory, 8) == 0x000072ffU && nvrtx_get32(memory, 12) == 0x00042088U &&
	    nvrtx_get32(memory, 16) == 0xf0000001U && nvrtx_get32(memory, 20) == 0x00042090U &&
	    nvrtx_get32(memory, 24) == 0x01e00400U, "disp core cursor set");

	/* The cursor off on head 1: control A8R8G8B8 disabled, no object. */
	nvrtx_push_init(&push, memory, 64);
	nvrtx_disp_core_cursor_clear(&push, 1);
	check(push.count == 4 && nvrtx_get32(memory, 0) == 0x0004249cU && nvrtx_get32(memory, 4) == 0xcfU &&
	    nvrtx_get32(memory, 8) == 0x00042488U && nvrtx_get32(memory, 12) == 0, "disp core cursor clear");

	/* The core's update with head 1's cursor: notifier on, interlocks (cursor 1, no window), update, notifier off. */
	nvrtx_push_init(&push, memory, 64);
	nvrtx_disp_core_update_cursor(&push, 0, 1);
	check(push.count == 9 && nvrtx_get32(memory, 0) == 0x0004020cU && nvrtx_get32(memory, 4) == 0x1000U &&
	    nvrtx_get32(memory, 8) == 0x00080218U && nvrtx_get32(memory, 12) == 2 && nvrtx_get32(memory, 16) == 0 &&
	    nvrtx_get32(memory, 20) == 0x00040200U && nvrtx_get32(memory, 24) == 1 && nvrtx_get32(memory, 32) == 0,
	    "disp core update with the cursor");

	/* Positions: x low, y high, 16-bit two's complement, held inside 16 bits. */
	check(nvrtx_disp_cursor_point(10, 20) == 0x0014000aU, "disp cursor point");
	check(nvrtx_disp_cursor_point(-5, -1) == 0xfffffffbU, "disp cursor point negative");
	check(nvrtx_disp_cursor_point(40000, -40000) == 0x80007fffU, "disp cursor point held");

	/* Pixels: BGRA8888 is A8R8G8B8 already, RGBA8888 swaps red and blue. */
	check(nvrtx_disp_cursor_pixel(0x80112233U, 0) == 0x80112233U, "disp cursor pixel BGRA");
	check(nvrtx_disp_cursor_pixel(0x80112233U, 1) == 0x80332211U, "disp cursor pixel RGBA");

	/* The 16-bit frame counter's movement, across its wrap. */
	check(nvrtx_disp_frame_advance(10, 12) == 2 && nvrtx_disp_frame_advance(5, 5) == 0 &&
	    nvrtx_disp_frame_advance(0xfffe, 0x0001) == 3, "disp frame advance");
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
	test_mmu();
	test_heap();
	test_objects();
	test_gr();
	test_disp();
	test_disp_cursor();
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
