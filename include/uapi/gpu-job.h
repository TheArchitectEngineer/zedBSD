/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Producer-owned GPU work reservations with autonomous completion supervision.
 */

#ifndef KERN_UAPI_GPU_JOB_H
#define KERN_UAPI_GPU_JOB_H

#include <uapi/gpu.h>

#define GPU_CAP_JOB		8192U
#define GPU_JOB_CANCEL_FAULT	1U

#define GPU_JOB_RESERVE		_IOWR('G', 34, struct gpu_job_reserve)
#define GPU_JOB_COMMIT		_IOW('G', 35, struct gpu_job_action)
#define GPU_JOB_CANCEL		_IOW('G', 36, struct gpu_job_action)

/*
 * One reservation owns completion storage and its backend marker before native
 * submission. A nonzero queue timeline identifies the work's completion domain.
 * Optional fd/generation use -1/zero when only the command record is observed.
 * Flags, reserved and the output sequence begin at zero. GPU_COMMAND_WAIT owns
 * terminal observation and consumption of successfully committed sequences.
 * Reserved generations have a producer deadline but are not GPU dependencies.
 */
struct gpu_job_reserve {
	uint32_t version;
	uint32_t size;
	int32_t fd;
	uint32_t flags;
	uint64_t generation;
	uint32_t timeline;
	uint32_t reserved;
	uint64_t sequence;
};

/*
 * One exact open description commits or cancels its retained reservation.
 * COMMIT follows native success and publishes without allocating marker storage.
 * CANCEL with flags zero is valid only after definite native nonacceptance; it
 * discards the record and leaves the optional fence unsignaled and unbound.
 * CANCEL_FAULT instead reports uncertain native work or device loss, retaining
 * terminal ERROR for observation and uncertain DMA until checked recovery.
 * COMMIT accepts flags zero only. The reserved field always begins at zero.
 */
struct gpu_job_action {
	uint32_t version;
	uint32_t size;
	uint64_t sequence;
	uint32_t flags;
	uint32_t reserved;
};

#endif
