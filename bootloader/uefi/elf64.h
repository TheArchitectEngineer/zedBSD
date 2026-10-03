/* Restricted ELF64 loader contract for the amd64 kernel. */
/* Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib */
#ifndef KERN_BOOTLOADER_UEFI_ELF64_H
#define KERN_BOOTLOADER_UEFI_ELF64_H

#include <stdint.h>

#define ZBL_ELF_HEADER_BYTES 512U
#define ZBL_ELF_MAX_SEGMENTS 8U

struct zbl_elf64_segment {
	uint64_t offset;
	uint64_t physical;
	uint64_t virtual_address;
	uint64_t file_size;
	uint64_t memory_size;
	uint32_t flags;
};

/*
 * link_physical_* is the extent the image was linked for; physical_* and
 * every segment->physical are where it will actually be loaded.  Both are
 * equal after zbl_elf64_plan(); zbl_elf64_place() rebases the latter.
 */
struct zbl_elf64_plan {
	uint64_t entry;
	uint64_t link_physical_start;
	uint64_t link_physical_end;
	uint64_t physical_start;
	uint64_t physical_end;
	uint32_t segment_count;
	struct zbl_elf64_segment segment[ZBL_ELF_MAX_SEGMENTS];
};

int zbl_elf64_plan(const void *header, uint64_t file_size_limit,
	struct zbl_elf64_plan *plan);

/* Rebases a validated plan onto the physical home the loader obtained. */
int zbl_elf64_place(struct zbl_elf64_plan *plan, uint64_t load_base);

#endif
