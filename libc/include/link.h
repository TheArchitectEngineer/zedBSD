/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Walking the loaded objects of a process.
 *
 * Not POSIX, but the way a program reaches the program headers of everything
 * that is loaded.  An unwinder needs them to find the exception tables that
 * describe each frame, so a C++ program that throws depends on this.
 *
 * The names follow the System V ABI's generic supplement, so that portable
 * software compiles here unchanged.  Only what that walk requires is defined;
 * this is not a general ELF header.
 */

#ifndef LIBC_LINK_H
#define LIBC_LINK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The program header, and the address and half-word types that go with it,
 * at the width of this ABI.
 */
#ifdef KERN_USER_ABI_LP64

typedef uint64_t ElfW_Addr;
typedef uint16_t ElfW_Half;
typedef uint32_t ElfW_Word;

typedef struct {
	ElfW_Word p_type;
	ElfW_Word p_flags;
	uint64_t p_offset;
	ElfW_Addr p_vaddr;
	ElfW_Addr p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
} ElfW_Phdr;

#else

typedef uint32_t ElfW_Addr;
typedef uint16_t ElfW_Half;
typedef uint32_t ElfW_Word;

typedef struct {
	ElfW_Word p_type;
	uint32_t p_offset;
	ElfW_Addr p_vaddr;
	ElfW_Addr p_paddr;
	uint32_t p_filesz;
	uint32_t p_memsz;
	ElfW_Word p_flags;
	uint32_t p_align;
} ElfW_Phdr;

#endif

/* The spelling portable software uses for a width-matched ELF type. */
#define ElfW(type) ElfW_##type

/* The program header types this walk's callers look for. */
#define PT_NULL 0
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_PHDR 6
#define PT_TLS 7
#define PT_GNU_EH_FRAME 0x6474e550
#define PT_GNU_STACK 0x6474e551
#define PT_GNU_RELRO 0x6474e552

/*
 * One loaded object.
 *
 * dlpi_addr is what has to be added to an address as it appears in the file
 * to reach the address it was loaded at; it is zero for an object loaded
 * where it was linked for.  dlpi_adds and dlpi_subs count how many objects
 * have been loaded and unloaded over the life of the process, so a caller can
 * tell whether a cached answer is still good.
 */
struct dl_phdr_info {
	ElfW_Addr dlpi_addr;
	const char *dlpi_name;
	const ElfW_Phdr *dlpi_phdr;
	ElfW_Half dlpi_phnum;
	unsigned long long dlpi_adds;
	unsigned long long dlpi_subs;
	size_t dlpi_tls_modid;
	void *dlpi_tls_data;
};

/*
 * Calls back once for each loaded object until the callback returns a
 * non-zero value, which is then returned.  Returns zero when every object was
 * visited.  The set of objects does not change during the walk.
 */
int dl_iterate_phdr(int (*)(struct dl_phdr_info *, size_t, void *), void *);

#ifdef __cplusplus
}
#endif

#endif
