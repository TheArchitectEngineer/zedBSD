/* Host test: ELF64 placement plan/rebase and the HAL's placement rule. */
/* Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bootloader/uefi/elf64.h"
#include "bootloader/include/amd64-kernel-image.h"
#include "src/hal/amd64/bsp-pcat/handoff-validation.h"

struct ehdr {
	uint8_t ident[16];
	uint16_t type, machine;
	uint32_t version;
	uint64_t entry, phoff, shoff;
	uint32_t flags;
	uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} __attribute__((packed));

struct phdr {
	uint32_t type, flags;
	uint64_t offset, vaddr, paddr, filesz, memsz, align;
} __attribute__((packed));

static void
build(uint8_t *buffer, uint64_t first_paddr, uint64_t bss_extra)
{
	struct ehdr *e = (void *)buffer;
	struct phdr *p = (void *)(buffer + sizeof(*e));
	unsigned i;

	memset(buffer, 0, ZBL_ELF_HEADER_BYTES);
	memcpy(e->ident, "\x7f" "ELF\x02\x01\x01", 7);
	e->type = 2; e->machine = 62; e->version = 1;
	e->entry = AMD64_KERNEL_LINK_VIRT_BASE + first_paddr + 0x10;
	e->phoff = sizeof(*e); e->ehsize = sizeof(*e);
	e->phentsize = sizeof(*p); e->phnum = 3;
	for (i = 0; i < 3; i++) {
		p[i].type = 1;
		p[i].flags = i == 0 ? 5 : i == 1 ? 4 : 6;
		p[i].offset = 0x1000 + i * 0x2000;
		p[i].paddr = first_paddr + i * 0x2000;
		p[i].vaddr = AMD64_KERNEL_LINK_VIRT_BASE + p[i].paddr;
		p[i].filesz = 0x2000;
		p[i].memsz = 0x2000 + (i == 2 ? bss_extra : 0);
		p[i].align = 0x1000;
	}
}

int
main(void)
{
	static uint8_t buffer[ZBL_ELF_HEADER_BYTES];
	struct zbl_elf64_plan plan;
	struct phdr *p = (void *)(buffer + sizeof(struct ehdr));

	/* The plan is expressed in linked physical coordinates. */
	build(buffer, AMD64_KERNEL_LINK_PHYS_START, 0x4000);
	assert(zbl_elf64_plan(buffer, 0x100000, &plan));
	assert(plan.segment_count == 3);
	assert(plan.link_physical_start == AMD64_KERNEL_LINK_PHYS_START);
	assert(plan.link_physical_end == AMD64_KERNEL_LINK_PHYS_START + 0x4000 + 0x6000);
	assert(plan.physical_start == plan.link_physical_start);
	assert(plan.physical_end == plan.link_physical_end);
	assert(plan.segment[2].physical == AMD64_KERNEL_LINK_PHYS_START + 0x4000);

	/* Placing at the linked address changes nothing. */
	assert(zbl_elf64_place(&plan, AMD64_KERNEL_LINK_PHYS_START));
	assert(plan.physical_start == plan.link_physical_start);
	assert(plan.segment[1].physical == AMD64_KERNEL_LINK_PHYS_START + 0x2000);

	/* Relocation rebases every segment and the load extent, not the link extent. */
	assert(zbl_elf64_place(&plan, 0x02000000));
	assert(plan.physical_start == 0x02000000);
	assert(plan.physical_end == 0x02000000 + 0xa000);
	assert(plan.link_physical_start == AMD64_KERNEL_LINK_PHYS_START);
	assert(plan.segment[0].physical == 0x02000000);
	assert(plan.segment[2].physical == 0x02004000);
	assert(plan.segment[2].virtual_address == AMD64_KERNEL_LINK_VIRT_BASE + AMD64_KERNEL_LINK_PHYS_START + 0x4000);
	assert(plan.entry == AMD64_KERNEL_LINK_VIRT_BASE + AMD64_KERNEL_LINK_PHYS_START + 0x10);

	/* Placing again from a relocated plan is relative to the linked extent. */
	assert(zbl_elf64_place(&plan, 0x00400000));
	assert(plan.physical_start == 0x00400000 && plan.segment[1].physical == 0x00402000);

	/* The last aligned slot that still holds the image is fine... */
	assert(zbl_elf64_place(&plan, AMD64_KERNEL_PHYS_LIMIT - AMD64_KERNEL_PHYS_ALIGN));
	assert(plan.physical_end == AMD64_KERNEL_PHYS_LIMIT - AMD64_KERNEL_PHYS_ALIGN + 0xa000);
	assert(zbl_elf64_place(&plan, 0x00400000));

	/* ...but misaligned, below the linked start, or past the window is not. */
	assert(!zbl_elf64_place(&plan, 0x02100000));
	assert(!zbl_elf64_place(&plan, 0x00100000));
	assert(!zbl_elf64_place(&plan, AMD64_KERNEL_PHYS_LIMIT));
	p[2].memsz = 0x2000 + 0x4000;
	build(buffer, AMD64_KERNEL_LINK_PHYS_START, AMD64_KERNEL_PHYS_ALIGN);
	assert(zbl_elf64_plan(buffer, 0x100000, &plan));
	assert(!zbl_elf64_place(&plan, AMD64_KERNEL_PHYS_LIMIT - AMD64_KERNEL_PHYS_ALIGN));
	assert(plan.physical_start == AMD64_KERNEL_LINK_PHYS_START);
	build(buffer, AMD64_KERNEL_LINK_PHYS_START, 0x4000);
	assert(zbl_elf64_plan(buffer, 0x100000, &plan));
	assert(zbl_elf64_place(&plan, 0x00400000));
	assert(plan.physical_start == 0x00400000);

	/* The image must be linked inside [link start, link start + max). */
	build(buffer, AMD64_KERNEL_LINK_PHYS_START + 0x1000, 0);
	assert(!zbl_elf64_plan(buffer, 0x100000, &plan));
	build(buffer, AMD64_KERNEL_LINK_PHYS_START, 0);
	p[2].memsz = AMD64_KERNEL_MAX_BYTES;
	assert(!zbl_elf64_plan(buffer, 0x100000, &plan));
	build(buffer, AMD64_KERNEL_LINK_PHYS_START, 0);
	p[1].vaddr += 0x1000;
	assert(!zbl_elf64_plan(buffer, 0x100000, &plan));

	/* The HAL accepts the same homes the loader may choose. */
	assert(zbl6_kernel_placement_valid(0x200000, 0x400000));
	assert(zbl6_kernel_placement_valid(0x2000000, 0x2000000 + 0x5d0000));
	assert(zbl6_kernel_placement_valid(0x3e000000, 0x3e000000 + AMD64_KERNEL_MAX_BYTES));
	assert(!zbl6_kernel_placement_valid(0x2100000, 0x2300000));
	assert(!zbl6_kernel_placement_valid(0x100000, 0x300000));
	assert(!zbl6_kernel_placement_valid(0x400000, 0x400000));
	assert(!zbl6_kernel_placement_valid(0x2000000, 0x2000000 + AMD64_KERNEL_MAX_BYTES + 0x1000));
	assert(!zbl6_kernel_placement_valid(0x3fe00000, 0x40001000));
	puts("PASS: kernel placement plan, rebase and HAL rule");
	return 0;
}
