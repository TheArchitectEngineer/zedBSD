#!/bin/sh
# q824 (ws148-p002, the Privacy page taken out): Settings' Storage page keeps or stops the desktop's recent list, on
# the host through Settings' host renderer (plan/ws089/tests/host-build.sh), with a data folder of its own
# (XDG_DATA_HOME; the renderer sets HOME to its own).  Off empties the
# list and leaves recent.off beside it; on again takes recent.off away.  The pages Privacy, Security and Accessibility
# are gone: --page=privacy no longer names a page.
#   sh plan/ws148/tests/run-host-recent.sh [OUTPUT]   (default build/ws148-recent)
# Each run gets a new directory behind OUTPUT (plan/tools/fresh-out.sh); nothing is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws148-recent}
. plan/tools/fresh-out.sh
fresh_out "$out"
sh plan/ws089/tests/host-build.sh >/dev/null
render=$(pwd)/build/ws089-host/settings-render
home=$(cd "$out" && pwd)/home
list=$home/data/keiland/recent
mkdir -p "$home/data/keiland"
printf '1700000000\torg.zedbsd.textedit\t/home/kei/notes.txt\n' > "$list"
status=0
check() {
	if eval "$2"; then echo "ok $1"; else echo "FAIL $1"; status=1; fi
}
# Off: the list is emptied and stopped.
HOME=$home XDG_DATA_HOME=$home/data HOST_ACCOUNT_RESULT=1 timeout 60 "$render" --size=1280x2400 --page=storage draw="$out/on.ppm" control=405 draw="$out/off.ppm" > "$out/off.log" 2>&1 || status=1
check "the list is read as kept" "grep -q 'STORAGE recent keep=1 error=0' '$out/off.log'"
check "the switch stops it" "grep -q 'STORAGE recent set keep=0 error=0' '$out/off.log'"
check "recent.off is there" "test -f '$list.off'"
check "the list is empty" "test ! -s '$list'"
# On again: recent.off goes.
HOME=$home XDG_DATA_HOME=$home/data HOST_ACCOUNT_RESULT=1 timeout 60 "$render" --size=1280x2400 --page=storage draw="$out/off2.ppm" control=405 draw="$out/on2.ppm" > "$out/on.log" 2>&1 || status=1
check "the list is read as stopped" "grep -q 'STORAGE recent keep=0 error=0' '$out/on.log'"
check "the switch starts it again" "grep -q 'STORAGE recent set keep=1 error=0' '$out/on.log'"
check "recent.off is gone" "test ! -e '$list.off'"
# The pages taken out are not found by their words.
HOME=$home HOST_ACCOUNT_RESULT=1 timeout 60 "$render" --page=privacy draw="$out/privacy.ppm" > "$out/privacy.log" 2>&1 || status=1
check "Settings starts on Home for privacy" "grep -q 'page=home\|page=0' '$out/privacy.log' || ! grep -qi 'privacy' '$out/privacy.log'"
for picture in on off on2; do convert "$out/$picture.ppm" "$out/$picture.png"; done
[ $status -eq 0 ] && echo "run-host-recent: PASS" || echo "run-host-recent: FAIL"
exit $status
