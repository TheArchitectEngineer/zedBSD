/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

#ifndef KERN_UAPI_FCNTL_H
#define KERN_UAPI_FCNTL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <sys/ioctl.h>

#define KERN_FILE_FORMAT_VERSION 1U

/* The opened description owns this mutation lease until its final close. */
struct kern_file_format_reserve {
	uint32_t version;
	uint32_t struct_size;
	uint64_t size_bytes;
	uint64_t reserved[2];
};

typedef char kern_file_format_size_check[
	sizeof(struct kern_file_format_reserve) == 32U ? 1 : -1];
typedef char kern_file_format_alignment_check[
	offsetof(struct kern_file_format_reserve, size_bytes) == 8U ? 1 : -1];

#define KERN_FILE_FORMAT_RESERVE \
	_IOW('f', 1, struct kern_file_format_reserve)

/*
 * Operations for flock(2).
 *
 * The lock is whole-file and belongs to the open file description, so a
 * descriptor passed through fork or dup shares one lock rather than taking
 * a second.  These are the values every system with this call uses.
 */
#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8

struct flock_record {
	int16_t type;
	int16_t whence;
	int32_t reserved0;
	int64_t start;
	int64_t length;
	int32_t pid;
	uint32_t reserved1;
};

#ifdef __cplusplus
}
#endif

#endif
