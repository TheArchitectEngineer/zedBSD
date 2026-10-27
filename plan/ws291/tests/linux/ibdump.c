/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ibdump: an LD_PRELOAD library for Linux that writes every command buffer
 * a Vulkan driver (RADV) submits to the GFX ring, with the buffers it
 * chains to, as one hexadecimal word a line (ws291-p005: the reference of
 * the packets a draw needs).  It follows the GEM handles' GPU addresses
 * (GEM_VA) and CPU mappings (GEM_MMAP, then mmap) to read the buffers.
 *
 *     LD_PRELOAD=./ibdump.so IBDUMP=/tmp/ib ./app      -> /tmp/ib.<n> per submission
 *
 * Build: gcc -O2 -shared -fPIC -o ibdump.so ibdump.c -ldl
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <libdrm/amdgpu_drm.h>

#define IBDUMP_BOS		4096
#define IBDUMP_DEPTH		4
#define IBDUMP_OP_CHAIN		0x3fU

/* A DRM request by its number alone (libdrm may build it with another direction than the header's). */
#define IBDUMP_IS(request, number)	(((request) & 0xffffU) == (('d' << 8) | (DRM_COMMAND_BASE + (number))))

/* One buffer object: its handle, mmap offset, CPU address and GPU address. */
struct ibdump_bo {
	uint32_t handle;
	uint64_t offset;
	void *cpu;
	uint64_t cpu_bytes;
	uint64_t va;
	uint64_t va_bytes;
};

static struct ibdump_bo ibdump_bos[IBDUMP_BOS];
static int ibdump_descriptor = -1;
static int (*ibdump_real_ioctl)(int, unsigned long, ...);
static void *(*ibdump_real_mmap)(void *, size_t, int, int, int, off_t);
static unsigned ibdump_count;
static unsigned ibdump_submissions;

/* The record of a handle, made when it is first seen. */
static struct ibdump_bo *
ibdump_bo(
	uint32_t handle)
{
	unsigned index;

	for (index = 0U; index < ibdump_count; index++) {
		if (ibdump_bos[index].handle == handle)
			return &ibdump_bos[index];
	}
	if (ibdump_count == IBDUMP_BOS)
		return NULL;
	memset(&ibdump_bos[ibdump_count], 0, sizeof(ibdump_bos[0]));
	ibdump_bos[ibdump_count].handle = handle;
	return &ibdump_bos[ibdump_count++];
}

/* The CPU address of a GPU address, or NULL when no mapped buffer holds it. */
static const uint32_t *
ibdump_find(
	uint64_t va,
	uint64_t bytes)
{
	unsigned index;
	struct ibdump_bo *bo;

	for (index = 0U; index < ibdump_count; index++) {
		bo = &ibdump_bos[index];
		if (bo->va == 0U || va < bo->va || va + bytes > bo->va + bo->va_bytes)
			continue;

		/* A buffer the driver does not keep mapped: mapped here, read only. */
		if (bo->cpu == NULL && ibdump_descriptor >= 0 && ibdump_real_ioctl != NULL && ibdump_real_mmap != NULL) {
			union drm_amdgpu_gem_mmap request;
			void *cpu;

			memset(&request, 0, sizeof(request));
			request.in.handle = bo->handle;
			if (ibdump_real_ioctl(ibdump_descriptor, DRM_IOCTL_AMDGPU_GEM_MMAP, &request) == 0) {
				cpu = ibdump_real_mmap(NULL, bo->va_bytes, PROT_READ, MAP_SHARED, ibdump_descriptor, (off_t)request.out.addr_ptr);
				if (cpu != MAP_FAILED) {
					bo->cpu = cpu;
					bo->cpu_bytes = bo->va_bytes;
				}
			}
		}
		if (bo->cpu != NULL && va - bo->va + bytes <= bo->cpu_bytes)
			return (const uint32_t *)((const char *)bo->cpu + (va - bo->va));
	}
	return NULL;
}

/* Writes a buffer's words, and those of the buffers it chains to. */
static void
ibdump_write(
	FILE *file,
	uint64_t va,
	uint32_t words,
	unsigned depth)
{
	const uint32_t *ib;
	uint32_t index;
	uint32_t header;
	uint32_t count;

	va &= 0xffffffffffffULL;
	ib = ibdump_find(va, (uint64_t)words * 4U);
	if (ib == NULL && depth == 0U) {
		unsigned index;

		for (index = 0U; index < ibdump_count; index++)
			fprintf(file, "# bo %u offset 0x%llx cpu %p bytes 0x%llx va 0x%llx bytes 0x%llx\n", ibdump_bos[index].handle,
				(unsigned long long)ibdump_bos[index].offset, ibdump_bos[index].cpu, (unsigned long long)ibdump_bos[index].cpu_bytes,
				(unsigned long long)ibdump_bos[index].va, (unsigned long long)ibdump_bos[index].va_bytes);
	}
	fprintf(file, "# ib va 0x%llx words %u%s (buffers %u)\n", (unsigned long long)va, words, ib == NULL ? " (not mapped)" : "", ibdump_count);
	if (ib == NULL)
		return;
	for (index = 0U; index < words; index++)
		fprintf(file, "%08x\n", ib[index]);

	/* The chained buffers (type-3 INDIRECT_BUFFER). */
	if (depth >= IBDUMP_DEPTH)
		return;
	for (index = 0U; index < words;) {
		header = ib[index];
		if ((header >> 30) != 3U) {
			index++;
			continue;
		}
		count = ((header >> 16) & 0x3fffU) + 1U;
		if (((header >> 8) & 0xffU) == IBDUMP_OP_CHAIN && index + 3U < words)
			ibdump_write(file, ((uint64_t)(ib[index + 2U] & 0xffffU) << 32) | ib[index + 1U], ib[index + 3U] & 0xfffffU, depth + 1U);
		index += count + 1U;
	}
}

