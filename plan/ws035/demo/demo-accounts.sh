#!/bin/sh
# ws035-p120: the accounts of the demonstration images (the 2026-09-28 user decision: the demonstration logs in as a
# named user).  Writes OUTDIR/passwd, OUTDIR/group and OUTDIR/shadow from the base system's (userland/base/etc/):
#   - the person kei, uid 1000, shown as "Kei" (the GECOS field the greeter and the Files Home greeting use), home
#     /home/kei (sessiond makes it at the first login: the image cannot give a directory to its owner), /bin/sh;
#   - kei logs in without a password (an empty shadow password takes only an empty one: Enter at the greeter and at
#     the lock screen).  sshd refuses empty passwords (PermitEmptyPasswords no), so this is the seat's login only;
#   - kei is a member of the group network (networkd's socket), which sessiond gives every session anyway (p104);
#   - root has a password (the 2026-09-28 user decision, for maintenance on the machine: the terminal's login and
#     SSH, PermitRootLogin yes), never an empty one: DEMO_ROOT_PASSWORD, or else a random one of 12 letters and
#     digits.  The password is written to OUTDIR/root-password (mode 0600, not in git) and printed; the image has its
#     SHA-512 crypt only (libc crypt.c: $6$).  The greeter offers kei only (root is offered only on a machine
#     without a person's account).  The Venus guest's harness still reaches root with its SSH key.
# The build scripts put the three files over the base ones (ZEDBSD_TEST_EXTRA_FILES comes after them):
#   plan/ws075/demo/build-demo-image.sh, plan/ws035/tests/build-demo-venus-image.sh
#
#   [DEMO_ROOT_PASSWORD=...] plan/ws035/demo/demo-accounts.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:?usage: demo-accounts.sh OUTDIR}
base=userland/base/etc
mkdir -p "$out"

# passwd: the base accounts and kei.
cp "$base/passwd" "$out/passwd.tmp"
printf '%s\n' 'kei:x:1000:1000:Kei:/home/kei:/bin/sh' >> "$out/passwd.tmp"

# group: kei in network, and kei's own group.
awk -F: 'BEGIN { OFS = ":" } $1 == "network" { if ($4 == "") $4 = "kei"; else $4 = $4 ",kei" } { print }' \
	"$base/group" > "$out/group.tmp"
printf '%s\n' 'kei:x:1000:' >> "$out/group.tmp"

# root's password: the given one, or a random one kept next to the files.
password=${DEMO_ROOT_PASSWORD:-}
if [ -z "$password" ]; then
	password=$(LC_ALL=C tr -dc 'A-HJ-NP-Za-km-z2-9' < /dev/urandom | head -c 12)
fi
(umask 077 && printf '%s\n' "$password" > "$out/root-password")
hash=$(printf '%s' "$password" | openssl passwd -6 -stdin)

# shadow: root with the password, kei without one.
awk -F: -v hash="$hash" 'BEGIN { OFS = ":" } $1 == "root" { $2 = hash } { print }' "$base/shadow" > "$out/shadow.tmp"
printf '%s\n' 'kei::0:0:99999:7:::' >> "$out/shadow.tmp"

# Each file must say what it is meant to.
grep -qx 'kei:x:1000:1000:Kei:/home/kei:/bin/sh' "$out/passwd.tmp"
grep -Eqx 'network:x:[0-9]+:(.*,)?kei' "$out/group.tmp"
grep -Eqx 'root:\$6\$[^:]+:.*' "$out/shadow.tmp"
grep -qx 'kei::0:0:99999:7:::' "$out/shadow.tmp"
for name in passwd group shadow; do
	mv -f "$out/$name.tmp" "$out/$name"
done
echo "demo accounts: root's password is in $out/root-password: $password"
