#!/bin/sh
# ws160-p001: passwd, su and sudo on the accounts guest (plan/ws160/tests/config-amd64-accounts.mk, started with
# plan/tools/guest/guest.py start IMAGE; the harness logs in as root over SSH with its key).  kei's password is "kei"
# (userland/base/etc/shadow), kei is in wheel (userland/base/etc/group).  The passwords come on standard input
# (passwd -s, sudo -S, su without a terminal).
#  1. The three are set-user-ID root (-rwsr-xr-x root).
#  2. sudo: kei runs "id -u" as root with kei's password (0); a wrong password is refused (status 1); the environment
#     is made anew (no LD_PRELOAD, the secure PATH, SUDO_USER=kei, HOME=/root).
#  3. A user not in wheel ("tester", added by the test; root sets its password with passwd -s) is refused by sudo.
#  4. passwd: kei changes its password (passwd -s: current, new): 0; a wrong current password is 3; a short new one 4;
#     /etc/shadow stays 0400 root with kei's new SHA-512 crypt hash ($6$rounds=65536$).
#  5. su: tester becomes kei with the new password (id -un is kei), not with the old one; root's account locked
#     ("*", as on a release) refuses su to root even with a password; root's line is put back.
#  6. The system log (/var/log/messages) has sudo's, su's and passwd's lines.
#  7. With sshpass on the host: SSH as kei with the new password works, with the old one not (skipped otherwise).
#
#   plan/tools/guest/guest.py start IMAGE; plan/tools/guest/guest.py wait
#   plan/ws160/tests/p001-guest.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws160-p001}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
status=0
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }
expect() { if printf '%s\n' "$3" | grep -Eq "$2"; then pass "$1"; else fail "$1"; printf '%s\n' "$3" | sed 's/^/    /'; fi; }
as_kei() { guest "/bin/su kei -c '$1'"; }

# 1. The modes.
modes=$(guest 'ls -l /bin/passwd /bin/su /bin/sudo')
printf '%s\n' "$modes" > "$out/modes.txt"
expect setuid-passwd '^-rwsr-xr-x .* root .*/bin/passwd$' "$modes"
expect setuid-su '^-rwsr-xr-x .* root .*/bin/su$' "$modes"
expect setuid-sudo '^-rwsr-xr-x .* root .*/bin/sudo$' "$modes"

# 2. sudo for kei.
expect sudo-root "$(printf '^0$')" "$(as_kei 'printf "kei\n" | /bin/sudo -S /bin/id -u 2>/dev/null')"
expect sudo-wrong 'status=1' "$(as_kei 'printf "wrong\n" | /bin/sudo -S /bin/id -u; echo status=$?')"
environment=$(as_kei 'printf "kei\n" | LD_PRELOAD=/tmp/x.so PATH=/tmp:/bin /bin/sudo -S env 2>/dev/null')
printf '%s\n' "$environment" > "$out/sudo-env.txt"
if printf '%s\n' "$environment" | grep -q '^LD_PRELOAD='; then fail sudo-env-no-ld-preload; else pass sudo-env-no-ld-preload; fi
expect sudo-env-path '^PATH=/bin:/sbin:/usr/bin:/usr/sbin$' "$environment"
expect sudo-env-sudo-user '^SUDO_USER=kei$' "$environment"
expect sudo-env-home '^HOME=/root$' "$environment"

# 3. tester, not in wheel.
guest 'grep -q "^tester:" /etc/passwd || { echo "tester:x:1001:1001:Tester:/tmp:/bin/sh" >> /etc/passwd; echo "tester:x:1001:" >> /etc/group; chmod 600 /etc/shadow; echo "tester:*:20000:0:99999:7:::" >> /etc/shadow; chmod 400 /etc/shadow; }' >/dev/null
expect passwd-root-sets 'status=0' "$(guest 'printf "testpass1\n" | /bin/passwd -s tester; echo status=$?')"
expect sudo-not-wheel 'not in the wheel group' "$(guest "/bin/su tester -c 'printf \"testpass1\n\" | /bin/sudo -S /bin/id -u' 2>&1")"

# 4. passwd for kei.
expect passwd-wrong-current 'status=3' "$(as_kei 'printf "wrong\nnewpass123\n" | /bin/passwd -s; echo status=$?')"
expect passwd-short 'status=4' "$(as_kei 'printf "kei\nshort\n" | /bin/passwd -s; echo status=$?')"
expect passwd-changes 'status=0' "$(as_kei 'printf "kei\nnewpass123\n" | /bin/passwd -s; echo status=$?')"
shadow=$(guest 'ls -l /etc/shadow; grep "^kei:" /etc/shadow | cut -c1-20')
printf '%s\n' "$shadow" > "$out/shadow.txt"
expect shadow-mode '^-r-------- .* root ' "$shadow"
expect shadow-hash '^kei:\$6\$rounds=65536\$' "$shadow"

# 5. su.
expect su-new-password '^kei$' "$(guest "/bin/su tester -c 'printf \"newpass123\n\" | /bin/su kei -c \"id -un\"' 2>/dev/null")"
expect su-old-password 'authentication failed' "$(guest "/bin/su tester -c 'printf \"kei\n\" | /bin/su kei -c \"id -un\"' 2>&1")"
guest 'cp /etc/shadow /tmp/shadow.saved; chmod 600 /etc/shadow; sed -i.bak "s/^root:[^:]*:/root:*:/" /etc/shadow 2>/dev/null || { sed "s/^root:[^:]*:/root:*:/" /tmp/shadow.saved > /etc/shadow; }; chmod 400 /etc/shadow' >/dev/null
expect su-root-locked 'authentication failed' "$(guest "/bin/su tester -c 'printf \"anything\n\" | /bin/su -c id' 2>&1")"
guest 'cp /tmp/shadow.saved /etc/shadow; chmod 400 /etc/shadow; rm -f /etc/shadow.bak' >/dev/null

# 6. The system log.
log=$(guest 'grep -E "sudo|su\[|passwd" /var/log/messages | tail -20')
printf '%s\n' "$log" > "$out/messages.txt"
expect log-sudo 'sudo.*kei : TTY=.* ; USER=root ; COMMAND=/bin/id -u' "$log"
expect log-su 'su.*tester to kei' "$log"
expect log-passwd 'passwd.*the password of kei was changed by kei' "$log"

# 7. SSH with the new password, when sshpass is there.
if command -v sshpass >/dev/null 2>&1; then
	port=$(python3 -c 'import json,os; print(json.load(open(os.environ.get("GUEST_RUNTIME","build/guest")+"/session.json"))["ssh_port"])')
	opts="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no -o ConnectTimeout=10 -p $port"
	expect ssh-new-password '^kei$' "$(timeout 30 sshpass -p newpass123 ssh $opts kei@127.0.0.1 id -un 2>/dev/null)"
	if timeout 30 sshpass -p kei ssh $opts kei@127.0.0.1 true 2>/dev/null; then fail ssh-old-password-refused; else pass ssh-old-password-refused; fi
else
	echo "ssh-new-password: skipped (no sshpass on the host)"
fi

echo "p001-guest: status $status (outputs in $out)"
exit $status
