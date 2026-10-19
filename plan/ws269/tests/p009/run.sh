#!/bin/sh
# Host check of ws269-p009: the PowerVR driver's SPIR-V subset on modules
# glslc compiles from shaders/*.comp, the fill program built for it, and
# the Vulkan executor decoding frames that libvulkan's own encoders write.
#   plan/ws269/tests/p009/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws269/tests/p009
out=$1
mkdir -p "$out/inc"
for shader in "$here"/shaders/*.comp; do
	name=$(basename "$shader" .comp)
	glslc --target-env=vulkan1.0 -o "$out/$name.spv" "$shader"
done
cc -std=c11 -D_GNU_SOURCE -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/spirv-fill.c" \
	"$root/src/drivers/gpu/powervr/usc-encode.c" \
	"$root/src/drivers/gpu/powervr/pds-encode.c" \
	"$root/src/drivers/gpu/powervr/compute-program.c" \
	"$here/spirv-host-test.c" -o "$out/spirv-host-test"
"$out/spirv-host-test" "$out"
# The executor: the kernel side (with host stand-ins) and libvulkan's encoders.
mkdir -p "$out/vk"
ln -sfn "$root/include/libc/vulkan" "$out/inc/vulkan"
for file in wire codec; do
	cc -std=c11 -O1 -Wall -Wno-unused-parameter -I"$root/include" -I"$out/inc" -I"$root/userland/desktop/libvulkan" \
		-c "$root/userland/desktop/libvulkan/$file.c" -o "$out/vk/lib-$file.o"
done
cc -std=c11 -O1 -Wall -Wextra -Werror -I"$root/include" -I"$out/inc" -I"$root/userland/desktop/libvulkan" \
	-c "$here/frame-host.c" -o "$out/vk/frame-host.o"
objects=""
for file in vk-object vk-stream vk-device vk-memory vk-layout vk-command vk-queue spirv-fill compute-program usc-encode pds-encode; do
	cc -std=c11 -O1 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
		-c "$root/src/drivers/gpu/powervr/$file.c" -o "$out/vk/$file.o"
	objects="$objects $out/vk/$file.o"
done
cc -std=c11 -O1 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	-c "$root/src/drivers/gpu/i915/render/codec.c" -o "$out/vk/i915-codec.o"
cc -std=c11 -O1 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" -I"$here" \
	-c "$here/vk-host-test.c" -o "$out/vk/vk-host-test.o"
cc -o "$out/vk-host-test" "$out/vk/vk-host-test.o" $objects "$out/vk/i915-codec.o" \
	"$out/vk/frame-host.o" "$out/vk/lib-wire.o" "$out/vk/lib-codec.o"
"$out/vk-host-test" "$out"
