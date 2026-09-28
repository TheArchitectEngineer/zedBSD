#!/bin/sh
# ws075-p013 (H4): builds the demonstration image (plan/ws075/demo/config-demo-hdmi.mk): the graphical boot to the
# greeter and the session on the HDMI display (display=hdmi), App Home with the demonstration's applications
# (plan/ws035/demo/apps.conf: Files, Notes, Terminal, PDF Viewer, Browser, Model viewer, Gears, X terminal), and the fonts and the
# wallpaper, which are not in git (build/ws035-fonts/, build/ws035-wallpaper/).  The demonstration's accounts
# (ws035-p120, plan/ws035/demo/demo-accounts.sh): the person kei, shown as "Kei", logs in without a password (Enter
# at the greeter and at the lock screen); root has a password (DEMO_ROOT_PASSWORD, or a random one in BUILD/demo-accounts/root-password).  sessiond waits for the i915's GPU node while the kernel reports the device still attaching
# (hw.gpu.attaching, BUG-092).
#
#   plan/ws075/demo/build-demo-image.sh [BUILD] [passthrough] [MAKE ARGUMENTS...]     (default build/demo-hdmi)
#
# The image is BUILD/hdd-image.img; write it to a USB stick and boot the machine from it (UEFI), or edit the boot
# parameters afterwards in zedbsd.cfg on its ESP (docs/reference/kernel-boot-parameters.md).
# "passthrough" is the image for the 5330's QEMU passthrough (plan/ws075/tests/hdmi-h4-hw.sh): the guest sees no
# OpRegion there, so the kernel carries the machine's captured VBT (I915_TEST_VBT=y).  On the machine itself the VBT
# comes from its OpRegion and the word is left out.  Further words go to make, e.g.
# "ZEDBSD_BOOT_EXTRA_LINES=display=hdmi display.mode=1920x1080@60" or ZEDBSD_TEST_CPPFLAGS=-DI915_TEST_HDMI_ABSENT=1.
# Give each variant a build directory of its own: make does not see a change of the C flags.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/demo-hdmi}
[ $# -gt 0 ] && shift
vbt=n
if [ "${1:-}" = passthrough ]; then
	vbt=y
	shift
fi
extra="--file /etc/keiland/apps.conf=plan/ws035/demo/apps.conf"
for pair in \
	keiland.ttf=build/ws035-fonts/Inter.ttf \
	keiland-OFL.txt=build/ws035-fonts/OFL.txt \
	keiland-mono.ttf=build/ws035-fonts/JetBrainsMono-Regular.ttf \
	keiland-mono-OFL.txt=build/ws035-fonts/JetBrainsMono-OFL.txt \
	keiland-fallback.ttf=build/ws035-fonts/DroidSansFallbackFull.ttf \
	keiland-fallback-LICENSE.txt=build/ws035-fonts/DroidSansFallback-LICENSE.txt; do
	[ -f "${pair#*=}" ] && extra="$extra --file /usr/share/fonts/$pair"
done
[ -f build/ws035-wallpaper/wallpaper-1080.ppm ] && extra="$extra --file /usr/share/keiland/wallpaper.ppm=build/ws035-wallpaper/wallpaper-1080.ppm"
mkdir -p "$build"
accounts=$build/demo-accounts
plan/ws035/demo/demo-accounts.sh "$accounts"
extra="$extra --file /etc/passwd=$accounts/passwd --file /etc/group=$accounts/group --file /etc/shadow=$accounts/shadow"
make -j"$(nproc)" ZEDBSD_CONFIG=plan/ws075/demo/config-demo-hdmi.mk BUILD="$build" I915_TEST_VBT=$vbt \
	"ZEDBSD_TEST_EXTRA_FILES=$extra" ZEDBSD_TEST_IMAGE_TAG=demo-hdmi "$@" disk-image
echo "demo image: $build/hdd-image.img"
