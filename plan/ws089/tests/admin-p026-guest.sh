#!/bin/sh
# ws089-p026: account-admin (docs/architecture/security.md, "Account administration") on the guest of
# plan/ws089/tests/config-amd64-account-admin.mk (plan/tools/guest/guest.py start IMAGE; the harness logs in as root
# over SSH).  kei (password "kei", in wheel) is the administrator; the requests go to the tool's standard input as the
# compositor's backend gives them, without a terminal.  /etc/passwd, /etc/group and /etc/shadow are saved first and
# put back at the end.
#  1. /usr/libexec/account-admin is set-user-ID root, mode 4555 (-r-sr-xr-x root).
#  2. Refusals: root running it (root), a wrong password (bad-password, after about 2 s), a request not understood
#     (bad-request), a bad name (bad-name), a weak password (weak-password).
#  3. kei adds alice (a standard user): ok; alice's passwd, group and shadow lines; /home/alice mode 0700 owned by
#     alice; the same name again is name-taken.
#  4. alice, not an administrator, is refused (not-administrator) and her password is not checked.
#  5. kei resets alice's password: ok; su from kei to alice works with the new one.
#  6. Groups: alice into network and wheel and out of wheel; wheel keeps its ID 0 and root; kei taking himself out of
#     wheel is self; root's account (uid 0) as a target is root.
#  7. busy: alice with a running process cannot be removed; after it ends, alice is removed keeping her home; adding
#     alice again is home-exists.
#  8. bob added and removed with remove-home: his home goes, a link in it to /etc/passwd is not followed.
#  9. passwd -s refuses a short new password (status 4) without a terminal.
# 10. The system log has account-admin's lines and no password.
# PASS: the last line "admin-p026: status=0".
#   plan/tools/guest/guest.py start IMAGE; plan/tools/guest/guest.py wait
#   plan/ws089/tests/admin-p026-guest.sh [OUTDIR]          (default build/ws089-p026-guest)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws089-p026-guest}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
status=0
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }
expect() { if printf '%s\n' "$3" | grep -Eq "$2"; then pass "$1"; else fail "$1"; printf '%s\n' "$3" | sed 's/^/    /'; fi; }
tool=/usr/libexec/account-admin
# Runs the tool as a user with a request (the lines given as printf's format) and prints its answer and status.
as() { guest "/bin/su $1 -c 'printf \"$2\" | $tool; echo status=\$?'"; }

# The files saved.
guest 'cp -p /etc/passwd /tmp/p026.passwd; cp -p /etc/group /tmp/p026.group; cp -p /etc/shadow /tmp/p026.shadow' >/dev/null

# 1. The mode.
expect setuid '^-r-sr-xr-x .* root .*/usr/libexec/account-admin$' "$(guest "ls -l $tool")"

# 2. Refusals.
expect root-refused '^error root' "$(guest "printf 'x\nremove\nkei\nkeep-home\n' | $tool")"
started=$(date +%s)
expect bad-password '^error bad-password' "$(as kei 'wrong\nreset-password\nkei\nwhatever12\n')"
elapsed=$(( $(date +%s) - started ))
[ "$elapsed" -ge 2 ] && pass bad-password-pause || fail "bad-password-pause (${elapsed}s)"
expect bad-request '^error bad-request' "$(as kei 'kei\nchmod\nalice\nx\n')"
expect bad-name '^error bad-name' "$(as kei 'kei\nadd\nAlice\nAlice\nalicepass1\nuser\n')"
expect weak-password '^error weak-password' "$(as kei 'kei\nadd\nalice\nAlice\nshort\nuser\n')"

# 3. alice added.
expect add-alice '^ok' "$(as kei 'kei\nadd\nalice\nAlice Tanaka\nalicepass1\nuser\n')"
files=$(guest 'grep "^alice:" /etc/passwd /etc/group; grep "^alice:" /etc/shadow | cut -c1-24; ls -ld /home/alice')
printf '%s\n' "$files" > "$out/alice.txt"
expect alice-passwd '/etc/passwd:alice:x:1001:1001:Alice Tanaka:/home/alice:/bin/sh' "$files"
expect alice-group '/etc/group:alice:x:1001:' "$files"
expect alice-shadow '^alice:\$6\$rounds=65536\$' "$files"
expect alice-home '^drwx------ .* alice .*/home/alice$' "$files"
expect name-taken '^error name-taken' "$(as kei 'kei\nadd\nalice\nAlice\nalicepass1\nuser\n')"

