/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

#ifndef KERN_UAPI_SYSCTL_H
#define KERN_UAPI_SYSCTL_H

#include <stdint.h>

#define CTL_MAXNAME	8U

#define CTL_SYSCTL	0
#define CTL_HW	1
#define CTL_KERN	2
#define CTL_VFS	3

#define CTL_SYSCTL_NAME2OID	1
#define CTL_SYSCTL_NEXT	2
#define CTL_SYSCTL_OIDNAME	3

#define VFS_BUFCACHE	1
#define VFS_IO	2
#define VFS_CACHE_MEMORY	3
#define VFS_WRITEBACK	4
#define VFS_READAHEAD	5
#define VFS_READAHEAD_STATS	1
#define VFS_WRITEBACK_STATS	1
#define VFS_WRITEBACK_CONTROL	2
#define VFS_CACHE_MEMORY_STATS	1
#define VFS_CACHE_MEMORY_TARGET	2
#define VFS_IO_STATS	1
#define VFS_BUFCACHE_MAX_BYTES	1
#define VFS_BUFCACHE_CURRENT_BYTES	2
#define VFS_BUFCACHE_DIRTY_BYTES	3
#define VFS_BUFCACHE_STATS	4

#define HW_NCPU	1
#define HW_NCPUONLINE	2
#define HW_MEMORY_STATS	3
/*
 * hw.gpu.attaching (uint32_t): how many GPU devices a driver has attached but
 * not yet published a node (/dev/gpuN) for or given up on.  The graphical
 * login waits for /dev/gpu0 only while it is nonzero (BUG-092).
 */
#define HW_GPU_ATTACHING	4
/*
 * hw.gpu.start (uint64_t): how many GPU devices a driver holds for a start
 * root asks for (the i915 with the boot parameter i915.start=manual).
 * Writing 1 as the superuser starts them; with none held the write fails
 * with ENODEV.
 */
#define HW_GPU_START	5
/*
 * hw.cputimes: each CPU's time since boot, counted in clock ticks of hz a
 * second (ws134-p005).  The value is a struct cpu_times_header followed by
 * count struct cpu_times_entry, one a CPU in the order of their numbers.
 * Read-only.  A buffer too small fails with ENOMEM and the length needed;
 * a reader asks for the length first (no buffer) or tries again.
 *
 * Each tick is charged to what its CPU was doing: user (a user thread in
 * user mode), system (a thread in the kernel, or a kernel thread), idle
 * (the CPU's idle thread), or other (between threads: the running thread
 * was going to sleep or leaving).  Interrupts are not counted apart: an
 * interrupt or a page fault that stopped a user thread is that thread's
 * user time.  A CPU that is not online has every count 0.
 */
#define HW_CPUTIMES	6

/* The version of the hw.cputimes layout. */
#define CPU_TIMES_VERSION	1U

/*
 * The head of hw.cputimes: the layout's version, the header's size, one
 * entry's size, how many entries follow, and the ticks a second.  Every
 * field has a fixed width, so one layout serves ILP32 and LP64 processes.
 */
struct cpu_times_header {
	uint32_t version;
	uint32_t struct_size;
	uint32_t element_size;
	uint32_t count;
	uint32_t hz;
	uint32_t reserved;
};

/* One CPU's ticks in hw.cputimes. */
struct cpu_times_entry {
	uint64_t user;
	uint64_t system;
	uint64_t idle;
	uint64_t other;
};

_Static_assert(sizeof(struct cpu_times_header) == 24U,
    "hw.cputimes header ABI must be identical on ILP32 and LP64");
_Static_assert(sizeof(struct cpu_times_entry) == 32U,
    "hw.cputimes entry ABI must be identical on ILP32 and LP64");

/* Firmware RAM and actually managed RAM are distinct. */
#define MEMORY_STATS_VERSION 2U
struct memory_stats {
	uint32_t version;
	uint32_t boot_ranges_valid;
	uint64_t boot_range_count;
	uint64_t boot_usable_bytes;
	uint64_t boot_highest_end;
	uint64_t boot_usable_highest_end;
	uint64_t direct_mapped_bytes;
	uint64_t allocator_initial_bytes;
	uint64_t physical_managed_bytes;
	uint64_t physical_reserved_bytes;
	uint64_t physical_allocated_bytes;
	uint64_t physical_free_bytes;
	uint64_t boot_reclaim_bytes;
	uint64_t allocator_metadata_bytes;
	uint64_t allocator_scan_words;
	uint64_t allocator_max_extent_scan_words;
	uint64_t allocator_max_irqoff_cycles;
	uint32_t boot_memory_source;
	uint32_t reserved;
};

#define KERN_MSGBUF	1
#define KERN_MSGBUF_SIZE	2
#define KERN_MSGBUF_DROPPED	3
#define KERN_HOSTNAME	4
#define KERN_BOOT_FIRMWARE 5
#define KERN_BOOT_CONFIGURATION 6
#define KERN_BOOT_CONFIG_MATCHES 7
#define KERN_BOOT_ROOT_IMAGE 8
/* The login= boot parameter (ws035-p098): "graphical", "console", or "" when it was not given. */
#define KERN_BOOT_LOGIN 9
#define ROOT_IMAGE_VERSION 1U
#define ROOT_IMAGE_OVERLAY 1U
#define ROOT_IMAGE_MOUNTED 2U
#define ROOT_IMAGE_READ_ONLY 4U
#define ROOT_IMAGE_LOOP_READ_ONLY 8U

/* One live root/lower/loop observation; zero flags denotes a native root. */
struct root_image_info {
	uint32_t version;
	uint32_t flags;
	uint64_t loop_device;
	uint64_t backing_device;
	uint64_t backing_inode;
	uint64_t backing_bytes;
};
#define KERN_HOST_NAME_MAX	64U

struct bufcache_stats {
	uint64_t max_bytes;
	uint64_t current_bytes;
	uint64_t data_bytes;
	uint64_t metadata_bytes;
	uint64_t dirty_bytes;
	uint64_t buffers;
	uint64_t hits;
	uint64_t misses;
	uint64_t read_bios;
	uint64_t write_bios;
	uint64_t evictions;
	uint64_t waits;
	uint64_t writeback_errors;
	uint64_t capacity_failures;
	uint64_t physical_failures;
};

#endif
