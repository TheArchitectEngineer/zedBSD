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
printf 'archive' > Downloads/tools.tar.gz
printf '#!/bin/sh\necho hi\n' > Downloads/install.sh
chmod +x Downloads/install.sh
printf 'hidden' > .profile-sample
# Fixed times (2026-09-27 16:20 and earlier) so that the pictures do not change.
touch -t 202609271620 "Documents/Plan v3.key"
touch -t 202609271403 Pictures/Design.fig
touch -t 202609271124 "Documents/Meeting notes.txt"
touch -t 202609262018 Pictures/Tiny.ppm
echo "made $home"
