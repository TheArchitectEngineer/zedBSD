#!/bin/sh
# Builds Mesa 25.0.7's Intel tools (brw_asm, brw_disasm) and unpacks its genxml, for the byte round trip and the
# genxml decode of plan/ws101/tests/host/run.sh, plan/ws101/tests/glsl/run.sh and plan/ws075/tests/guard/run.sh.
# The tarball is fetched and checked against the SHA-256 of Mesa's release notes (docs.mesa3d.org/relnotes/25.0.7);
# nothing is copied into the tree.  About two minutes on the host.
#
#   plan/ws101/tests/host/mesa-tools.sh [DIR]     (default build/mesa-tools; prints the BRW_TOOLS and GENXML to set)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
dir=${1:-$repo/build/mesa-tools}
version=25.0.7
sha256=592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6
mkdir -p "$dir"
cd "$dir"
[ -f mesa-$version.tar.xz ] || curl -sSfLO https://archive.mesa3d.org/mesa-$version.tar.xz
echo "$sha256  mesa-$version.tar.xz" | sha256sum -c -
[ -d mesa-$version ] || tar xf mesa-$version.tar.xz
# Only the Intel tools: no drivers, no GL or EGL, no LLVM (the tools need expat).
[ -f build-asm/build.ninja ] || meson setup build-asm mesa-$version -Dbuildtype=release -Dtools=intel \
	-Dgallium-drivers= -Dvulkan-drivers= -Dplatforms= -Dglx=disabled -Degl=disabled -Dgles1=disabled \
	-Dgles2=disabled -Dopengl=false -Dllvm=disabled -Dzstd=disabled -Dvalgrind=disabled -Dlibunwind=disabled \
	-Dshader-cache=disabled -Dintel-elk=false > setup.log
ninja -C build-asm src/intel/compiler/brw_asm src/intel/compiler/brw_disasm > ninja.log
echo "BRW_TOOLS=$dir/build-asm/src/intel/compiler"
echo "GENXML=$dir/mesa-$version/src/intel/genxml"
