/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The restricted ELF64 validation shared by the UEFI loader.
 */

#include "elf64.h"
#include "bootloader/include/amd64-kernel-image.h"

/* The program header type of a loadable segment. */
#define PT_LOAD 1U

/* The program header flag of an executable segment. */
#define PF_X    1U

/* The ELF machine of amd64. */
#define EM_X86_64 62U

/* The ELF type of an executable. */
#define ET_EXEC 2U

/* The image must be linked inside [link start, link start + max bytes). */
#define KERNEL_LINK_PHYS_LIMIT \
	(AMD64_KERNEL_LINK_PHYS_START + AMD64_KERNEL_MAX_BYTES)

/* The ELF64 file header as it is in the kernel file. */
struct elf64_header {
	uint8_t ident[16];
	uint16_t type;
	uint16_t machine;
	uint32_t version;
	uint64_t entry;
	uint64_t phoff;
	uint64_t shoff;
	uint32_t flags;
	uint16_t ehsize;
	uint16_t phentsize;
	uint16_t phnum;
	uint16_t shentsize;
	uint16_t shnum;
	uint16_t shstrndx;
} __attribute__((packed));

/* One ELF64 program header as it is in the kernel file. */
struct elf64_program_header {
	uint32_t type;
	uint32_t flags;
	uint64_t offset;
	uint64_t vaddr;
	uint64_t paddr;
	uint64_t filesz;
	uint64_t memsz;
	uint64_t align;
} __attribute__((packed));

static int header_acceptable(const struct elf64_header *header);
static int segment_acceptable(const struct elf64_program_header *ph, uint32_t segment_count, uint64_t file_size_limit);

/*
 * Validates a kernel's headers and builds its load plan.
 *
 * Returns 1 when the kernel is an amd64 executable the loader can place,
 * with its PT_LOAD segments ascending, inside the link window and its entry
 * point in an executable segment; returns 0 otherwise.
 */
int
zbl_elf64_plan(
	const void *buffer,
	uint64_t file_size_limit,
	struct zbl_elf64_plan *plan)
{
	const struct elf64_header *header;
	const struct elf64_program_header *ph;
	struct zbl_elf64_segment *segment;
	const uint8_t *bytes;
	uint64_t previous_end;
	uint64_t end;
	uint16_t index;
	int entry_is_executable;
	int acceptable;

	/* Views the buffer as the file header and as bytes. */
	header = buffer;
	bytes = buffer;
	previous_end = 0;
	entry_is_executable = 0;

	/* Refuses a missing buffer or plan. */
	if (buffer == 0 || plan == 0)
		return 0;

	/* Refuses a file that is not an amd64 executable with its program headers in reach. */
	acceptable = header_acceptable(header);
	if (!acceptable)
		return 0;

	/* Starts an empty plan at the kernel's entry point. */
	plan->entry = header->entry;
	plan->link_physical_start = UINT64_MAX;
	plan->link_physical_end = 0;
	plan->segment_count = 0;

	/* Takes each PT_LOAD segment into the plan. */
	for (index = 0; index < header->phnum; index++) {
		/* Finds the program header; only PT_LOAD segments are loaded. */
		ph = (const void *)(bytes + header->phoff + (uint64_t)index * header->phentsize);
		if (ph->type != PT_LOAD)
			continue;

		/* Refuses a segment that cannot be loaded where it was linked. */
		acceptable = segment_acceptable(ph, plan->segment_count, file_size_limit);
		if (!acceptable)
			return 0;

		/* Refuses a segment that starts below the end of the one before it. */
		end = ph->paddr + ph->memsz;
		if (previous_end != 0 && ph->paddr < previous_end)
			return 0;

		/* Records the segment. */
		previous_end = end;
		segment = &plan->segment[plan->segment_count];
		plan->segment_count++;
		segment->offset = ph->offset;
		segment->physical = ph->paddr;
		segment->virtual_address = ph->vaddr;
		segment->file_size = ph->filesz;
		segment->memory_size = ph->memsz;
		segment->flags = ph->flags;

		/* Widens the linked extent to take the segment in. */
		if (ph->paddr < plan->link_physical_start)
			plan->link_physical_start = ph->paddr;
		if (end > plan->link_physical_end)
			plan->link_physical_end = end;

		/* The entry point counts only when it lies in an executable segment. */
		if ((ph->flags & PF_X) != 0 &&
		    header->entry >= ph->vaddr &&
		    header->entry < ph->vaddr + ph->memsz)
			entry_is_executable = 1;
	}

	/* The kernel is loaded where it was linked until it is placed. */
	plan->physical_start = plan->link_physical_start;
	plan->physical_end = plan->link_physical_end;

	/* Refuses a kernel with nothing to load. */
	if (plan->segment_count == 0)
		return 0;

	/* Refuses a kernel whose entry point is in no executable segment. */
	if (!entry_is_executable)
		return 0;

