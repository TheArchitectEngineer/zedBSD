#!/bin/sh
# ws122-p002: the Venus guest of plan/ws035/tests/zdesktop-guest.sh with a sound card: an ICH9 HDA whose output
# QEMU records to a WAV file ($GUEST_RUNTIME/sound.wav, 48 kHz S16 stereo), so videoplayer-p002.sh can check that
# the player's sound reached the card.  Runtime directory build/ws122-run.
#
#   plan/ws122/tests/videoplayer-guest.sh start IMAGE
#   plan/ws122/tests/videoplayer-guest.sh stop
# Host set-up: that of plan/ws035/tests/zdesktop-guest.sh (the Venus renderer, vgem).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
. plan/tools/guest/venus-hostmem.sh
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws122-run}"
export LIBGL_ALWAYS_SOFTWARE=1
export MESA_LOADER_DRIVER_OVERRIDE=zink
export VK_DRIVER_FILES="${VK_DRIVER_FILES:-/usr/share/vulkan/icd.d/lvp_icd.json}"
VENUS_RENDERER=${VENUS_RENDERER:-$PWD/build/ws035-sq-venus/install}
export RENDER_SERVER_EXEC_PATH="${RENDER_SERVER_EXEC_PATH:-$VENUS_RENDERER/libexec/virgl_render_server}"
export LD_LIBRARY_PATH="$VENUS_RENDERER/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
case "${1:-start}" in
start)
	image=${2:?image}
	python3 plan/tools/guest/guest.py stop >/dev/null
	mkdir -p "$GUEST_RUNTIME"
	printf '%s\n' "$GUEST_RUNTIME" > build/.zdesktop-guest-runtime
	rm -f "$GUEST_RUNTIME/sound.wav"
	exec python3 plan/tools/guest/guest.py start "$image" \
	    --qemu-extra "-object memory-backend-memfd,id=mem,size=8G,share=on -machine memory-backend=mem -device virtio-gpu-gl-pci,id=venus,venus=on,blob=on,hostmem=$VENUS_HOSTMEM,max_outputs=1 -display egl-headless,rendernode=/dev/dri/renderD128 -vnc unix:$GUEST_RUNTIME/vnc.sock,display=venus -device usb-tablet,bus=xhci.0,port=4 -audiodev wav,id=sound,path=$GUEST_RUNTIME/sound.wav,out.frequency=48000,out.channels=2,out.format=s16 -device ich9-intel-hda,id=hda -device hda-output,bus=hda.0,audiodev=sound,mixer=off"
	;;
*)
	exec python3 plan/tools/guest/guest.py "$@"
	;;
esac
