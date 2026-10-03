# ws103-p004: the criteria image (plan/ws099/tests/config-amd64-criteria.mk) with the GPU buffer forgery test
# (/bin/gpu-forge-test), wltest (a real client after the refusal) and acquire-fence-test (plan/ws035/tests/zdesktop-p054.sh).  Build: sh plan/tools/gpu-boundary/build-forge-image.sh [BUILD]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/ws099/tests/config-amd64-criteria.mk
ZEDBSD_USER_PROGRAMS += gpu-forge-test wltest acquire-fence-test
