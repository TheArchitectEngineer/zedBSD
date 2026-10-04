#!/bin/sh
# ws160-p002: the Users page of Settings on the host (plan/ws089/tests/host-build.sh's settings-render, its stand-in
# kl_system answering a password change with HOST_ACCOUNT_RESULT).  Checks:
#   no account offered: the page says so and has no password field;
#   the change asked with the three fields (the stand-in sees the lengths 3 and 10, never the text), answered 0;
#   the answers EPERM (1), EINVAL (22) and ENOTSUP (95) reach the page;
#   the new password and its repeat differing: Enter asks nothing.
# Pictures in build/ws160-p002 (users-*.png when python3's PIL is there).
#   sh plan/ws160/tests/run-host-users.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=build/ws160-p002
mkdir -p "$out"
sh plan/ws089/tests/host-build.sh >/dev/null || { echo "host-build: FAILED"; exit 1; }
render=build/ws089-host/settings-render
status=0
checks=0
expect() { checks=$((checks + 1)); if printf '%s\n' "$3" | grep -Eq "$2"; then :; else echo "FAIL: $1"; status=1; fi; }
refuse() { checks=$((checks + 1)); if printf '%s\n' "$3" | grep -Eq "$2"; then echo "FAIL: $1"; status=1; fi; }
typed='text=kei key=15 text=newpass123 key=15 text=newpass123'

result=$(timeout 60 $render --page=users draw=$out/users-noaccount.ppm hits 2>&1)
expect "no account: the page shows" 'ZSETTINGS USERS account name=' "$result"
refuse "no account: no control" 'ZSETTINGS (LAYOUT|CONTROL)' "$result"

result=$(HOST_ACCOUNT_RESULT=0 timeout 60 $render --page=users $typed draw=$out/users-typed.ppm key=28 draw=$out/x.ppm draw=$out/users-changed.ppm 2>&1)
expect "the change is asked" 'ZSETTINGS USERS change request=1' "$result"
expect "the stand-in sees the lengths only" 'HOSTACCOUNT set-password request=1 current_length=3 new_length=10' "$result"
refuse "no password in the output" 'newpass123' "$(printf '%s\n' "$result" | grep -v '^HIT')"
expect "changed" 'ZSETTINGS USERS result request=1 errno=0' "$result"

for answer in 1 22 95; do
	result=$(HOST_ACCOUNT_RESULT=$answer timeout 60 $render --page=users $typed key=28 draw=$out/x.ppm draw=$out/users-errno$answer.ppm 2>&1)
	expect "answer $answer reaches the page" "ZSETTINGS USERS result request=1 errno=$answer" "$result"
done

result=$(HOST_ACCOUNT_RESULT=0 timeout 60 $render --page=users text=kei key=15 text=newpass123 key=15 text=newpass12 key=28 draw=$out/users-mismatch.ppm 2>&1)
refuse "differing new passwords ask nothing" 'HOSTACCOUNT set-password' "$result"

# The pictures, when PIL is there.
python3 - "$out" <<'PY' 2>/dev/null || true
import glob, sys
from PIL import Image
for path in glob.glob(sys.argv[1] + "/users-*.ppm"):
    Image.open(path).save(path[:-4] + ".png")
PY
rm -f "$out/x.ppm"

if [ $status -eq 0 ]; then echo "host-users: $checks checks passed"; else echo "host-users: FAILED"; fi
exit $status
