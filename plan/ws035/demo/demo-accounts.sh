#!/bin/sh
# ws035-p120: the accounts of the demonstration images (the 2026-09-28 user decision: the demonstration logs in as a
# named user).  Writes OUTDIR/passwd, OUTDIR/group and OUTDIR/shadow from the base system's (userland/base/etc/):
#   - the person kei, uid 1000, shown as "Kei" (the GECOS field the greeter and the Files Home greeting use), home
#     /home/kei (sessiond makes it at the first login: the image cannot give a directory to its owner), /bin/sh;
#   - kei logs in without a password (an empty shadow password takes only an empty one: Enter at the greeter and at
#     the lock screen).  sshd refuses empty passwords (PermitEmptyPasswords no), so this is the seat's login only;
#   - kei is a member of the group network (networkd's socket), which sessiond gives every session anyway (p104);
#   - root is locked ("*"): no empty-password root login.  The greeter offers kei only (root is offered only on a
#     machine without a person's account).  The Venus guest's harness still reaches root with its SSH key.
# The build scripts put the three files over the base ones (ZEDBSD_TEST_EXTRA_FILES comes after them):
#   plan/ws075/demo/build-demo-image.sh, plan/ws035/tests/build-demo-venus-image.sh
#
#   plan/ws035/demo/demo-accounts.sh OUTDIR
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

# shadow: root locked, kei without a password.
awk -F: 'BEGIN { OFS = ":" } $1 == "root" { $2 = "*" } { print }' "$base/shadow" > "$out/shadow.tmp"
printf '%s\n' 'kei::0:0:99999:7:::' >> "$out/shadow.tmp"

# Each file must say what it is meant to.
grep -qx 'kei:x:1000:1000:Kei:/home/kei:/bin/sh' "$out/passwd.tmp"
grep -Eqx 'network:x:[0-9]+:(.*,)?kei' "$out/group.tmp"
grep -qx 'root:\*:.*' "$out/shadow.tmp"
grep -qx 'kei::0:0:99999:7:::' "$out/shadow.tmp"
for name in passwd group shadow; do
	mv -f "$out/$name.tmp" "$out/$name"
done
