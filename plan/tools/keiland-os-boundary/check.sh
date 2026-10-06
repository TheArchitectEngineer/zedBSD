#!/bin/sh
# Checks Keiland's common-source OS boundary and installation paths.
# Usage: sh plan/tools/keiland-os-boundary/check.sh (from any directory).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
# The work directory stays in build/tmp (2026-10-06 user: deleting is Q1's step; plan/tools/q1-clean.sh removes it).
mkdir -p build/tmp
work=$(mktemp -d "$(pwd)/build/tmp/os-boundary.XXXXXX")
status=0

# Collect the compositor's and the system library's sources: all of them, since their OS code is libkeiland-backend's
# (ws131-p009 moved the last of it, the GPU buffers).
find userland/desktop/libkeiland userland/desktop/wayland -name '*.[ch]' -print | LC_ALL=C sort > "$work/common"

# The compositor and libkeiland include no OS header (the evdev header choice is
# libkeiland-backend's keiland-backend-evdev.h since ws131-p007).
while IFS= read -r file; do
    awk '/^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](uapi\/|userland\/base\/)/ {print FILENAME ":" FNR ": " $0}' "$file"
done < "$work/common" > "$work/C1"

# No device ioctl in the compositor or libkeiland: the input devices are libkeiland-backend's (ws131-p007).
while IFS= read -r file; do
    awk '/ioctl[[:space:]]*\(/ {print FILENAME ":" FNR ": " $0}' "$file"
done < "$work/common" > "$work/C2"

# The zedBSD GPU wire layout and the kernel's GPU types stay inside libkeiland-backend-zedbsd (WS131 C3, ws131-p009):
# not in the compositor, libkeiland or the other backend trees (libvulkan, the driver, is not the desktop's boundary).
find userland/desktop/wayland userland/desktop/libkeiland userland/desktop/libkeiland-backend \
    userland/desktop/libkeiland-backend-linux userland/desktop/libkeiland-backend-freebsd -name '*.[ch]' -print |
while IFS= read -r file; do
    awk '/kwl_buffer_layout|gpu_image_descriptor|[<"]uapi\/gpu/ {print FILENAME ":" FNR ": " $0}' "$file"
done > "$work/C3"

# Inspect each literal so a system-shell exception cannot hide another path.
find userland/desktop -path '*/sessiond' -prune -o -path '*/keiland' -prune \
    -o -name '*.[ch]' -print |
while IFS= read -r file; do
    [ "$file" != userland/desktop/paths.h ] || continue
    awk '{
        remaining = $0
        while (match(remaining, /"([^"\\]|\\.)*"/)) {
            literal = substr(remaining, RSTART + 1, RLENGTH - 2)
            if (literal ~ /^\/(usr\/share|usr\/libexec|etc\/keiland|bin\/|usr\/bin\/)/ &&
                literal != "/bin/sh" && literal !~ /^\/bin\/sh /) {
                print FILENAME ":" FNR ": " $0
                break
            }
            remaining = substr(remaining, RSTART + RLENGTH)
        }
    }' "$file"
done > "$work/C4"

# Desktop headers are owned by desktop rather than libc.
find include/libc \( -name 'keiland.h' -o -name 'keiui.h' -o -name 'truetype.h' \
    -o -name 'browser.h' -o -name 'wayland*' -o -name 'xdg-shell*' \
    -o -name 'primary-selection*' -o -name 'tablet-unstable*' \) -print > "$work/C5"

# Linux selection belongs to the OS modules; the evdev header bridges constants.
find userland/desktop \
    \( -path '*/zedbsd' -o -path '*/linux' -o -path '*/freebsd' -o -path '*/wpa' \
    -o -path 'userland/desktop/libkeiland-backend-*' \) -prune \
    -o -name '*.[ch]' -print |
while IFS= read -r file; do
    [ "$file" != userland/desktop/libkeiland-backend/keiland-backend-evdev.h ] || continue
    # D14 (2026-10-03 user): Terminal's pty header, <pty.h> or FreeBSD's <libutil.h>, is chosen by one macro block
    # (ws131-p018 moved it into terminal/main.c); the block holds only those includes.
    if [ "$file" = userland/desktop/terminal/main.c ]; then
        awk '/^[[:space:]]*#[[:space:]]*(if|ifdef|elif).*(__linux__|__FreeBSD__)/ {
            getline next_line
            if (next_line ~ /^[[:space:]]*#[[:space:]]*include[[:space:]]*<libutil\.h>/) next
            print FILENAME ":" FNR - 1 ": " $0
        }' "$file"
        continue
    fi
    awk '/^[[:space:]]*#[[:space:]]*(if|ifdef|elif).*(__linux__|__FreeBSD__)/ {print FILENAME ":" FNR ": " $0}' "$file"