/* The interposed mmap: a mapping of a GEM offset gives its buffer a CPU address. */
void *
mmap(
	void *address,
	size_t length,
	int protection,
	int flags,
	int descriptor,
	off_t offset)
{
	static void *(*real)(void *, size_t, int, int, int, off_t);
	void *result;
	unsigned index;

	if (real == NULL)
		real = (void *(*)(void *, size_t, int, int, int, off_t))dlsym(RTLD_NEXT, "mmap");
	ibdump_real_mmap = real;
	result = real(address, length, protection, flags, descriptor, offset);
	if (result != MAP_FAILED) {
		for (index = 0U; index < ibdump_count; index++) {
			if (ibdump_bos[index].offset == (uint64_t)offset && offset != 0) {
				ibdump_bos[index].cpu = result;
				ibdump_bos[index].cpu_bytes = length;
			}
		}
	}
	return result;
}

/* The interposed munmap: a buffer no longer mapped there is mapped again when it is read. */
int
munmap(
	void *address,
	size_t length)
{
	static int (*real)(void *, size_t);
	unsigned index;

	if (real == NULL)
		real = (int (*)(void *, size_t))dlsym(RTLD_NEXT, "munmap");
	for (index = 0U; index < ibdump_count; index++) {
		if (ibdump_bos[index].cpu != NULL && (char *)ibdump_bos[index].cpu >= (char *)address &&
		    (char *)ibdump_bos[index].cpu < (char *)address + length)
			ibdump_bos[index].cpu = NULL;
	}
	return real(address, length);
}

/* mmap64 is mmap on 64-bit Linux. */
void *
mmap64(
	void *address,
	size_t length,
	int protection,
	int flags,
	int descriptor,
	off_t offset)
{
	return mmap(address, length, protection, flags, descriptor, offset);
}

/* The interposed ioctl: GEM_MMAP and GEM_VA recorded, CS dumped. */
int
ioctl(
	int descriptor,
	unsigned long request,
	...)
{
	static int (*real)(int, unsigned long, ...);
	union drm_amdgpu_gem_mmap *gem_mmap;
	struct drm_amdgpu_gem_va *gem_va;
	union drm_amdgpu_cs *cs;
	struct drm_amdgpu_cs_chunk *chunk;
	struct drm_amdgpu_cs_chunk_ib *ib;
	struct ibdump_bo *bo;
	const uint64_t *chunks;
	const char *base;
	char path[256];
	FILE *file;
	va_list arguments;
	void *argument;
	unsigned index;
	uint32_t handle;
	int result;

	va_start(arguments, request);
	argument = va_arg(arguments, void *);
	va_end(arguments);
	if (real == NULL)
		real = (int (*)(int, unsigned long, ...))dlsym(RTLD_NEXT, "ioctl");
	ibdump_real_ioctl = real;
	if (IBDUMP_IS(request, DRM_AMDGPU_CS))
		ibdump_descriptor = descriptor;

	/* A submission: its GFX buffers written before it runs. */
	if (IBDUMP_IS(request, DRM_AMDGPU_CS)) {
		cs = argument;
		base = getenv("IBDUMP");
		chunks = (const uint64_t *)(uintptr_t)cs->in.chunks;
		snprintf(path, sizeof(path), "%s.%u", base != NULL ? base : "/tmp/ib", ibdump_submissions++);
		file = fopen(path, "w");
		for (index = 0U; file != NULL && index < cs->in.num_chunks; index++) {
			chunk = (struct drm_amdgpu_cs_chunk *)(uintptr_t)chunks[index];
			if (chunk->chunk_id != AMDGPU_CHUNK_ID_IB)
				continue;
			ib = (struct drm_amdgpu_cs_chunk_ib *)(uintptr_t)chunk->chunk_data;
			fprintf(file, "# chunk ip %u flags 0x%x\n", ib->ip_type, ib->flags);
			ibdump_write(file, ib->va_start, ib->ib_bytes / 4U, 0U);
		}
		if (file != NULL)
			fclose(file);
	}

	/* GEM_MMAP's answer overwrites its handle (a union): the handle kept first. */
	handle = 0U;
	if (IBDUMP_IS(request, DRM_AMDGPU_GEM_MMAP))
		handle = ((union drm_amdgpu_gem_mmap *)argument)->in.handle;
	result = real(descriptor, request, argument);
	if (result != 0)
		return result;

	/* The mapping offset of a handle. */
	if (IBDUMP_IS(request, DRM_AMDGPU_GEM_MMAP)) {
		gem_mmap = argument;
		bo = ibdump_bo(handle);
		if (bo != NULL)
			bo->offset = gem_mmap->out.addr_ptr;
	}

	/* The GPU address of a handle. */
	if (IBDUMP_IS(request, DRM_AMDGPU_GEM_VA)) {
		gem_va = argument;
		if (gem_va->operation == AMDGPU_VA_OP_MAP) {
			bo = ibdump_bo(gem_va->handle);
			if (bo != NULL) {
				bo->va = (gem_va->va_address - gem_va->offset_in_bo) & 0xffffffffffffULL;
				bo->va_bytes = gem_va->map_size + gem_va->offset_in_bo;
			}
		}
	}
	return result;
}
