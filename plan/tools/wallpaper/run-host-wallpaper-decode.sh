#!/bin/sh
# ws138-p001/p002: the host test of the wallpaper decoding (userland/desktop/picture/wallpaper.c), under ASan/UBSan.
#
#   plan/tools/wallpaper/run-host-wallpaper-decode.sh
#
# It builds the decoder with libpng-compat, libz-compat and libjpeg-compat from their sources, makes the pictures
# (python3, and ImageMagick's convert for the JPEGs) and checks:
#   1. the tree's Lakeside.png and Birch-Lake.png decode to exactly the pixels Python's own PNG reading gives
#      (ppm-to-png.py's read_png; in ws138-p001 those were checked to be the PPMs' pixels);
#   2. a PNG with alpha: transparent is black, opaque keeps its colour, half transparent is half the colour (U8);
#   3. a JPEG of Lakeside decodes to its size within a small mean difference; a grey JPEG decodes grey; a CMYK JPEG
#      is refused (EINVAL 22);
#   4. a PNG wider than 8192 is refused (EFBIG 27); a text file and a cut PNG are refused (EINVAL 22);
#   5. a PPM is refused (the host's EINVAL, 22 on Linux) since ws138-p002 (U4: PNG and JPEG only).
# Exits 0 on success.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/wallpaper-host
mkdir -p "$out/include" "$out/obj" "$out/data"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
cc=${CC:-cc}
flags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -fsanitize=address,undefined -fno-sanitize-recover=all -I$out/include -Iuserland/desktop/picture -I."
objects=""
for file in userland/base/libz-compat/inflate.c userland/base/libz-compat/checksum.c userland/base/libpng-compat/read.c \
    userland/base/libjpeg-compat/decompress.c userland/base/libjpeg-compat/error.c userland/base/libjpeg-compat/huffman.c \
    userland/base/libjpeg-compat/idct.c userland/base/libjpeg-compat/marker.c userland/base/libjpeg-compat/memory.c \
    userland/base/libjpeg-compat/source.c userland/desktop/picture/wallpaper.c plan/tools/wallpaper/host-wallpaper-decode.c; do
	object="$out/obj/$(basename "$file" .c).o"
	"$cc" $flags -c "$file" -o "$object"
	objects="$objects $object"
done
"$cc" $flags -o "$out/decode" $objects
run="$out/decode"
data="$out/data"
tree=userland/desktop/keiland/wallpapers
failed=0
check() {
	if [ "$2" = "$3" ]; then
		echo "$1: ok"
	else
		echo "$1: FAILED (got '$2', expected '$3')"
		failed=1
	fi
}

# The references: the tree's PNGs as Python reads them, and Lakeside as a PPM (for step 5).
python3 - "$tree" "$data" <<'EOF'
import importlib.util, sys
spec = importlib.util.spec_from_file_location('p2p', 'userland/desktop/wallpapers/ppm-to-png.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
tree, data = sys.argv[1], sys.argv[2]
for name in ('Lakeside', 'Birch-Lake'):
    width, height, rgb = m.read_png('%s/%s.png' % (tree, name))
    with open('%s/%s.rgb' % (data, name), 'wb') as out:
        out.write(b'%d %d\n' % (width, height) + rgb)
    if name == 'Lakeside':
        with open('%s/lakeside.ppm' % data, 'wb') as out:
            out.write(b'P6\n%d %d\n255\n' % (width, height) + rgb)
EOF

# 1. The tree's PNGs hold the pixels Python reads from them.
check lakeside "$($run same $tree/Lakeside.png $data/Lakeside.rgb)" same
check birch-lake "$($run same $tree/Birch-Lake.png $data/Birch-Lake.rgb)" same

# 2. Alpha over black: a 3x1 PNG of (255,0,0,0) (0,255,0,255) (100,200,50,128).
python3 - "$data/alpha.png" <<'PY'
import struct, sys, zlib
def chunk(kind, body):
    return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)
raw = b'\x00' + bytes([255, 0, 0, 0, 0, 255, 0, 255, 100, 200, 50, 128])
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 3, 1, 8, 6, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
open(sys.argv[1], 'wb').write(png)
PY
check alpha-black "$($run $data/alpha.png)" "ok 3x1 0,0,0 0,255,0"

# 3. JPEGs: Lakeside at quality 95, grey, CMYK.
convert $tree/Lakeside.png -quality 95 "$data/lakeside.jpg"
check jpeg-near "$($run near $data/lakeside.jpg $data/Lakeside.rgb 3)" near
convert $tree/Lakeside.png -colorspace Gray -quality 90 "$data/grey.jpg"
check jpeg-grey "$($run grey $data/grey.jpg)" grey
convert $tree/Lakeside.png -colorspace CMYK -quality 90 "$data/cmyk.jpg"
check jpeg-cmyk "$($run $data/cmyk.jpg)" "error 22"

# 4. Sizes and damage.
python3 -c "
import sys; sys.path.insert(0, 'userland/desktop/wallpapers')
import importlib.util
spec = importlib.util.spec_from_file_location('p2p', 'userland/desktop/wallpapers/ppm-to-png.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
m.write_png('$data/wide.png', 8193, 1, bytes(8193 * 3), 'none')
"
check too-wide "$($run $data/wide.png)" "error 27"
printf 'not a picture\n' > "$data/text.png"
check not-picture "$($run $data/text.png)" "error 22"
head -c 2000 $tree/Lakeside.png > "$data/cut.png"
check cut-png "$($run $data/cut.png)" "error 22"

# 5. A PPM is refused.
check ppm-refused "$($run $data/lakeside.ppm)" "error 22"

exit $failed
