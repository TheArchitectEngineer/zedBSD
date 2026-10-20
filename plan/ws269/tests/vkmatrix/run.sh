#!/bin/sh
# Builds userland/tests/vkmatrix on the build host against its Vulkan
# loader and runs it on the host's software renderer (lavapipe), the
# reference the board's driver is held to.
#   plan/ws269/tests/vkmatrix/run.sh OUTDIR
set -eu
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../../.." && pwd)
out=$1
mkdir -p "$out"
cc -std=c11 -O1 -Wall -Wextra -Werror -o "$out/vkmatrix" "$top/userland/tests/vkmatrix/main.c" -lvulkan
icd=/usr/share/vulkan/icd.d/lvp_icd.json
VK_ICD_FILENAMES=$icd VK_DRIVER_FILES=$icd timeout 120 "$out/vkmatrix"
