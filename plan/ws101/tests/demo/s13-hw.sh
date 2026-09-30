#!/bin/sh
# ws101-p015: the demonstration's scene S13 by hand's steps on the 5330's passthrough: the image of
# plan/ws101/tests/demo/build-s13-image.sh boots to the session (kei logs in by itself), the Kei button opens App
# Home, its Terminal tile starts Terminal, and "sh /usr/share/gpudemo/s13.sh" is typed there, as the presenter does;
# the screen is taken after Terminal opened and after the script had time to end.  Drives the run with
# plan/ws075/tests/hdmi-h4-hw.sh (the machine's lock is held from start to stop, H4_MINUTES bounds QEMU).  The shots
# are OUTDIR/shots/*-live.png; read them to judge (the script prints the times and "The results are the same").
#
#   plan/ws101/tests/demo/s13-hw.sh [IMAGE] [OUTDIR]     (defaults build/ws101-p015-demo/hdd-image.img,
#                                                         build/ws101-p015-s13-hw; BOOT_S seconds to the session)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../../.."
image=${1:-build/ws101-p015-demo/hdd-image.img}
out=${2:-build/ws101-p015-s13-hw}
boot=${BOOT_S:-150}
h4=plan/ws075/tests/hdmi-h4-hw.sh
export H4_MINUTES=${H4_MINUTES:-12}
mkdir -p "$out"
$h4 start "$image" "$out" || exit 1
sleep "$boot"
timeout 60 $h4 ctl shot session | tail -1
# App Home from the Kei button, then its Terminal tile (the fourth of plan/ws035/demo/apps.conf).
timeout 90 $h4 ctl pointer move 22 16 sleep 100 down up sleep 1500 move 1031 386 sleep 150 down up sleep 8000 > /dev/null
timeout 60 $h4 ctl shot terminal | tail -1
# The presenter's command.
timeout 60 $h4 ctl keys "'sh /usr/share/gpudemo/s13.sh\n'" > /dev/null
sleep "${RUN_S:-60}"
timeout 60 $h4 ctl shot s13 | tail -1
$h4 stop "$out"
ls "$out"/shots/*-live.png 2>/dev/null
