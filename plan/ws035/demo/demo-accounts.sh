#!/bin/sh
# ws035-p120: the accounts of the demonstration images.  Since 2026-09-29 (user decision) the base system's
# accounts (userland/base/etc/) are the demonstration's: root with the password "root", the person kei (uid 1000,
# shown as "Kei", home /home/kei made by sessiond at the first login, a member of network) with the password "kei",
# and the default image logs kei in by itself at boot (/etc/keiland/autologin, sessiond).  This script writes
# OUTDIR/passwd, OUTDIR/group and OUTDIR/shadow from them and checks that they say so, for the build scripts that
# put the three files over the base ones (ZEDBSD_TEST_EXTRA_FILES comes after them):
#   plan/ws075/demo/build-demo-image.sh, plan/ws035/tests/build-demo-venus-image.sh
#
#   plan/ws035/demo/demo-accounts.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:?usage: demo-accounts.sh OUTDIR}
base=userland/base/etc
mkdir -p "$out"
for name in passwd group shadow; do
	cp "$base/$name" "$out/$name.tmp"
done

# Each file must say what it is meant to.
grep -qx 'kei:x:1000:1000:Kei:/home/kei:/bin/sh' "$out/passwd.tmp"
grep -Eqx 'network:x:[0-9]+:(.*,)?kei(,.*)?' "$out/group.tmp"
grep -Eqx 'root:\$6\$[^:]+:.*' "$out/shadow.tmp"
grep -Eqx 'kei:\$6\$[^:]+:.*' "$out/shadow.tmp"
for name in passwd group shadow; do
	mv -f "$out/$name.tmp" "$out/$name"
done
echo "demo accounts: root (password root), kei (password kei)"
