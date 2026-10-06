#!/bin/sh
# ws168-p002: the kernel's sandbox_spawn on a running zedBSD guest of plan/ws168/tests/config-amd64-sandbox.mk.
#  1. sandboxtest as root: the request's checks, the child's state (only its files, the limits, the default signals), each
#     call outside the set EPERM with DENY_ERRNO, the allowed calls (and threads with the allow bit), the refused memory,
#     and without DENY_ERRNO a call outside the set ending the child with SIGKILL while a child that keeps to the set
#     (libc's start included) runs ("SANDBOX PASS").
#  2. The same as a user who is not root (kei, else nobody): "SANDBOX PASS" (the sandbox needs no privilege).
#  3. The kernel's log has the "SANDBOX deny" lines of the denied calls (at most 8 a child), read over SSH (dmesg).
# PASS: every "ok" line and the last line sandbox-p002: PASS.
#
#   plan/tools/guest/test-image.sh plan/ws168/tests/config-amd64-sandbox.mk BUILD; plan/tools/files/files-guest.sh start IMAGE
#   plan/ws168/tests/sandbox-p002.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws168-p002}
mkdir -p "$out"
guest() { timeout 180 python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
status=0

# 1. As root.
guest '/bin/sandboxtest' > "$out/root.txt"
cat "$out/root.txt"
grep -q '^SANDBOX PASS' "$out/root.txt" && echo "ok: sandboxtest as root" || { echo "FAIL: sandboxtest as root"; status=1; }

# 2. As another user.
user=$(guest 'grep -q "^kei:" /etc/passwd && echo kei || echo nobody' | tail -1)
guest "cd /tmp; runas $user /bin/sandboxtest 2>&1; true" > "$out/user.txt"
grep -q '^SANDBOX PASS' "$out/user.txt" && echo "ok: sandboxtest as $user" || { echo "FAIL: sandboxtest as $user"; grep -E 'FAIL' "$out/user.txt" | head -20; status=1; }

# 3. The kernel's lines of the denied calls.
guest 'dmesg 2>/dev/null | grep "SANDBOX deny" | tail -40' > "$out/klog.txt"
grep -q 'SANDBOX deny' "$out/klog.txt" && echo "ok: the kernel logs the denied calls" || { echo "FAIL: no SANDBOX deny line"; status=1; }

[ $status = 0 ] && echo "sandbox-p002: PASS" || echo "sandbox-p002: FAIL"
exit $status
