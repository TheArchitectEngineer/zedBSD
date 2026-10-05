#!/bin/sh
# ws139-p001: builds the performance image (plan/ws139/tests/config-amd64-perf.mk: the Settings image with the input
# method and the System Monitor) into BUILD/hdd-image.img, through the Settings image's build (which adds the
# wallpaper and apps.conf).
#   sh plan/ws139/tests/build-perf-image.sh BUILD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
[ $# -eq 1 ] || { echo "usage: build-perf-image.sh BUILD" >&2; exit 2; }
SETTINGS_CONFIG=plan/ws139/tests/config-amd64-perf.mk exec sh plan/ws089/tests/build-settings-image.sh "$1"
