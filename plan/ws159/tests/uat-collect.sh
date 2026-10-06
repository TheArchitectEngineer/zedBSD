#!/bin/sh
# ws159-p005: collects the evidence of the 5330 UAT from the 5330 booted from the UAT image, over SSH as kei (the
# password "kei", or the one the UAT changed it to) with sudo for the root-only logs.  Run on centris while the user
# keeps the session open:
#   plan/ws159/tests/uat-collect.sh HOST [OUTDIR] [PASSWORD]      (HOST the 5330's address; default PASSWORD kei)
# With sshpass on centris the password is given by it; otherwise ssh asks for it (several times).
# What is collected (OUTDIR, default build/ws159-uat): the kernel log, sessiond's and the session's logs (the
# compositor's KWL lines), the system log, systemevents -p, ifconfig -a, uname -a, /etc/os-release, and a summary of
# the lines the procedure checks (summary.txt).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
host=${1:?usage: uat-collect.sh HOST [OUTDIR] [PASSWORD]}
out=${2:-build/ws159-uat}
password=${3:-kei}
mkdir -p "$out"
opts="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=10"
remote() {
	if command -v sshpass >/dev/null 2>&1; then
		timeout 60 sshpass -p "$password" ssh $opts "kei@$host" "$1"
	else
		timeout 120 ssh $opts "kei@$host" "$1"
	fi
}
as_root() { remote "printf '%s\n' '$password' | /bin/sudo -S $1 2>/dev/null"; }

as_root 'cat /var/log/kernel.log' > "$out/kernel.log"
as_root 'cat /var/log/sessiond.log' > "$out/sessiond.log"
as_root 'cat /var/log/messages' > "$out/messages"
remote 'cat /run/user/$(id -u)/session.log' > "$out/session.log"
remote '/bin/systemevents -p; uname -a; cat /etc/os-release; ifconfig -a' > "$out/state.txt"

# The lines the procedure looks for.
{
	echo "== touch pad (kernel)"
	grep -E 'lpss-i2c|i2c-hid|intel-gpio' "$out/kernel.log"
	echo "== touch pad (compositor)"
	grep -E 'KWL INPUT device=.* kind=touchpad' "$out/session.log" | tail -3
	echo "== power (kernel)"
	grep -E 'acpi: power device|acpi: power button|acpi: sleep button' "$out/kernel.log" | tail -10
	echo "== power (compositor)"
	grep -E 'KL EVENTS|KWL EVENT|KWL POWER' "$out/session.log" | tail -20
	echo "== Windows key, network details, Users"
	grep -E 'KWL SUPER|KWL HOME open via=super|KWL NETWORK info (open|rows)|KWL SYSTEM account' "$out/session.log" | tail -20
	echo "== sudo, su, passwd (system log)"
	grep -E 'sudo|su\[|passwd' "$out/messages" | tail -20
	echo "== state"
	cat "$out/state.txt"
} > "$out/summary.txt"
cat "$out/summary.txt"
