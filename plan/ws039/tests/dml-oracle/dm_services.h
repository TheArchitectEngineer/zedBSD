/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the reference's display mode library (Linux v6.19
 * drivers/gpu/drm/amd/display/dc/dml, built on the host from the temp tree
 * by amdrdna-dml-test.sh, never kept in the tree) needs from the kernel's
 * display services, in host terms: the integer and Boolean types, the
 * memory calls, and the checks and the log calls as nothing.
 */

#ifndef PLAN_WS039_TESTS_DML_ORACLE_DM_SERVICES_H
#define PLAN_WS039_TESTS_DML_ORACLE_DM_SERVICES_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ASSERT(condition) ((void)0)
#define ASSERT_CRITICAL(condition) ((void)0)
#define BREAK_TO_DEBUGGER() ((void)0)
#define DC_LOG_DML(...) ((void)0)
#define dm_error(...) ((void)0)
#define dm_output_to_console(...) ((void)0)
#define DC_FP_START() ((void)0)
#define DC_FP_END() ((void)0)
#define dc_assert_fp_enabled() ((void)0)

struct dal_logger;

#endif
