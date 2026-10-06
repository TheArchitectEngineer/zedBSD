#!/bin/sh
# q824: Files' Recents has Clear Recents at the right of its title, which empties the desktop's recent list
# (kl_recent_clear); on the host through Files' host renderer (plan/tools/files/host-build.sh), with a data folder of
# its own (XDG_DATA_HOME).  Action 31 is FM_ACTION_GO_RECENTS; the button is at the title's right end.
#   sh plan/ws148/tests/run-host-files-recents.sh [OUTPUT]   (default build/ws148-files-recents)
# Each run gets a new directory behind OUTPUT (plan/tools/fresh-out.sh); nothing is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws148-files-recents}
. plan/tools/fresh-out.sh
fresh_out "$out"
sh plan/tools/files/host-build.sh >/dev/null
work=$(cd "$out" && pwd)
mkdir -p "$work/data/keiland" "$work/docs"
echo one > "$work/docs/a.txt"
echo two > "$work/docs/b.txt"
printf '1700000000\tx\t%s\n1700000001\tx\t%s\n' "$work/docs/a.txt" "$work/docs/b.txt" > "$work/data/keiland/recent"
status=0
XDG_DATA_HOME=$work/data timeout 60 build/ws071-host/files-render action=31 draw="$out/before.ppm" click=1037,32 draw="$out/after.ppm" > "$out/log" 2>&1 || status=1
grep -q "ZFILES LOCATION kind=recents path= items=2 error=0" "$out/log" && echo "ok Recents shows two items" || { echo "FAIL Recents"; status=1; }
grep -q "ZFILES RECENTS clear error=0" "$out/log" && echo "ok Clear Recents empties the list" || { echo "FAIL no clear"; status=1; }
grep -q "ZFILES LOCATION kind=recents path= items=0 error=0" "$out/log" && echo "ok Recents is shown again, empty" || { echo "FAIL not empty"; status=1; }
test ! -s "$work/data/keiland/recent" && echo "ok the list file is empty" || { echo "FAIL the list file"; status=1; }
test -f "$work/docs/a.txt" && test -f "$work/docs/b.txt" && echo "ok the files stay" || { echo "FAIL a file went"; status=1; }
for picture in before after; do convert "$out/$picture.ppm" "$out/$picture.png"; done
[ $status -eq 0 ] && echo "run-host-files-recents: PASS" || echo "run-host-files-recents: FAIL"
exit $status