# 4. alice is not an administrator.
expect alice-not-admin '^error not-administrator' "$(as alice 'wrong\nremove\nkei\nkeep-home\n')"

# 5. alice's password reset.
expect reset-alice '^ok' "$(as kei 'kei\nreset-password\nalice\nalicenew99\n')"
expect su-alice-new '^alice$' "$(guest "/bin/su kei -c 'printf \"alicenew99\n\" | /bin/su alice -c /bin/whoami' 2>/dev/null")"

# 6. Groups.
expect network-add '^ok' "$(as kei 'kei\ngroup-add\nalice\nnetwork\n')"
expect wheel-add '^ok' "$(as kei 'kei\ngroup-add\nalice\nwheel\n')"
groups=$(guest 'grep -E "^(wheel|network):" /etc/group; grep "^root:" /etc/passwd')
printf '%s\n' "$groups" > "$out/groups.txt"
expect wheel-line '^wheel:x:0:root,kei,alice$' "$groups"
expect network-line '^network:x:69:kei,alice$' "$groups"
expect root-primary '^root:x:0:0:' "$groups"
expect wheel-remove '^ok' "$(as kei 'kei\ngroup-remove\nalice\nwheel\n')"
expect wheel-after '^wheel:x:0:root,kei$' "$(guest 'grep "^wheel:" /etc/group')"
expect self '^error self' "$(as kei 'kei\ngroup-remove\nkei\nwheel\n')"
expect root-target '^error root' "$(as kei 'kei\nreset-password\nroot\nrootpass12\n')"

# 7. busy, then removed keeping the home, then home-exists.
guest "/bin/su alice -c 'sleep 40' >/dev/null 2>&1 </dev/null & sleep 1; echo started" >/dev/null
expect busy '^error busy' "$(as kei 'kei\nremove\nalice\nkeep-home\n')"
guest 'for p in $(ps -A -o pid,args | grep "[s]leep 40" | awk "{print \$1}"); do kill $p; done; sleep 1' >/dev/null
expect remove-alice '^ok' "$(as kei 'kei\nremove\nalice\nkeep-home\n')"
after=$(guest 'grep -c "alice" /etc/passwd /etc/group /etc/shadow; ls -ld /home/alice')
printf '%s\n' "$after" > "$out/alice-removed.txt"
expect alice-gone '/etc/passwd:0' "$after"
expect alice-gone-group '/etc/group:0' "$after"
expect alice-home-kept '/home/alice$' "$after"
expect home-exists '^error home-exists' "$(as kei 'kei\nadd\nalice\nAlice\nalicepass1\nuser\n')"

# 8. bob with remove-home; a link in his home is not followed.
expect add-bob '^ok' "$(as kei 'kei\nadd\nbob\nBob\nbobpass123\nuser\n')"
guest '/bin/su bob -c "echo hi > /home/bob/note; ln -s /etc/passwd /home/bob/link; mkdir /home/bob/sub; echo x > /home/bob/sub/y"' >/dev/null
expect remove-bob '^ok' "$(as kei 'kei\nremove\nbob\nremove-home\n')"
expect bob-home-gone 'gone' "$(guest '[ -e /home/bob ] && echo still || echo gone')"
expect passwd-intact '^root:x:0:0:' "$(guest 'head -1 /etc/passwd')"

# 9. passwd -s refuses a short password without a terminal.
expect passwd-short 'status=4' "$(guest "/bin/su kei -c 'printf \"kei\nshort\n\" | /bin/passwd -s; echo status=\$?'")"

# 10. The log, without passwords.
log=$(guest 'grep account-admin /var/log/messages')
printf '%s\n' "$log" > "$out/messages.txt"
expect log-add 'add by kei for alice: ok' "$log"
expect log-refused 'refused: a wrong password for kei' "$log"
if printf '%s\n' "$log" | grep -Eq 'alicepass1|alicenew99|bobpass123'; then fail log-no-password; else pass log-no-password; fi

# The files put back (alice's kept home removed).
guest 'cp -p /tmp/p026.passwd /etc/passwd; cp -p /tmp/p026.group /etc/group; cp -p /tmp/p026.shadow /etc/shadow; rm -rf /home/alice /home/bob' >/dev/null
echo "admin-p026: status=$status"
exit $status
