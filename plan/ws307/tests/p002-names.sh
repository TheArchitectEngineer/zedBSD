#!/bin/sh
# WS307 p002: the OS name and the default user on two images, with the shared tools.
#   NEW: an image of this tree (plan/ws307/tests/config-amd64-p002.mk: user/user, hostname zedbsd, the zedBSD splash).
#   OLD: an image made before the change (kei/kei), so the tools are seen to work on both.
# For each image:
#   1. plan/tools/boot-test.sh: the console's login prompt (OUTDIR/LABEL/boot-test/login.png).
#   2. The Venus guest (plan/ws035/tests/zdesktop-guest.sh, its own GUEST_RUNTIME): the first screens, one a second,
#      as OUTDIR/LABEL/early-NN.png (the boot splash), then SSH as root (guest.py wait).
#   3. Over SSH: the name of uid 1000, the hostname and /etc/os-release (OUTDIR/LABEL/names.txt).
#   4. plan/tools/aat/run-aat.sh qemu: the session as uid 1000's user (os.boot.session-up), the lock screen and the
#      password (desktop.lock.lock-unlock), sudo with the user's password (os.accounts.sudo), and last Log Out
#      (desktop.session.logout-login).  The helpers take the user's name from the target (aatlib.py).
#   5. The login screen: when logout-login stopped at the greeter (its capture, keiland-shot, does not reach the
#      greeter on QEMU), the greeter is photographed from the Venus head's VNC (OUTDIR/LABEL/greeter.png) and the
#      script logs in itself: the user's password (its name) and Enter, then greeter.log's "KWL GREETER auth
#      user=NAME" and a new session's "KWL READY ... role=normal".
# The last lines: "p002-names: LABEL CHECK pass|fail ..." and "p002-names: PASS" or "p002-names: FAIL".
#
#   plan/ws307/tests/p002-names.sh NEW_IMAGE OLD_IMAGE [OUTDIR]     (default OUTDIR build/ws307-p002-run)
# VENUS_RENDERER names the renderer's install (plan/ws035/tests/zdesktop-guest.sh).  Each guest is stopped at the end.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
[ $# -ge 2 ] || { echo "usage: p002-names.sh NEW_IMAGE OLD_IMAGE [OUTDIR]" >&2; exit 2; }
new=$(realpath -- "$1")
old=$(realpath -- "$2")
out=${3:-build/ws307-p002-run}
mkdir -p "$out"
out=$(realpath -- "$out")
summary=$out/summary.txt
: > "$summary"
failed=0

note() {
	echo "p002-names: $*" | tee -a "$summary"
}

check() {
	# check LABEL NAME CONDITION-STATUS DETAIL
	if [ "$3" -eq 0 ]; then
		note "$1 $2 pass $4"
	else
		note "$1 $2 fail $4"
		failed=1
	fi
}

one() {
	label=$1
	image=$2
	user=$3
	dir=$out/$label
	mkdir -p "$dir"

	# 1. The console's login prompt.
	OUTPUT=$dir/boot-test BOOT_TIMEOUT=240 timeout 400 plan/tools/boot-test.sh "$image" > "$dir/boot-test.txt" 2>&1
	check "$label" boot-test $? "$dir/boot-test"

	# 2. The Venus guest, its first screens, SSH.
	GUEST_RUNTIME=$dir/run
	export GUEST_RUNTIME
	timeout 120 plan/ws035/tests/zdesktop-guest.sh start "$image" > "$dir/guest-start.txt" 2>&1
	check "$label" guest-start $? "$dir/guest-start.txt"
	i=0
	while [ $i -lt 30 ]; do
		timeout 20 python3 plan/tools/guest/guest.py screenshot "$dir/early-$(printf %02d $i).png" > /dev/null 2>&1
		sleep 1
		i=$((i + 1))
	done
	timeout 300 python3 plan/tools/guest/guest.py wait > "$dir/guest-wait.txt" 2>&1
	check "$label" ssh $? "$dir/guest-wait.txt"

	# 3. The names.
	timeout 60 python3 plan/tools/guest/guest.py run \
	    "awk -F: '\$3 == 1000 { print \"user=\" \$1 }' /etc/passwd; echo hostname=\$(hostname); cat /etc/os-release" \
	    > "$dir/names.txt" 2>&1
	grep -qx "user=$user" "$dir/names.txt"
	check "$label" user $? "$(grep '^user=' "$dir/names.txt" | head -1)"
	if [ "$label" = new ]; then
		grep -qx 'hostname=zedbsd' "$dir/names.txt"
		check "$label" hostname $? "$(grep '^hostname=' "$dir/names.txt" | head -1)"
		grep -qx 'NAME="zedBSD"' "$dir/names.txt" && ! grep -qi 'kei' "$dir/names.txt"
		check "$label" os-release $? "$(grep '^PRETTY_NAME=' "$dir/names.txt" | head -1)"
	fi

	# 4. The AAT: the session, the login screen and the password, sudo.
	ids="os.boot.session-up desktop.lock.lock-unlock os.accounts.sudo desktop.session.logout-login"
	# shellcheck disable=SC2086
	timeout 1500 plan/tools/aat/run-aat.sh qemu "$dir/aat" $ids --no-samples > "$dir/aat.txt" 2>&1
	status=$?
	logout=
	for id in $ids; do
		verdict=$(awk -F '\t' -v id="$id" '$1 == id { print $2 }' "$dir/aat/verdicts.tsv" 2>/dev/null | tail -1)
		[ "$id" = desktop.session.logout-login ] && logout=$verdict
		[ "$verdict" = pass ]
		check "$label" "aat:$id" $? "${verdict:-none} (run-aat status $status)"
	done

	# 5. The login screen, when logout-login left the greeter up.
	if [ "$logout" != pass ]; then
		aat="python3 plan/tools/aat/aat --qemu"
		sleep 3
		timeout 60 python3 plan/ws035/tests/zdesktop-shot.py "$dir/greeter.png" > "$dir/greeter-shot.txt" 2>&1
		check "$label" greeter-photo $? "$dir/greeter.png"
		$aat mark greeter-login > /dev/null 2>&1
		$aat type "$user" > /dev/null 2>&1
		$aat key enter > /dev/null 2>&1
		timeout 60 $aat wait-log "KWL GREETER auth user=$user" --log /var/log/greeter.log --timeout 30 \
		    > "$dir/greeter-auth.txt" 2>&1
		check "$label" greeter-auth $? "$(head -c 200 "$dir/greeter-auth.txt")"
		timeout 120 $aat wait-log 'KWL READY socket=.* role=normal' --since greeter-login --timeout 90 \
		    > "$dir/greeter-ready.txt" 2>&1
		check "$label" greeter-session $? "$(head -c 200 "$dir/greeter-ready.txt")"
		sleep 3
		timeout 60 python3 plan/ws035/tests/zdesktop-shot.py "$dir/after-login.png" > /dev/null 2>&1
	fi

	timeout 60 plan/ws035/tests/zdesktop-guest.sh stop > /dev/null 2>&1
}

one new "$new" user
one old "$old" kei

if [ $failed -eq 0 ]; then
	note PASS
else
	note FAIL
fi
exit $failed
