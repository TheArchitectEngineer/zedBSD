#!/bin/sh
# ws245: takes the regression evidence of one bare start of the Latitude 5330 running zedBSD (design.md §6.3, ws.md
# "5330 の素の起動での段ごとの回帰の手順" 3 and 6): the login mode, the session log's start and handoff, the kernel
# log's i915 lines (the survey of the first picture and the digest of the display writes among them), the dropped
# log bytes, and the backlight device read, set to 100 and read again.
#   sh plan/ws245/tests/5330-survey.sh <zedBSD address> <name> [OUT]
#     The summary goes to OUT/<name>.md (default OUT plan/ws245/tests/evidence, kept in git) and the whole kernel log
#     to LOGS/<name>-dmesg.txt (default LOGS plan/ws245/temp/5330, not in git).  plan/ws245/tests/5330-survey-diff.py
#     compares two whole kernel logs.
# The account is kei (password kei, sudo with the same password); sshpass must be installed.  The backlight probe
# is copied from BACKLIGHT_PROBE (default Q1's build/q1/blp/bin/backlight-probe of the main checkout).
# Nothing on the host or the machine is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname "$0")/../../.."
HOST=${1:?usage: 5330-survey.sh <zedBSD address> <name> [OUT]}
NAME=${2:?usage: 5330-survey.sh <zedBSD address> <name> [OUT]}
OUT=${3:-plan/ws245/tests/evidence}
LOGS=${LOGS:-plan/ws245/temp/5330}
PROBE=${BACKLIGHT_PROBE:-/home/awe/zedBSD-claude1/build/q1/blp/bin/backlight-probe}
PASS=kei
SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=10 -o LogLevel=ERROR"
mkdir -p "$OUT" "$LOGS"

remote() {
	sshpass -p "$PASS" ssh $SSH_OPTS "kei@$HOST" "$1"
}

# The kernel log, whole (sudo reads it; the password goes on stdin).
remote "echo $PASS | sudo -S -p '' dmesg" > "$LOGS/$NAME-dmesg.txt"

# The backlight probe, copied to the machine's /tmp.
sshpass -p "$PASS" scp $SSH_OPTS "$PROBE" "kei@$HOST:/tmp/backlight-probe" > /dev/null

{
	echo "# 5330 bare start: $NAME"
	echo
	echo "- taken: $(date '+%Y-%m-%d %H:%M:%S %Z') from $(hostname), machine $HOST"
	echo "- whole kernel log: $LOGS/$NAME-dmesg.txt (not in git)"
	echo
	echo "## login, session, dropped log bytes"
	echo
	echo '```'
	remote "sysctl kern.boot.login; sysctl kern.msgbuf_dropped 2>&1; uname -a"
	remote "grep -E 'SESSION start|HANDOFF' /var/log/sessiond.log | tail -4"
	echo '```'
	echo
	echo "## backlight (read, set 100, read)"
	echo
	echo '```'
	remote "chmod +x /tmp/backlight-probe; /tmp/backlight-probe -f /dev/backlight/backlight0; /tmp/backlight-probe -f /dev/backlight/backlight0 100; /tmp/backlight-probe -f /dev/backlight/backlight0" 2>&1
	echo '```'
	echo
	echo "## i915 lines"
	echo
	echo '```'
	printf 'i915 lines: %s\n' "$(grep -c 'i915:' "$LOGS/$NAME-dmesg.txt")"
	printf 'lease 1 claimed: %s\n' "$(grep -c 'lease 1 claimed' "$LOGS/$NAME-dmesg.txt")"
	printf 'picture up: %s\n' "$(grep -ci 'picture up' "$LOGS/$NAME-dmesg.txt")"
	printf 'device lost: %s\n' "$(grep -ci 'device lost' "$LOGS/$NAME-dmesg.txt")"
	printf 'error or warn lines: %s\n' "$(grep 'i915:' "$LOGS/$NAME-dmesg.txt" | grep -ciE 'error|warn')"
	grep -E 'perf: .* per submit' "$LOGS/$NAME-dmesg.txt" | tail -2 || true
	echo '```'
	echo
	echo "## survey and the digest of the display writes"
	echo
	echo '```'
	grep -E 'i915: survey|i915: w: ' "$LOGS/$NAME-dmesg.txt" || echo '(no survey lines)'
	echo '```'
} > "$OUT/$NAME.md"
echo "5330-survey: $OUT/$NAME.md"
