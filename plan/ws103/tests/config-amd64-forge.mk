# ws103-p004: the criteria image (plan/ws099/tests/config-amd64-criteria.mk) with the GPU buffer forgery test
# (/bin/gpu-forge-test) and wltest (a real client after the refusal).  Build: sh plan/ws103/tests/build-forge-image.sh [BUILD]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/ws099/tests/config-amd64-criteria.mk
ZEDBSD_USER_PROGRAMS += gpu-forge-test wltest
