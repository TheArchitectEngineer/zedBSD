#!/bin/sh
# ws138-p002 (U2): the edits outside WS138 that Q1 makes on main when merging the switch to PNG wallpapers.
#
#   sh plan/ws138/phase002/apply-q1.sh          (from the top of the tree, on the commit that holds the ws138-p002 switch)
#
# 1. Five sed rules over every executed test or tool file (sh, py, mk, c, h, in, desktop, Makefile) outside
#    userland/, tools/, plan/history, evidence and plan/ws138 that names the wallpaper as a .ppm: the default
#    /usr/share/keiland/wallpaper.ppm, the tree's Birch-Lake.ppm and Lakeside.ppm, and the generated
#    share/keiland/wallpapers/NAME.ppm.  Other .ppm files (screenshots, a user's pictures, settings key checks) do not
#    match.
# 2. The places the rules do not reach (phase.md step 5): ws075's demo image loop, ws099's c7 contrast listing, ws089's
#    host wallpaper test, Settings' p003 picture, the showcase's pictures, a p004 comment, the launcher check's expected
#    default, Files' host render's comment.
# 3. ws089's host build compiles the shared decoder (userland/desktop/picture/wallpaper.c, since ws138-p001) and
#    libz-, libpng- and libjpeg-compat, which Settings' look.c now needs.
# Prints the files it changed and the lines still naming a wallpaper .ppm (phase.md step 6 lists the expected ones).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
pattern='wallpaper.*\.ppm|wallpapers/.*\.ppm|Birch-Lake\.ppm|Lakeside\.ppm|Aurora\.ppm'
list=$(mktemp)
trap 'rm -f "$list"' EXIT INT TERM

# 1. The rules.
git grep -lE "$pattern" -- '*.sh' '*.py' '*.mk' '*.c' '*.h' '*.in' '*.desktop' '*Makefile*' \
	':!plan/history' ':!*/evidence/*' ':!plan/ws138/*' ':!.internal' ':!userland/*' ':!tools/*' > "$list" || true
echo "files matched: $(wc -l < "$list")"
xargs -r sed -i \
	-e 's#/usr/share/keiland/wallpaper\.ppm#/usr/share/keiland/wallpaper.png#g' \
	-e 's#share/keiland/wallpaper\.ppm#share/keiland/wallpaper.png#g' \
	-e 's#keiland/wallpapers/Birch-Lake\.ppm#keiland/wallpapers/Birch-Lake.png#g' \
	-e 's#keiland/wallpapers/Lakeside\.ppm#keiland/wallpapers/Lakeside.png#g' \
	-e 's#\(share/keiland/wallpapers/[A-Za-z$][A-Za-z0-9_${}-]*\)\.ppm#\1.png#g' \
	< "$list"

# 2. By hand.
sed -i 's#userland/desktop/keiland/wallpapers/\*\.ppm#userland/desktop/keiland/wallpapers/*.png#' \
	plan/ws075/demo/build-demo-image.sh
python3 - plan/ws099/tests/c7-contrast.sh <<'EOF'
import sys
path = sys.argv[1]
text = open(path).read()
for old, new in (("/usr/share/keiland/wallpapers/*.ppm 2>/dev/null' | grep '\\.ppm$'",
                  "/usr/share/keiland/wallpapers/*.png 2>/dev/null' | grep '\\.png$'"),
                 ('name=$(basename "$picture" .ppm)', 'name=$(basename "$picture" .png)')):
    assert text.count(old) == 1 or text.count(new) == 1, old
    text = text.replace(old, new)
open(path, 'w').write(text)
EOF
sed -i \
	-e 's#"$data/keiland/wallpaper\.ppm"#"$data/keiland/wallpaper.png"#' \
	-e 's#"$data/keiland/wallpapers/Broken\.ppm"#"$data/keiland/wallpapers/Broken.png"#' \
	-e 's#path=\.\*/Broken\.ppm error=22#path=.*/Broken.png error=22#' \
	plan/ws089/tests/host-wallpaper.sh
sed -i "s#ls /usr/share/keiland/wallpapers/\*\.ppm | head -1#ls /usr/share/keiland/wallpapers/*.png | head -1#" \
	plan/tools/settings/settings-p003.sh
sed -i 's#for f in userland/desktop/keiland/wallpapers/\*\.ppm; do#for f in userland/desktop/keiland/wallpapers/*.png; do#' \
	plan/tools/showcase/showcase.sh
sed -i 's#value=\.\.\./Aurora\.ppm)#value=.../Aurora.png)#' \
	plan/ws089/tests/settings-p004.sh
sed -i "s#'--wallpaper='+str(prefix/'share/keiland/wallpaper\.ppm')#'--wallpaper='+str(prefix/'share/keiland/wallpaper.png')#" \
	plan/tools/keiland-launcher/check.py
sed -i 's#(default /usr/share/keiland/wallpaper\.ppm)#(default /usr/share/keiland/wallpaper.png)#' \
	plan/tools/files/host-render.c

# 3. ws089's host build: the compat headers, and the decoder with its libraries before Settings' own objects.
python3 - plan/ws089/tests/host-build.sh <<'EOF'
import sys
path = sys.argv[1]
text = open(path).read()
link = 'ln -sf "$(pwd)/userland/desktop/keiland/keiland-ui.h" "$out/include/keiland-ui.h"\n'
compat = 'ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"\n'
if compat not in text:
    assert text.count(link) == 1
    text = text.replace(link, link + compat)
anchor = 'for file in $src/*.c; do\n'
block = ('# The wallpaper decoding look.c uses (ws138-p001): the shared decoder and libz-, libpng- and libjpeg-compat.\n'
         'for file in userland/base/libz-compat/inflate.c userland/base/libz-compat/checksum.c userland/base/libpng-compat/read.c \\\n'
         '    userland/base/libjpeg-compat/decompress.c userland/base/libjpeg-compat/error.c userland/base/libjpeg-compat/huffman.c \\\n'
         '    userland/base/libjpeg-compat/idct.c userland/base/libjpeg-compat/marker.c userland/base/libjpeg-compat/memory.c \\\n'
         '    userland/base/libjpeg-compat/source.c userland/desktop/picture/wallpaper.c; do\n'
         '\tobject="$out/obj/shared-$(basename "$file" .c).o"\n'
         '\t"$cc" -O2 -g -Wall -Werror -D_GNU_SOURCE -I$out/include -I. -c "$file" -o "$object"\n'
         '\tobjects="$objects $object"\n'
         'done\n')
if 'userland/desktop/picture/wallpaper.c' not in text:
    assert text.count(anchor) == 1
    text = text.replace(anchor, block + anchor)
open(path, 'w').write(text)
EOF

# What changed, and what still names a wallpaper .ppm.
git diff --stat | tail -1
echo "lines still naming a .ppm wallpaper (expected: phase.md step 6 table):"
git grep -nE "$pattern" -- '*.sh' '*.py' '*.mk' '*.c' '*.h' '*.in' '*.desktop' '*Makefile*' \
	':!plan/history' ':!*/evidence/*' ':!plan/ws138/*' ':!.internal' || true
