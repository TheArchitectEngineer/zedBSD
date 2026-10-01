#!/bin/sh
# Checks Keiland's common-source OS boundary and installation paths.
# Usage: sh plan/tools/keiland-os-boundary/check.sh (from any directory).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
status=0

# Collect common compositor and system-library sources, excluding OS modules.
find userland/desktop/libkeiland userland/desktop/wayland \
    \( -path '*/zedbsd' -o -path '*/linux' -o -path '*/wpa' \) -prune \
    -o -name '*.[ch]' -print | LC_ALL=C sort > "$work/common"

# Keep exactly the agreed evdev header exception, and reject other OS includes.
while IFS= read -r file; do
    awk '
    /^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](uapi\/|userland\/base\/)/ {
        if (FILENAME == "userland/desktop/wayland/zwl-evdev.h" &&
            $0 ~ /^[[:space:]]*#[[:space:]]*include[[:space:]]*<uapi\/input\.h>[[:space:]]*$/) {
            exceptions++
        } else {
            print FILENAME ":" FNR ": " $0
        }
    }
    END {
        if (FILENAME == "userland/desktop/wayland/zwl-evdev.h" && exceptions != 1)
            print FILENAME ": expected exactly one input UAPI include, got " exceptions + 0
    }' "$file"
done < "$work/common" > "$work/C1"

# Device ioctls belong to an OS module rather than the common code.
while IFS= read -r file; do
    awk '/ioctl[[:space:]]*\(/ {print FILENAME ":" FNR ": " $0}' "$file"
done < "$work/common" > "$work/C2"

# A zedBSD wire layout must stay inside the zedBSD GPU backend.
find userland/desktop/wayland -path '*/zedbsd' -prune -o -name '*.[ch]' -print |
while IFS= read -r file; do
    awk '/zwl_buffer_layout/ {print FILENAME ":" FNR ": " $0}' "$file"
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

# Report every violated condition before returning the aggregate outcome.
for check in C1 C2 C3 C4 C5; do
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