	/* Refuses a kernel that does not start at the linked physical address. */
	if (plan->link_physical_start != AMD64_KERNEL_LINK_PHYS_START)
		return 0;

	/* Succeeded: the plan describes a kernel the loader can place. */
	return 1;
}

/*
 * Rebases the load extent and every segment onto load_base.
 *
 * The virtual addresses and the entry are untouched: the kernel is not
 * relocatable, the loader maps its linked virtual range onto the chosen
 * physical range.
 */
int
zbl_elf64_place(
	struct zbl_elf64_plan *plan,
	uint64_t load_base)
{
	struct zbl_elf64_segment *segment;
	uint64_t bytes;
	uint64_t delta;
	uint32_t index;

	/* Refuses a plan that zbl_elf64_plan() did not accept. */
	if (plan == 0 ||
	    plan->segment_count == 0 ||
	    plan->link_physical_start != AMD64_KERNEL_LINK_PHYS_START ||
	    plan->link_physical_end <= plan->link_physical_start)
		return 0;

	/* Refuses a base that is unaligned or leaves the image outside the bootstrap window. */
	bytes = plan->link_physical_end - plan->link_physical_start;
	if ((load_base & (AMD64_KERNEL_PHYS_ALIGN - 1U)) != 0 ||
	    load_base < AMD64_KERNEL_LINK_PHYS_START ||
	    load_base > AMD64_KERNEL_PHYS_LIMIT ||
	    bytes > AMD64_KERNEL_PHYS_LIMIT - load_base)
		return 0;

	/* Moves the extent to the base. */
	delta = load_base - plan->link_physical_start;
	plan->physical_start = load_base;
	plan->physical_end = load_base + bytes;

	/* Moves each segment by the same distance. */
	for (index = 0; index < plan->segment_count; index++) {
		/* Recovers the linked address from the virtual one, then rebases. */
		segment = &plan->segment[index];
		segment->physical = segment->virtual_address - AMD64_KERNEL_LINK_VIRT_BASE + delta;
	}

	/* Succeeded: the plan is placed at the base. */
	return 1;
}

/* Reports whether a file header is an amd64 executable's with its program headers in reach. */
static int
header_acceptable(
	const struct elf64_header *header)
{
	/* The file is not a little-endian ELF64 file of the current version. */
	if (header->ident[0] != 0x7f ||
	    header->ident[1] != 'E' ||
	    header->ident[2] != 'L' ||
	    header->ident[3] != 'F' ||
	    header->ident[4] != 2 ||
	    header->ident[5] != 1 ||
	    header->ident[6] != 1)
		return 0;

	/* The file is not an amd64 executable. */
	if (header->type != ET_EXEC ||
	    header->machine != EM_X86_64 ||
	    header->version != 1 ||
	    header->ehsize != sizeof(*header))
		return 0;

	/* The program headers are not of the expected size or count. */
	if (header->phentsize != sizeof(struct elf64_program_header) ||
	    header->phnum == 0 ||
	    header->phnum > ZBL_ELF_MAX_SEGMENTS)
		return 0;

	/* The program headers do not all lie within the bytes the loader read. */
	if (header->phoff > ZBL_ELF_HEADER_BYTES ||
	    (uint64_t)header->phnum * header->phentsize > ZBL_ELF_HEADER_BYTES - header->phoff)
		return 0;

	/* Succeeded: the header can be planned from. */
	return 1;
}

/* Reports whether a PT_LOAD segment fits the plan, the link window and the file. */
static int
segment_acceptable(
	const struct elf64_program_header *ph,
	uint32_t segment_count,
	uint64_t file_size_limit)
{
	/* The plan is full, or the segment's sizes are inconsistent. */
	if (segment_count == ZBL_ELF_MAX_SEGMENTS ||
	    ph->filesz > ph->memsz ||
	    ph->memsz == 0)
		return 0;

	/* The segment is not linked inside the kernel's physical window. */
	if (ph->paddr < AMD64_KERNEL_LINK_PHYS_START ||
	    ph->paddr >= KERNEL_LINK_PHYS_LIMIT ||
	    ph->memsz > KERNEL_LINK_PHYS_LIMIT - ph->paddr)
		return 0;

	/* The segment's virtual address is not its physical one in the kernel's window. */
	if (ph->vaddr != AMD64_KERNEL_LINK_VIRT_BASE + ph->paddr)
		return 0;

	/* The segment is not page aligned, or its alignment is not a power of two. */
	if ((ph->paddr & 0xfffU) != 0 ||
	    ph->align < 0x1000U ||
	    (ph->align & (ph->align - 1U)) != 0)
		return 0;

	/* The segment's file part lies outside the file. */
	if (ph->offset > file_size_limit ||
	    ph->filesz > file_size_limit - ph->offset)
		return 0;

	/* Succeeded: the segment can be loaded. */
	return 1;
}
