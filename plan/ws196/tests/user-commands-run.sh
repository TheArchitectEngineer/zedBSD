#!/bin/sh
# ws196-p003: the root half of user-commands-host-test.sh, run by it through sudo with the folder it prepared.
# Prints "ok NAME" or "FAIL NAME (...)" a check and "== done" at the end.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
O=$1
F="$O/fake"
R="$O/real"
chmod 0400 "$O/shadowdir/shadow"

# st NAME STATUS COMMAND...: the command's exit status (its standard error kept in $O/err).
st() {
	name=$1
	want=$2
	shift 2
	"$@" > "$O/stdout" 2> "$O/err"
	got=$?
	if grep -q 'AddressSanitizer\|runtime error' "$O/err"; then
		echo "FAIL $name (sanitizer)"
		cat "$O/err"
	fi
	if [ "$got" = "$want" ]; then echo "ok $name"; else echo "FAIL $name (status $got, wanted $want: $(cat "$O/err"))"; fi
}
said() { if grep -q -e "$2" "$O/err"; then echo "ok $1"; else echo "FAIL $1 (no '$2' in '$(cat "$O/err")')"; fi; }
quiet() { if [ -s "$O/err" ]; then echo "FAIL $1 (said '$(cat "$O/err")')"; else echo "ok $1"; fi; }
is() { if [ "$2" = "$3" ]; then echo "ok $1"; else echo "FAIL $1 (got '$2', wanted '$3')"; fi; }
has() { if grep -q -e "$3" "$2"; then echo "ok $1"; else echo "FAIL $1 (no '$3' in $2)"; fi; }
lacks() { if grep -q -e "$3" "$2"; then echo "FAIL $1 ('$3' in $2)"; else echo "ok $1"; fi; }
there() { if [ -e "$2" ]; then echo "ok $1"; else echo "FAIL $1 ($2 missing)"; fi; }
gone() { if [ -e "$2" ]; then echo "FAIL $1 ($2 there)"; else echo "ok $1"; fi; }
# req NAME TEXT: the stand-in was given exactly TEXT (printf %b), up to its last line's end.
req() { is "$1" "$(cat "$O/fake-request")" "$(printf '%b' "$2")"; }
answer() { printf '%s\n' "$1" > "$O/fake-answer"; }

### The requests the options make (the stand-in answers ok).
answer ok
st req-add-default 0 "$F/useradd" tom
req req-add-default-text 'user-add\nname tom\nhome make'
quiet req-add-default-quiet
st req-add-all 0 "$F/useradd" -c 'C D' -g wheel -G wheel,network -s /bin/zsh -u 1234 -M tom
req req-add-all-text 'user-add\nname tom\ncomment C D\ngroup wheel\ngroups wheel,network\nshell /bin/zsh\nuid 1234\nhome none'
st req-add-m 0 "$F/useradd" -m tom
req req-add-m-text 'user-add\nname tom\nhome make'
st req-add-last-wins 0 "$F/useradd" -c one -c two tom
req req-add-last-wins-text 'user-add\nname tom\ncomment two\nhome make'
st req-mod-all 0 "$F/usermod" -a -G wheel -L -c x -g network -s /bin/sh tom
req req-mod-all-text 'user-modify\nname tom\ncomment x\ngroup network\ngroups-add wheel\nshell /bin/sh\npassword lock'
st req-mod-unlock 0 "$F/usermod" -U tom
req req-mod-unlock-text 'user-modify\nname tom\npassword unlock'
st req-mod-groups-empty 0 "$F/usermod" -G '' tom
req req-mod-groups-empty-text 'user-modify\nname tom\ngroups '
st req-del 0 "$F/userdel" tom
req req-del-text 'user-remove\nname tom'
st req-del-r 0 "$F/userdel" -r tom
req req-del-r-text 'user-remove\nname tom\nhome remove'
st req-del-f 0 "$F/userdel" -f tom
req req-del-f-text 'user-remove\nname tom\nforce yes'
is req-sudo-user "$(cat "$O/fake-sudo-user")" "${SUDO_USER-}"