done > "$work/L1"

# Each OS module consumes only its own kernel and service interfaces (libkeiland-backend's trees, WS131).
find userland/desktop/libkeiland-backend-linux userland/desktop/libkeiland-backend-freebsd \
    userland/desktop/libkeiland-backend/wpa -name '*.[ch]' -print 2>/dev/null |
while IFS= read -r file; do
    awk '/^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](uapi\/|userland\/base\/(net|audiod)\/)/ {print FILENAME ":" FNR ": " $0}' "$file"
done > "$work/L2"
find userland/desktop/libkeiland-backend-zedbsd -name '*.[ch]' -print 2>/dev/null |
while IFS= read -r file; do
    awk '/^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](linux\/|drm\/|sound\/)/ {print FILENAME ":" FNR ": " $0}' "$file"
done > "$work/L3"

# The Linux Vulkan frontend never includes or compiles zedBSD Vulkan sources.
find userland/desktop/libvulkan-compat -type f \
    \( -name '*.[ch]' -o -name 'Makefile.linux' \) -print |
while IFS= read -r file; do
    awk '/^[[:space:]]*#[[:space:]]*include.*(userland\/desktop\/libvulkan\/|\.\.\/libvulkan\/)/ ||
         /^[^#]*userland\/desktop\/libvulkan\/.*\.c/ {print FILENAME ":" FNR ": " $0}' "$file"
done > "$work/L4"
make -s -f userland/desktop/keiland-linux.mk print-sources > "$work/linux-sources"
awk '/^userland\/desktop\/libvulkan\// {print "compiled Linux source: " $0}' "$work/linux-sources" >> "$work/L4"

# libkeiland-backend never reaches into the compositor or the applications' library (WS131 B1).
find userland/desktop/libkeiland-backend userland/desktop/libkeiland-backend-zedbsd \
    userland/desktop/libkeiland-backend-linux userland/desktop/libkeiland-backend-freebsd \
    -name '*.[ch]' -print 2>/dev/null |
while IFS= read -r file; do
    awk '/^[[:space:]]*#[[:space:]]*include[[:space:]]*([<"]userland\/desktop\/wayland\/|"[^"]*zwl[^"]*\.h"|<keiland\.h>|<keiui\.h>)/ {print FILENAME ":" FNR ": " $0}' "$file"
done > "$work/B1"

# Only the compositor uses libkeiland-backend: no other desktop source includes its headers, and no other Makefile
# builds or links its sources (the top-level lists that include the backend's own Makefiles aside) (WS131 B3; since ws131-p011 libkeiland reaches the system only through the compositor).
find userland/desktop -path 'userland/desktop/wayland' -prune \
    -o -path 'userland/desktop/libkeiland-backend*' -prune \
    -o -name '*.[ch]' -print |
while IFS= read -r file; do
    awk '/^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"].*keiland-backend[a-z-]*\.h[>"]/ {print FILENAME ":" FNR ": " $0}' "$file"
done > "$work/B3"
find userland/desktop -path 'userland/desktop/wayland' -prune \
    -o -path 'userland/desktop/libkeiland-backend*' -prune \
    -o \( -name 'Makefile*' -o -name '*.mk' \) -print |
while IFS= read -r file; do
    awk '/libkeiland-backend|userland\/base\/net\// && !/libkeiland-backend[a-z-]*\/Makefile\.(linux|freebsd)/ {print FILENAME ":" FNR ": " $0}' "$file"
done >> "$work/B3"

# The compositor takes from libkeiland only what D4 allows: the touch motion, the scroller, the gestures, the
# version and the translations (kl_tr*, translate.c: files and strings, no Wayland client part; q811) (WS131 B2, ws131-p011; `nm -u` of each compositor binary that is built: zedBSD's under $BUILD, default
# build/amd64, Linux's under $KEILAND_LINUX_BUILD, default build/keiland-linux, and any in $B2_BINARIES).
for binary in "${BUILD:-build/amd64}/bin/wayland" "${KEILAND_LINUX_BUILD:-build/keiland-linux}/bin/wayland" ${B2_BINARIES:-}; do
    if [ ! -f "$binary" ]; then
        echo "check: B2 note: $binary not built, not looked at" >&2
        continue
    fi
    nm -u "$binary" | awk -v binary="$binary" '{name = $NF; sub(/@.*/, "", name)}
        name ~ /^(keiland_|kl_)/ && name !~ /^(keiland_motion_|keiland_scroller_|keiland_gesture_|kl_motion_|kl_scroller_|kl_gesture_|keiland_version$|kl_version$|kl_tr$|kl_trc$|kl_tr_)/ {
            print binary ": uses " name " of libkeiland"
        }'
done > "$work/B2"

# libkeiland keeps no operating-system directory: its OS code is libkeiland-backend's (WS131 L6, ws131-p004).
for os_dir in zedbsd linux freebsd; do
    if [ -e "userland/desktop/libkeiland/$os_dir" ]; then
        echo "userland/desktop/libkeiland/$os_dir: an OS directory in libkeiland"
    fi
done > "$work/L6"

# The X server reads the Wayland keyboard's key codes from its own header, not an OS header (WS131 D14, ws131-p009).
find userland/desktop/xserver -name '*.[ch]' -print |
while IFS= read -r file; do
    awk '/^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](uapi\/|linux\/|dev\/)/ {print FILENAME ":" FNR ": " $0}' "$file"
done > "$work/X1"

# The compositor keeps no operating-system directory either: its OS code is libkeiland-backend's (WS131, ws131-p009).
for os_dir in zedbsd linux freebsd dmabuf evdev session drm wpa; do
    if [ -e "userland/desktop/wayland/$os_dir" ]; then
        echo "userland/desktop/wayland/$os_dir: an OS directory in the compositor"
    fi
done > "$work/L7"

# The compositor's three Makefiles build the same compositor sources (libkeiland-backend's are each OS's own; ws131-p009;
# the zedBSD test image's screen capture, shot.c in place of shot-none.c, is zedBSD's alone, ws173-p002).
for makefile in Makefile Makefile.linux Makefile.freebsd; do
    grep -oE 'userland/[A-Za-z0-9_/.-]*\.c\b' "userland/desktop/wayland/$makefile" | grep -v '^userland/desktop/libkeiland-backend' |
        grep -vx 'userland/desktop/wayland/shot\.c' |
        LC_ALL=C sort -u > "$work/sources-$makefile"
done
{
    diff "$work/sources-Makefile" "$work/sources-Makefile.linux" | sed -n 's/^[<>] /Makefile vs Makefile.linux: /p'
    diff "$work/sources-Makefile" "$work/sources-Makefile.freebsd" | sed -n 's/^[<>] /Makefile vs Makefile.freebsd: /p'
} > "$work/M1"

# desktop.conf is the compositor's own file (WS135): no other desktop source names it.
find userland/desktop -name '*.[ch]' -print |
while IFS= read -r file; do
    [ "$file" != userland/desktop/wayland/settings-store.c ] || continue
    awk '/"[^"]*desktop\.conf[^"]*"/ {print FILENAME ":" FNR ": " $0}' "$file"
done > "$work/S1"

# Inspect the actual target package membership and wildcard filename boundary.
# The database only (a target that does not exist, so no recipe of the package builds runs; 2026-10-04, a fresh build/ has no package trees).
make -pn -q zedbsd-make-database-only > "$work/make-database" 2>/dev/null || true
python3 - "$work/make-database" > "$work/L5" <<'PY'
from pathlib import Path
import fnmatch
import re
import sys
text = Path('Makefile').read_text()
patterns = re.findall(r'\$\(wildcard ([^)]+)\)', text)
for path in Path('userland').rglob('Makefile.linux'):
    for pattern in patterns:
        if fnmatch.fnmatchcase(str(path), pattern):
            print(f'{path}: matches top-level wildcard {pattern}')
for line in Path(sys.argv[1]).read_text().splitlines():
    if line.startswith('USERLAND_PACKAGE_MAKEFILES :=') and 'Makefile.linux' in line:
        print(line)
    if line.startswith('MAKEFILE_LIST :='):
        for name in line.split()[2:]:
            if name.endswith('Makefile.linux'):
                print(f'target includes Linux rules: {name}')
PY

# Report every violated condition before returning the aggregate outcome.
for check in C1 C2 C3 C4 C5 L1 L2 L3 L4 L5 L6 L7 M1 X1 B1 B2 B3 S1; do
    if [ -s "$work/$check" ]; then
        while IFS= read -r detail; do
            printf 'check: %s FAIL %s\n' "$check" "$detail"
        done < "$work/$check"
        status=1
    else
        printf 'check: %s PASS\n' "$check"
    fi
done
if [ "$status" -ne 0 ]; then
    echo 'keiland-os-boundary: FAIL'
    exit 1
fi
echo 'keiland-os-boundary: PASS'
