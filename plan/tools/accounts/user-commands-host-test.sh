#!/bin/sh
# ws196-p003: useradd, usermod and userdel built for the Linux host (ASan and UBSan) and run as root through sudo.
# Two builds of the three commands: one runs a stand-in account-admin (a shell script that keeps the request and gives
# back a chosen answer), to check the exact request each set of options makes and the exit status and message of every
# answer; the other runs the real account-admin, built as in account-admin-root-host-test.sh with every file it touches
# moved into a fresh folder, to check the commands end to end (usage errors 2, bad values 3, id-taken 4, no-such-user 6,
# name-taken 9, home-exists 12, last-administrator and no-password 1, and the files after each change).  Also: a caller
# who is not root is refused (1).  The host's account files are not touched.  The last line is
# "user-commands-host-test: PASS".
#   sh plan/ws196/tests/user-commands-host-test.sh   (from the repository's top; needs sudo, openssl)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
mkdir -p build/tmp
OUT=$(realpath "$(mktemp -d build/tmp/ws196-user-commands.XXXXXX)")
mkdir -p "$OUT/etc" "$OUT/shadowdir" "$OUT/home" "$OUT/skel" "$OUT/cron" "$OUT/at" "$OUT/real" "$OUT/fake"
CFLAGS="-std=gnu17 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -I. -Iinclude"

# account-admin, every path in the folder.
# shellcheck disable=SC2086
cc $CFLAGS -Wno-format-truncation \
	-DACCOUNT_PASSWD_FILE="\"$OUT/etc/passwd\"" -DACCOUNT_GROUP_FILE="\"$OUT/etc/group\"" \
	-DACCOUNT_SHADOW_PATH="\"$OUT/shadowdir/shadow\"" -DACCOUNT_SHADOW_LOCK="\"$OUT/etc/shadow.lock\"" \
	-DPASSKEY_FILE="\"$OUT/etc/passkey\"" -DADMIN_HOME_BASE="\"$OUT/home\"" -DADMIN_SKELETON="\"$OUT/skel\"" \
	-DADMIN_LAST_ID_FILE="\"$OUT/etc/account-admin.last-id\"" -DADMIN_CRON_SPOOL="\"$OUT/cron\"" \
	-DADMIN_AT_SPOOL="\"$OUT/at\"" -DADMIN_AUTOLOGIN_FILE="\"$OUT/etc/autologin\"" \
	-DADMIN_LANGUAGE_FILE="\"$OUT/etc/language\"" -DADMIN_LANGUAGE_DIRECTORY="\"$OUT/etc\"" \
	-o "$OUT/account-admin" userland/base/account-admin/main.c userland/base/account-admin/edit.c \
	userland/base/common/account.c userland/base/login/verify.c userland/base/passkey/record.c -lcrypt

# The three commands, once for the real account-admin and once for the stand-in.
for command in useradd usermod userdel; do
	# shellcheck disable=SC2086
	cc $CFLAGS -DACCOUNT_ADMIN_PATH="\"$OUT/account-admin\"" -o "$OUT/real/$command" \
		"userland/base/$command/main.c" userland/base/user-control/front.c
	# shellcheck disable=SC2086
	cc $CFLAGS -DACCOUNT_ADMIN_PATH="\"$OUT/fake-admin\"" -o "$OUT/fake/$command" \
		"userland/base/$command/main.c" userland/base/user-control/front.c
done

# The stand-in: keeps the request and the SUDO_USER it was given, and answers what fake-answer holds.
cat > "$OUT/fake-admin" <<EOF
#!/bin/sh
cat > "$OUT/fake-request"
printf '%s\n' "\${SUDO_USER-}" > "$OUT/fake-sudo-user"
cat "$OUT/fake-answer"
EOF
chmod 0755 "$OUT/fake-admin"
: > "$OUT/fake-answer"

# The account files: kei the only administrator who can log in, hal a user, sshd a system account.
hash=$(openssl passwd -6 -salt p003salt secret123)
cat > "$OUT/etc/passwd" <<EOF
root:x:0:0:root:/root:/bin/sh
sshd:x:22:22::/var/empty:/usr/sbin/nologin
kei:x:1000:1000:Kei:$OUT/home/kei:/bin/sh
hal:x:1002:1002::$OUT/home/hal:/bin/sh
EOF
cat > "$OUT/etc/group" <<EOF
wheel:x:0:root,kei
network:x:69:
sshd:x:22:
kei:x:1000:
hal:x:1002:
EOF
cat > "$OUT/shadowdir/shadow" <<EOF
root:*:20000:0:99999:7:::
kei:$hash:20000:0:99999:7:::
hal:$hash:20000:0:99999:7:::
EOF
printf 'echo hello\n' > "$OUT/skel/.profile"
printf '%s\n' "$hash" > "$OUT/hash"

# A caller who is not root is refused before anything runs.
set +e
"$OUT/real/useradd" tom 2> "$OUT/not-root.err"
not_root=$?
set -e

sudo sh plan/ws196/tests/user-commands-run.sh "$OUT" > "$OUT/out.txt" 2>&1 || true
if [ "$not_root" = 1 ] && grep -q '^useradd: Permission denied\.$' "$OUT/not-root.err"; then
	echo "ok not-root" >> "$OUT/out.txt"
else
	echo "FAIL not-root (status $not_root)" >> "$OUT/out.txt"
fi
failed=0
if grep -q '^FAIL' "$OUT/out.txt"; then failed=1; fi
if grep -q 'AddressSanitizer\|runtime error' "$OUT/out.txt"; then failed=1; fi
if ! grep -q '^== done' "$OUT/out.txt"; then failed=1; fi
grep -v '^ok ' "$OUT/out.txt" | grep -v '^== done' || true
echo "$(grep -c '^ok ' "$OUT/out.txt") checks passed ($OUT/out.txt)"
if [ $failed -eq 0 ]; then echo "user-commands-host-test: PASS"; else echo "user-commands-host-test: FAIL"; fi
exit $failed
