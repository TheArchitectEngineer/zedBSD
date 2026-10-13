#!/bin/sh
# ws196-p004 (T1): account-admin's root form (ws196-p002) and root's useradd, usermod and userdel (ws196-p003) on the
# guest of plan/ws196/tests/config-amd64-users.mk (plan/tools/guest/guest.py start IMAGE; the harness logs in as root
# over SSH).  kei (password "kei", in wheel) is the only administrator who can log in.  /etc/passwd, /etc/group and
# /etc/shadow are saved first and put back at the end (in the guest), with the homes made and the record of the last
# user ID given.
#  1. /sbin/useradd, usermod, userdel: mode 0755 root, not set-user-ID; kei (not root) is refused with status 1.
#  2. account-admin's root form: user-add (uid above every one used and equal to /etc/account-admin.last-id, home 0700
#     owned by the user, a locked password), an unlock with no password (no-password), kei's lock (last-administrator),
#     user-remove with home remove, and a later user-add given a higher uid (no reuse).
#  3. The lock: two user-adds at once both ok, /etc/shadow.lock left, passwd -s works after them (flock on zedBSD).
#  4. useradd: -G wheel,network -c; the same name again (9); usermod -U with no password (1); usermod -L kei (1, the
#     new administrator has no password yet); passwd -s, then usermod -L and -U, and su to the user works; usermod -G ''.
#  5. userdel: kei (1, last administrator), a user with a running process (8, then -f: 0), -r (the home goes),
#     -r with -f (2), a name not there (6).
#  6. Through sudo: kei runs sudo -S useradd; the system log says "user-add by root (sudo kei) for sam: ok".
# PASS: the last line "user-commands-guest: status=0".
#   plan/tools/guest/guest.py start IMAGE; plan/tools/guest/guest.py wait
#   plan/ws196/tests/user-commands-guest.sh [OUTDIR]          (default build/ws196-p004-guest)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws196-p004-guest}
mkdir -p "$out"
: > "$out/ssh-retries.txt"
# Runs a command in the guest and prints its output.  After one boot the guest closed SSH connections at once for
# about a minute (T1-597: empty answers, ssh's status 255): a status of 255 is ssh's own failure, so the command is
# given again every 3 s, for up to 90 s, and each time is written to $out/ssh-retries.txt (a connection lost while the
# command ran would run it twice, so a retry is reported at the end).
guest() {
	g_tries=0
	while :; do
		g_answer=$(timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null)
		g_code=$?
		if [ "$g_code" -ne 255 ] || [ "$g_tries" -ge 30 ]; then break; fi
		g_tries=$((g_tries + 1))
		printf '%s %s\n' "$g_tries" "$1" >> "$out/ssh-retries.txt"
		sleep 3
	done
	printf '%s\n' "$g_answer"
}
status=0
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }
expect() { if printf '%s\n' "$3" | grep -Eq "$2"; then pass "$1"; else fail "$1"; printf '%s\n' "$3" | sed 's/^/    /'; fi; }
tool=/usr/libexec/account-admin
# Runs root's request (the lines given as printf's format) and prints the answer.
admin() { guest "printf '$1' | $tool"; }
# Runs a command as root and prints its messages and, on a line of its own, its status (a prompt such as passwd's
# "New password: " or sudo's has no line end of its own).
run() { guest "$1 2>&1; s=\$?; echo; echo status=\$s"; }
# The uid of a user in /etc/passwd.
uid_of() { guest "grep '^$1:' /etc/passwd | cut -d: -f3" | tr -d '\r'; }

# The files saved.
guest 'cp -p /etc/passwd /tmp/p004.passwd; cp -p /etc/group /tmp/p004.group; cp -p /etc/shadow /tmp/p004.shadow' >/dev/null

# 1. The commands' modes; only root runs them.
modes=$(guest 'ls -l /sbin/useradd /sbin/usermod /sbin/userdel')
printf '%s\n' "$modes" > "$out/modes.txt"
expect mode-useradd '^-rwxr-xr-x .* root .*/sbin/useradd$' "$modes"
expect mode-usermod '^-rwxr-xr-x .* root .*/sbin/usermod$' "$modes"
expect mode-userdel '^-rwxr-xr-x .* root .*/sbin/userdel$' "$modes"
refused=$(guest "/bin/su kei -c '/sbin/useradd tom; echo status=\$?' 2>&1")
expect not-root-said 'useradd: Permission denied\.' "$refused"
expect not-root-status '^status=1$' "$refused"

# 2. account-admin's root form.
expect form-add '^ok$' "$(admin 'user-add\nname tom\ngroups network\n')"
tom_uid=$(uid_of tom)
expect form-add-uid '^(100[1-9]|10[1-9][0-9]|1[1-9][0-9][0-9]|[2-9][0-9]{3}|[1-5][0-9]{4})$' "$tom_uid"
expect form-add-record "^$tom_uid\$" "$(guest 'cat /etc/account-admin.last-id')"
expect form-add-home '^drwx------ .* tom .*/home/tom$' "$(guest 'ls -ld /home/tom')"
expect form-add-locked '^tom:!:' "$(guest 'grep "^tom:" /etc/shadow')"
expect form-no-password '^error no-password$' "$(admin 'user-modify\nname tom\npassword unlock\n')"
expect form-last-lock '^error last-administrator$' "$(admin 'user-modify\nname kei\npassword lock\n')"
expect form-remove '^ok$' "$(admin 'user-remove\nname tom\nhome remove\n')"
expect form-remove-home 'gone' "$(guest '[ -e /home/tom ] && echo still || echo gone')"
expect form-add-again '^ok$' "$(admin 'user-add\nname tom\nhome none\n')"
again_uid=$(uid_of tom)
if [ -n "$again_uid" ] && [ -n "$tom_uid" ] && [ "$again_uid" -gt "$tom_uid" ]; then pass form-no-reuse; else fail "form-no-reuse ($tom_uid then $again_uid)"; fi
expect form-remove-again '^ok$' "$(admin 'user-remove\nname tom\n')"

