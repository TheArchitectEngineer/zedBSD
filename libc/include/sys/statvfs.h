/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

#ifndef LIBC_SYS_STATVFS_H
#define LIBC_SYS_STATVFS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef uint64_t fsblkcnt_t;
typedef uint64_t fsfilcnt_t;

#define ST_RDONLY 0x00000001UL
#define ST_NOSUID 0x00000002UL
/*
 * The filesystem is held by this machine rather than reached over a
 * network.  This is not one of the two flags the standard defines; it is
 * what this system reports for the question portable software asks about a
 * path before deciding whether reading it twice is cheap.  MNT_LOCAL in
 * <sys/mount.h> says the same thing about a mount, and the two are kept
 * apart because f_flag carries ST_ values and a mount carries MNT_ ones.
 */
#define ST_LOCAL  0x00000004UL

struct statvfs {
	uint64_t f_bsize;
	uint64_t f_frsize;
	fsblkcnt_t f_blocks;
	fsblkcnt_t f_bfree;
	fsblkcnt_t f_bavail;
	fsfilcnt_t f_files;
	fsfilcnt_t f_ffree;
	fsfilcnt_t f_favail;
	uint64_t f_fsid;
	uint64_t f_flag;
	uint64_t f_namemax;
};

int statvfs(const char *, struct statvfs *);
int fstatvfs(int, struct statvfs *);

#ifdef __cplusplus
}
#endif

#endif
