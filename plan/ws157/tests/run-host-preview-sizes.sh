#!/bin/sh
# ws157-p015: keiland-preview (the Linux build's, seccomp) makes Photos' and Phone's pictures at their sizes: a
# 12-megapixel photo's thumbnail (cover 160), its view picture (contain 2048), Phone's 480 and the drag's, also when
# its EXIF turns it, a small one never made larger; a 54-megapixel photo (under the limit of 64 M pixels, the user's
# decision L1) is made.  Since ws157-p017 a JPEG is decoded scaled down (1/2, 1/4, 1/8) as far as the size asked allows,
# and the limits apply to the scaled size: a 72-megapixel photo's thumbnail is made, and a 200-megapixel photo's
# thumbnail, view picture and largest preview are made within the sandbox's 1 GiB and 10 s of processor time (prlimit,
# as zedBSD's jail); a PNG over the limit (72 M pixels, decoded whole) is still refused (status 3), which Photos shows
# as a picture that could not be made.  The last line is "preview sizes: all PASS".
# usage: sh plan/ws157/tests/run-host-preview-sizes.sh LINUX_BUILD   (the keiland-linux.mk build with keiland-preview)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
linux=$(realpath "$1")
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws157-sizes.XXXXXX")
python3 - "$work" <<'PY'
import sys
from PIL import Image
w = sys.argv[1]
photo = Image.new("RGB", (4000, 3000), (200, 40, 40))
photo.save(w + "/photo.jpg", quality=50)
exif = Image.Exif(); exif[0x0112] = 6
photo.save(w + "/turned.jpg", quality=50, exif=exif.tobytes())
Image.new("RGB", (9000, 6000), (40, 40, 200)).save(w + "/big.jpg", quality=50)
Image.new("RGB", (9000, 8000), (40, 200, 40)).save(w + "/huge.jpg", quality=50)
Image.new("RGB", (9000, 8000), (40, 200, 40)).save(w + "/huge.png")
Image.new("RGB", (120, 90), (10, 200, 10)).save(w + "/small.jpg")
import os
texture = Image.frombytes("RGB", (4080, 3060), os.urandom(4080 * 3060 * 3))
texture.resize((16320, 12240), Image.BILINEAR).save(w + "/p200.jpg", quality=85)
PY
size() {
	prlimit --as=1073741824 --cpu=10 env LD_LIBRARY_PATH="$linux/lib" "$linux/libexec/keiland-preview" "$@" <"$input" >"$work/out.ppm"
	head -c 32 "$work/out.ppm" | tr '\n' ' ' | awk '{print $2 "x" $3}'
}
expect() {
	if [ "$2" != "$3" ]; then echo "FAIL $1: wanted $3, got $2"; exit 1; fi
	echo "ok $1 $2"
}
input="$work/photo.jpg"
expect cover "$(size --width=160 --height=160 --fit=cover)" 160x160
expect contain "$(size --width=2048 --height=2048)" 2048x1536
expect phone "$(size --width=480 --height=480)" 480x360
input="$work/turned.jpg"
expect turned "$(size --width=2048 --height=2048)" 1536x2048
input="$work/small.jpg"
expect small "$(size --width=2048 --height=2048)" 120x90
expect small-cover "$(size --width=160 --height=160 --fit=cover)" 160x160
input="$work/big.jpg"
expect big "$(size --width=160 --height=160 --fit=cover)" 160x160
input="$work/huge.jpg"
expect huge-reduced "$(size --width=160 --height=160 --fit=cover)" 160x160
expect huge-largest "$(size --width=4096 --height=4096)" 4096x3641
input="$work/p200.jpg"
expect p200-cover "$(size --width=160 --height=160 --fit=cover)" 160x160
expect p200-contain "$(size --width=2048 --height=2048)" 2048x1536
expect p200-largest "$(size --width=4096 --height=4096)" 4096x3072
status=0
LD_LIBRARY_PATH="$linux/lib" "$linux/libexec/keiland-preview" --width=160 --height=160 --fit=cover <"$work/huge.png" >"$work/out.ppm" || status=$?
expect huge-png-refused "$status" 3
echo "preview sizes: all PASS"
