#!/bin/sh
# ws172-p011: passkey as the Linux Keiland builds it (the same sources and flags as userland/base/passkey/Makefile.linux,
# with its file and lock moved into a fresh folder), run as root through sudo in a private mount namespace whose
# /etc/passwd and /etc/shadow are this test's (bind mounts: the host's files are not touched).  Checks: a password,
# a PIN set and checked, the file's mode and line, a security key's request answering "fail no-key" (no backend on
# Linux), and a caller that is not root refused.  The last line is "passkey-linux-host-test: PASS".
#   sh plan/ws172/tests/passkey-linux-host-test.sh   (from the repository's top; needs sudo and unshare)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
mkdir -p build/tmp
OUT=$(realpath "$(mktemp -d build/tmp/ws172-passkey-linux.XXXXXX)")
cc -std=gnu17 -D_GNU_SOURCE -O2 -g -Wall -Wextra -Werror -Wno-format-truncation -I. \
	-DPASSKEY_FILE="\"$OUT/passkey\"" -DACCOUNT_SHADOW_LOCK="\"$OUT/passkey.lock\"" \
	-DPASSKEY_FIDO2_PATH='"/opt/keiland/libexec/passkey-fido2"' \
	-o "$OUT/passkey-program" userland/base/passkey/main.c userland/base/passkey/request.c userland/base/passkey/record.c \
	userland/base/common/account.c userland/base/login/verify.c -lcrypt
hash=$(openssl passwd -6 -salt p011salt secret123)
printf 'root:x:0:0:root:/root:/bin/sh\nkei:x:1500:1500:Kei:/home/kei:/bin/sh\n' > "$OUT/passwd"
printf 'root:*:20000:0:99999:7:::\nkei:%s:20000:0:99999:7:::\n' "$hash" > "$OUT/shadow"
chmod 0644 "$OUT/passwd"
chmod 0600 "$OUT/shadow"
cat > "$OUT/run.sh" <<EOF
set -u
mount --bind "$OUT/passwd" /etc/passwd
mount --bind "$OUT/shadow" /etc/shadow
ask() { printf '%b' "\$1" | "$OUT/passkey-program"; echo "status \$?"; }
echo "== password-good"; ask 'auth\nkei\npassword\nsecret123\n'
echo "== password-bad"; ask 'auth\nkei\npassword\nwrong\n'
echo "== enroll-pin"; ask 'enroll-pin\nkei\nsecret123\n482916\n'
echo "== pin-good"; ask 'auth\nkei\npin\n482916\n'
echo "== pin-bad"; ask 'auth\nkei\npin\n111111\n'
echo "== styles"; ask 'styles\nkei\n'
echo "== fido2-auth"; ask 'auth\nkei\nfido2\nx\n'
echo "== fido2-enroll"; ask 'enroll-fido2\nkei\nsecret123\nlabel\nx\n'
echo "== no-user"; ask 'auth\nnobody-p011\npassword\nx\n'
EOF
sudo unshare -m sh "$OUT/run.sh" > "$OUT/out.txt" 2>&1 || true
nonroot=0
printf 'styles\nkei\n' | "$OUT/passkey-program" > /dev/null 2>&1 || nonroot=$?
failed=0
check() {
	if grep -A2 -x "== $1" "$OUT/out.txt" | grep -q -- "$2"; then
		echo "ok $1"
	else
		echo "FAIL $1 (wanted '$2')"
		failed=1
	fi
}
check password-good '^ok uid=1500'
check password-bad '^fail bad-secret'
check enroll-pin '^ok uid=1500'
check pin-good '^ok uid=1500'
check pin-bad '^fail'
check styles 'pin'
check fido2-auth '^fail no-key'
check fido2-enroll '^fail no-key'
check no-user '^fail no-such-user'
if sudo grep -q '^kei:1500:pin:\$6\$' "$OUT/passkey" && [ "$(sudo stat -c %a "$OUT/passkey")" = 600 ]; then
	echo "ok file (kei's pin line, mode 600)"
else
	echo "FAIL file"
	failed=1
fi
if [ "$nonroot" = 2 ]; then echo "ok not root (status 2)"; else echo "FAIL not root ($nonroot)"; failed=1; fi
if [ $failed -eq 0 ]; then echo "passkey-linux-host-test: PASS"; else cat "$OUT/out.txt"; echo "passkey-linux-host-test: FAIL"; fi
exit $failed