### Usage errors (2), before account-admin is run.
: > "$O/fake-request"
st use-add-none 2 "$F/useradd"
st use-add-two 2 "$F/useradd" tom ann
st use-add-empty 2 "$F/useradd" ''
st use-add-m-M 2 "$F/useradd" -m -M tom
said use-add-m-M-said 'not taken together'
st use-add-d 2 "$F/useradd" -d /x tom
st use-add-p 2 "$F/useradd" -p hash tom
st use-mod-none 2 "$F/usermod" tom
said use-mod-none-said 'no change'
st use-mod-a 2 "$F/usermod" -a tom
said use-mod-a-said 'only with -G'
st use-mod-L-U 2 "$F/usermod" -L -U tom
st use-mod-u 2 "$F/usermod" -u 1500 tom
st use-mod-d 2 "$F/usermod" -d /x tom
st use-mod-l 2 "$F/usermod" -l new tom
st use-del-none 2 "$F/userdel"
st use-del-r-f 2 "$F/userdel" -r -f tom
said use-del-r-f-said 'not taken together'
st use-del-x 2 "$F/userdel" -x tom
is use-not-sent "$(cat "$O/fake-request")" ''

### Values that would break the request (3), before account-admin is run.
st value-line-break 3 "$F/useradd" -c "$(printf 'a\nb')" tom
said value-line-break-said 'comment may not hold a line break'
st value-name-line-break 3 "$F/userdel" "$(printf 'tom\nforce yes')"
long=$(head -c 5000 /dev/zero | tr '\0' a)
st value-too-long 3 "$F/useradd" -c "$long" tom
said value-too-long-said 'comment is too long'
is value-not-sent "$(cat "$O/fake-request")" ''

### Each answer: its exit status and message.
check_answer() {
	answer "$1"
	st "answer-$2" "$3" "$F/userdel" tom
	said "answer-$2-said" "$4"
}
check_answer 'error bad-request' bad-request 2 '^userdel: tom: account-admin did not take the request$'
check_answer 'error bad-value' bad-value 3 'a value is outside its rules'
check_answer 'error bad-name' bad-name 3 'not a valid user name'
check_answer 'error id-taken' id-taken 4 'user ID is used'
check_answer 'error no-such-user' no-such-user 6 'no such user'
check_answer 'error busy' busy 8 'running process'
check_answer 'error name-taken' name-taken 9 'name is used'
check_answer 'error home-exists' home-exists 12 'home is already there'
check_answer 'error home-failed' home-failed 12 'home could not be made'
check_answer 'error root' root 1 'system accounts are not changed'
check_answer 'error last-administrator' last-administrator 1 'no administrator who can log in'
check_answer 'error no-password' no-password 1 'set one with passwd'
check_answer 'error failed' failed 1 'system log says why'
check_answer 'error self' unknown 1 'account-admin refused: self$'
check_answer 'garbage' garbage 1 'gave no answer'
check_answer '' none 1 'gave no answer'

### The real account-admin: useradd.
st add-bob 0 "$R/useradd" -G wheel -c 'Bob B' bob
quiet add-bob-quiet
has add-bob-passwd "$O/etc/passwd" "^bob:x:1003:1003:Bob B:$O/home/bob:/bin/sh\$"
has add-bob-wheel "$O/etc/group" '^wheel:x:0:root,kei,bob$'
has add-bob-shadow "$O/shadowdir/shadow" '^bob:!:'
there add-bob-home "$O/home/bob/.profile"
st add-bob-again 9 "$R/useradd" bob
said add-bob-again-said '^useradd: bob: the name is used by a user or a group$'
st add-uid-taken 4 "$R/useradd" -u 1000 erin
st add-uid-low 3 "$R/useradd" -u 99 erin
st add-groups-other 3 "$R/useradd" -G staff erin
st add-group-other 3 "$R/useradd" -g dave erin
st add-shell-missing 3 "$R/useradd" -s /nonexistent/sh erin
st add-bad-name 3 "$R/useradd" Bad
said add-bad-name-said 'not a valid user name'
lacks add-refused-none "$O/etc/passwd" '^erin:'
st add-erin 0 "$R/useradd" -M -u 1500 -g network -s /bin/sh erin
has add-erin-passwd "$O/etc/passwd" "^erin:x:1500:69::$O/home/erin:/bin/sh\$"
has add-erin-group "$O/etc/group" '^erin:x:1500:$'
gone add-erin-no-home "$O/home/erin"
mkdir "$O/home/fay"
st add-fay-home-exists 12 "$R/useradd" fay
said add-fay-home-exists-said 'home is already there'
st add-fay-no-home 0 "$R/useradd" -M fay
has add-fay-uid "$O/etc/passwd" '^fay:x:1501:1501:'