# 3. The lock under two requests at once.
both=$(guest "printf 'user-add\nname lockone\nhome none\n' | $tool > /tmp/p004.one & printf 'user-add\nname locktwo\nhome none\n' | $tool > /tmp/p004.two & wait; cat /tmp/p004.one /tmp/p004.two; ls /etc/shadow.lock")
printf '%s\n' "$both" > "$out/lock.txt"
expect lock-both-ok '^ok$' "$(printf '%s\n' "$both" | sed -n 1p)"
expect lock-both-ok-2 '^ok$' "$(printf '%s\n' "$both" | sed -n 2p)"
expect lock-file '^/etc/shadow.lock$' "$both"
expect lock-passwd '^status=0$' "$(run "printf 'lockpass123\n' | /bin/passwd -s lockone")"
expect lock-clean-1 '^status=0$' "$(run '/sbin/userdel lockone')"
expect lock-clean-2 '^status=0$' "$(run '/sbin/userdel locktwo')"

# 4. useradd and usermod.
added=$(run "/sbin/useradd -G wheel,network -c 'Tom T' tom")
expect add-tom '^status=0$' "$added"
tom_uid=$(uid_of tom)
lines=$(guest 'grep "^tom:" /etc/passwd; grep -E "^(wheel|network|tom):" /etc/group; ls -ld /home/tom')
printf '%s\n' "$lines" > "$out/tom.txt"
expect add-tom-passwd "^tom:x:$tom_uid:$tom_uid:Tom T:/home/tom:/bin/sh\$" "$lines"
expect add-tom-wheel '^wheel:x:0:.*,tom$' "$lines"
expect add-tom-network '^network:x:69:.*tom' "$lines"
expect add-tom-home '^drwx------ .* tom .*/home/tom$' "$lines"
again=$(run '/sbin/useradd tom')
expect add-tom-again '^status=9$' "$again"
expect add-tom-again-said 'useradd: tom: the name is used by a user or a group' "$again"
expect mod-unlock-none '^status=1$' "$(run '/sbin/usermod -U tom')"
expect mod-last-lock '^status=1$' "$(run '/sbin/usermod -L kei')"
expect tom-password '^status=0$' "$(run "printf 'tompass123\n' | /bin/passwd -s tom")"
expect mod-lock '^status=0$' "$(run '/sbin/usermod -L tom')"
expect mod-locked '^tom:!\$6\$' "$(guest 'grep "^tom:" /etc/shadow')"
expect mod-unlock '^status=0$' "$(run '/sbin/usermod -U tom')"
expect su-tom '^tom$' "$(guest "/bin/su kei -c 'printf \"tompass123\n\" | /bin/su tom -c /bin/whoami' 2>/dev/null")"
expect mod-groups-empty '^status=0$' "$(run "/sbin/usermod -G '' tom")"
expect mod-groups-empty-wheel '^wheel:x:0:root,kei$' "$(guest 'grep "^wheel:" /etc/group')"

# 5. userdel.
expect del-last '^status=1$' "$(run '/sbin/userdel kei')"
guest "/bin/su tom -c 'exec sleep 40' >/dev/null 2>&1 </dev/null & echo \$! > /tmp/p004-busy.pid; sleep 1; echo started" >/dev/null
busy=$(run '/sbin/userdel tom')
expect del-busy '^status=8$' "$busy"
expect del-busy-said 'userdel: tom: the user has a running process' "$busy"
expect del-force '^status=0$' "$(run '/sbin/userdel -f tom')"
guest 'kill $(cat /tmp/p004-busy.pid) 2>/dev/null; echo killed' >/dev/null
expect del-force-gone '^0$' "$(guest 'grep -c "^tom:" /etc/passwd')"
expect add-jay '^status=0$' "$(run '/sbin/useradd jay')"
expect del-jay '^status=0$' "$(run '/sbin/userdel -r jay')"
expect del-jay-home 'gone' "$(guest '[ -e /home/jay ] && echo still || echo gone')"
expect del-r-f '^status=2$' "$(run '/sbin/userdel -r -f jay')"
expect del-missing '^status=6$' "$(run '/sbin/userdel nobodyhere')"

# 6. Through sudo, logged with the user who ran it.
expect sudo-add '^status=0$' "$(run "/bin/su kei -c 'printf \"kei\n\" | /bin/sudo -S /sbin/useradd -M sam'")"
log=$(guest 'grep account-admin /var/log/messages')
printf '%s\n' "$log" > "$out/messages.txt"
expect log-sudo 'user-add by root \(sudo kei\) for sam: ok' "$log"
expect sudo-del '^status=0$' "$(run '/sbin/userdel sam')"

# The files put back, with the homes made and the record of the last user ID.
guest 'cp -p /tmp/p004.passwd /etc/passwd; cp -p /tmp/p004.group /etc/group; cp -p /tmp/p004.shadow /etc/shadow; rm -rf /home/tom /home/jay; rm -f /etc/account-admin.last-id' >/dev/null
echo "ssh-retries: $(wc -l < "$out/ssh-retries.txt" | tr -d ' ')"
echo "user-commands-guest: status=$status"
exit $status
