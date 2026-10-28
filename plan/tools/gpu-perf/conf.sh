# gpu-perf: the settings every script of this directory reads first, sourced (". plan/tools/gpu-perf/conf.sh") from the
# top of the tree.  GPU_PERF_MACHINE names the test machine (machines/NAME.conf: how its systems are reached and
# booted, where the programs and references come from), and the machine's file names its GPU driver
# (drivers/NAME.conf: the driver's status command for the guard, its timing marks, its control program); a variable
# already set in the environment is kept.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
GPU_PERF=${GPU_PERF:-plan/tools/gpu-perf}
GPU_PERF_MACHINE=${GPU_PERF_MACHINE:-rdna}
. "$GPU_PERF/machines/$GPU_PERF_MACHINE.conf"
. "$GPU_PERF/drivers/$GPU_PERF_DRIVER.conf"