### usermod.
st mod-unlock-none 1 "$R/usermod" -U bob
said mod-unlock-none-said 'no password is behind the lock'
st mod-last-lock 1 "$R/usermod" -L kei
said mod-last-lock-said 'no administrator who can log in would be left'
if [ -x /usr/sbin/nologin ]; then
	st mod-last-shell 1 "$R/usermod" -s /usr/sbin/nologin kei
fi
st mod-last-groups 1 "$R/usermod" -G network kei
st mod-comment 0 "$R/usermod" -c 'Kei K' kei
has mod-comment-passwd "$O/etc/passwd" '^kei:x:1000:1000:Kei K:'
st mod-groups-add 0 "$R/usermod" -a -G network bob
has mod-groups-add-network "$O/etc/group" '^network:x:69:bob$'
has mod-groups-add-wheel-kept "$O/etc/group" '^wheel:x:0:root,kei,bob$'
st mod-groups-empty 0 "$R/usermod" -G '' bob
has mod-groups-empty-wheel "$O/etc/group" '^wheel:x:0:root,kei$'
has mod-groups-empty-network "$O/etc/group" '^network:x:69:$'
st mod-primary 0 "$R/usermod" -g wheel hal
has mod-primary-passwd "$O/etc/passwd" '^hal:x:1002:0:'
st mod-lock 0 "$R/usermod" -L hal
has mod-locked "$O/shadowdir/shadow" '^hal:!\$6\$'
st mod-unlock 0 "$R/usermod" -U hal
is mod-unlocked "$(grep '^hal:' "$O/shadowdir/shadow" | cut -d: -f2)" "$(cat "$O/hash")"
st mod-missing 6 "$R/usermod" -c x ghost
said mod-missing-said '^usermod: ghost: there is no such user$'
st mod-system 1 "$R/usermod" -c x sshd
said mod-system-said 'system accounts are not changed'
st mod-primary-own 0 "$R/usermod" -g hal hal
has mod-primary-own-passwd "$O/etc/passwd" '^hal:x:1002:1002:'

### userdel.
st del-missing 6 "$R/userdel" ghost
st del-system 1 "$R/userdel" sshd
st del-last 1 "$R/userdel" kei
said del-last-said 'no administrator who can log in would be left'
has del-last-kept "$O/etc/passwd" '^kei:'
st del-bob 0 "$R/userdel" -r bob
lacks del-bob-passwd "$O/etc/passwd" '^bob:'
lacks del-bob-shadow "$O/shadowdir/shadow" '^bob:'
lacks del-bob-group "$O/etc/group" '^bob:'
gone del-bob-home "$O/home/bob"
st del-erin-force 0 "$R/userdel" -f erin
lacks del-erin-passwd "$O/etc/passwd" '^erin:'
st del-fay 0 "$R/userdel" fay
there del-fay-home-kept "$O/home/fay"
st del-bob-again 6 "$R/userdel" bob

# S3 through the command: the removed fay's 1501 is not given again.
st add-gil 0 "$R/useradd" -M gil
has add-gil-uid "$O/etc/passwd" '^gil:x:1502:1502:'
echo "== done"
