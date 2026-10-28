#!/bin/sh
# The console's leaving of the boot framebuffer in QEMU: builds the test images the standard way
# (plan/tools/guest/test-image.sh with the test's config) and runs plan/ws290/tests/text-end-qemu.py on them.
# Only the summary is printed; the details are in each scene's directory.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
#   plan/ws290/tests/text-end-qemu.sh trigger|core|build-trigger|build-core
#
# trigger: config-amd64-text-end.mk (BUILD=build/p017-text-end), the guest's ports from TRIGGER_PORTS
#   ("SSH QMP GDB", default "22240 22241 22242") and its directories below build/tmp/p3-qemu-b.
# core: config-amd64-text-end-gpu.mk (BUILD=build/p017-text-end-gpu), ports from CORE_PORTS (default
#   "22230 22231 22232"), directories below build/tmp/p3-qemu-a, the Venus renderer from VENUS_RENDERER (default
#   build/tmp/p3-qemu-a/venus, a copy of the renderer plan/ws035/tests/zdesktop-guest.sh describes; /dev/dri/renderD128
#   must be usable).
# build-*: only the image.  SKIP_BUILD=1 runs on the image already built.
# Nothing is removed: each run writes a new directory.
set -eu
cd "$(dirname -- "$0")/../../.."
scene=${1:-}
case $scene in
trigger|build-trigger)
	config=plan/ws290/tests/config-amd64-text-end.mk
	build=build/p017-text-end
	base=build/tmp/p3-qemu-b
	set -- ${TRIGGER_PORTS:-22240 22241 22242}
	extra=
	;;
core|build-core)
	config=plan/ws290/tests/config-amd64-text-end-gpu.mk
	build=build/p017-text-end-gpu
	base=build/tmp/p3-qemu-a
	set -- ${CORE_PORTS:-22230 22231 22232}
	extra="--venus ${VENUS_RENDERER:-$base/venus}"
	;;
*)
	echo "usage: text-end-qemu.sh trigger|core|build-trigger|build-core" >&2
	exit 2
	;;
esac
ssh_port=$1
qmp_port=$2
gdb_port=$3
mkdir -p "$base"

# The image: the test's config and the files of the tree, nothing an older build made.
if [ "${SKIP_BUILD:-0}" != 1 ]; then
	log=$(mktemp "$base/build-log.XXXXXX")
	if [ "$config" = plan/ws290/tests/config-amd64-text-end-gpu.mk ]; then
		set -- --file /usr/share/keiland/wallpaper.png=userland/desktop/wallpapers/Birch-Lake.png
	else
		set --
	fi
	if ! timeout 14400 plan/tools/guest/test-image.sh "$config" "$build" "$@" > "$log" 2>&1; then
		echo "FAIL text-end-qemu $scene: the image did not build ($log)"
		exit 1
	fi
	warnings=$(grep -c 'warning:' "$log" || true)
	echo "image $build/hdd-image.img built, $warnings warning lines ($log)"
fi
case $scene in
build-*) exit 0 ;;
esac

# One guest, its directory new, its time bounded.
out=$(mktemp -d "$base/run.XXXXXX")
scene_out=$out/scene
# shellcheck disable=SC2086
timeout 1200 python3 -I plan/ws290/tests/text-end-qemu.py "$scene" --image "$build/hdd-image.img" \
	--symbols "$build/vmunix" --out "$scene_out" --ssh-port "$ssh_port" --qmp-port "$qmp_port" \
	--gdb-port "$gdb_port" $extra
