#!/bin/sh
# ws071: makes a sample home folder for zdesktop-files' tests (the host's and the guest's).
#
#   sh plan/ws071/tests/make-home.sh DIR
#
# The folders of the sidebar (Desktop, Documents, Downloads, Pictures, Music, Movies), a
# Projects tree, and files of several kinds with fixed times, so that pictures of the
# window are the same from run to run.  Runs on the host and in the guest (plain POSIX sh).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
home=${1:?usage: make-home.sh DIR}
mkdir -p "$home"
cd "$home"
mkdir -p Desktop Documents Downloads Pictures Music Movies Projects/zedBSD/src Projects/zedBSD/docs Projects/website
printf 'Agenda\n- Release plan\n- File manager review\n- Budget\n' > "Documents/Meeting notes.txt"
printf 'Quarterly plan, third draft.\n' > "Documents/Plan v3.key"
printf '%%PDF-1.4\n%% sample\n' > Documents/Report.pdf
printf 'name,amount\nrent,1200\nfood,300\n' > Documents/Budget.csv
printf '\xe4\xbc\x9a\xe8\xad\xb0\xe3\x81\xae\xe3\x83\xa1\xe3\x83\xa2\n' > "Documents/$(printf '\344\274\232\350\255\260\343\203\241\343\203\242').txt"
printf 'Long name test\n' > "Documents/A file with a rather long name for wrapping.txt"
printf '#include <stdio.h>\nint main(void) { puts("hello"); return 0; }\n' > Projects/zedBSD/src/main.c
printf '# zedBSD\n\nA small operating system.\n' > Projects/zedBSD/README.md
printf 'all:\n\tcc -o hello src/main.c\n' > Projects/zedBSD/Makefile
printf '<html><body>Hello</body></html>\n' > Projects/website/index.html
printf 'ID3' > Music/Song.mp3
printf 'RIFF' > Music/Voice.wav
printf 'ftyp' > Movies/Trip.mp4
printf 'P6\n2 2\n255\n\377\000\000\000\377\000\000\000\377\377\377\377' > Pictures/Tiny.ppm
printf 'placeholder' > Pictures/Design.fig
# A 256x160 sunset gradient (PPM) and a 64x48 grey ramp (PGM) for the thumbnails and Quick Look (p007).
# Each row is one colour, built by doubling one pixel's octal escapes up to the width.
rows() {
	magic=$1 width=$2 height=$3 doublings=$4
	printf '%s\n%s %s\n255\n' "$magic" "$width" "$height"
	y=0
	while [ "$y" -lt "$height" ]; do
		if [ "$magic" = P6 ]; then
			pixel=$(printf '\\%03o\\%03o\\%03o' $((250 - y * 110 / height)) $((150 + y * 50 / height)) $((80 + y * 160 / height)))
		else
			pixel=$(printf '\\%03o' $((40 + y * 200 / height)))
		fi
		i=0
		while [ "$i" -lt "$doublings" ]; do pixel=$pixel$pixel; i=$((i + 1)); done
		printf "$pixel"
		y=$((y + 1))
	done
}
rows P6 256 160 8 > Pictures/Sunset.ppm
rows P5 64 48 6 > Pictures/Ramp.pgm
# Two PNGs (p010): a 120x80 RGB picture and a 64x64 palette one with a transparent corner.
printf '\211PNG\015\012\032\012\000\000\000\015IHDR\000\000\000x\000\000\000P\010\002\000\000\000\135\371\046\336\000\000\000\331IDATx\332\355\332\261\015\3020\024EQ\033\221\236\011\030\001e\005\244L\301\014\054\220\056\343\260\005\035Ji1\011\175\212P\261\000\222cG\076w\003\037\075\375\312\361\174\233\203\362w\214\021\002h\320\002\015\032\264\100\203\026h\320\240\005\032\264\100\203\006\055\320\240C\010\353\351\322\232W\374\274K\100\2677\314\277\271\234\016\320\240A\203\006\015\0324h\320\277\136\343\2415\350a\262h\247\0034h\320\240A\203n\017\272\173\366\271\037\274\134\023h\213\006\015\032\264\100\203\006\015\0324h\201\006\015\0324h\320\002\015\0324h\320\240\005\0324h\320\240A\253R\350R\277\133\054\0324h\320\240A\203\006\015\032\364\036\240\357\217\004\332\242\353\0124h\320\002\015\0324h\320\240\005\0324h\320\233\364\005\176\213\020pc\222N\033\000\000\000\000IEND\256B\140\202' > Desktop/Screenshot.png
printf '\211PNG\015\012\032\012\000\000\000\015IHDR\000\000\000\100\000\000\000\100\002\003\000\000\000\327\007\231M\000\000\000\011PLTE\377\377\377\334\050\074\000\000\000P\341V\325\000\000\000\003tRNS\377\377\000\327\312\015A\000\000\000\277IDATx\332\265\323\261\021\303\040\014\005\320O\341\021\262\017\043\270\210\0502\202\367a\204\024\316\2246B\140\045\237\304w\311\205\316\357d\004B\302\343e\341kXE\026\017\373w\223\012\271\100\072\100\003\054\004\075\300B\024\304V\203\173\203\233An\220\014D\334\077\3509\054\017\334\026\272\311\010\362\001IA\304\357\072\200\325\303\322\041\206\016\232d\006\246\232\306\040\002\241\201f\305\276j\336\012\327\002\027\007s\201\311A\054\020\014\304\203\274\003\240\245\371\047\174\076\007\035\175x9\272\376S\201\250\204Tdz\006z\250\363\267\345v8\357\040j\072jKn\134jmj\176\036\017\032\040\0321\036B\032\323\337f\177\003\336\303\270\272\023\057\023N\000\000\000\000IEND\256B\140\202' > Desktop/Logo.png
printf 'archive' > Downloads/tools.tar.gz
printf '#!/bin/sh\necho hi\n' > Downloads/install.sh
chmod +x Downloads/install.sh
printf 'hidden' > .profile-sample
# The tests open PDFs and plain text by writing their paths to ~/.opened, so no window appears (p012);
# other kinds keep the built-in ways (a terminal, Quick Look).
mkdir -p .config/zdesktop
printf '# ws071 tests\napplication/pdf,text/plain\tRecord\techo %%f >> "$HOME/.opened"\n' > .config/zdesktop/open-with
# Fixed times (2026-09-27 16:20 and earlier) so that the pictures do not change.
touch -t 202609271620 "Documents/Plan v3.key"
touch -t 202609271403 Pictures/Design.fig
touch -t 202609271124 "Documents/Meeting notes.txt"
touch -t 202609262018 Pictures/Tiny.ppm
touch -t 202609251930 Pictures/Sunset.ppm Pictures/Ramp.pgm
touch -t 202609251200 Desktop/Screenshot.png Desktop/Logo.png
echo "made $home"
