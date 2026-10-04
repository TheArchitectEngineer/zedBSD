#!/bin/sh
# ws129-p002: host test of tools/release/license-inventory.py on the CI image's configuration.
#  1. The real table: the known open item only (the remacs decision; the missing notices G1-G4 were added 2026-10-04).
#  2. A table without the AX211 component: its ISC/BSD sources are reported uncovered.
#  3. A table where remacs is GPL without the decision mark: reported as a GPL component without a decision.
#  4. An empty root filesystem: every installed notice is reported missing on disk.
#  5. The index has one line per component, four fields each.
#   sh plan/ws129/tests/license-inventory-test.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0
config=config/ci/config-amd64.mk
run() { timeout 300 python3 tools/release/license-inventory.py --config "$config" "$@" 2> "$work/err" > "$work/out"; }
expect() {
	if grep -q -- "$1" "$work/err"; then echo "ok: $2"; else echo "FAIL: $2"; status=1; fi
}

# 1.
run --index "$work/index.txt"
open=$(grep -c "^license-inventory: \(missing notice\|decision pending\|uncovered\|unlisted\|GPL-family\)" "$work/err")
if [ "$open" = 1 ]; then echo "ok: the real table has the 1 known open item"; else echo "FAIL: the real table has $open open items"; status=1; fi
expect "decision pending: remacs" "the open item is remacs"
expect "decision pending: remacs" "remacs waits for the user's decision"

# 2.
python3 -c "
import json,sys
d=json.load(open('tools/release/license-components.json'))
d['components']=[c for c in d['components'] if c['id']!='intel-ax211-driver']
json.dump(d,open('$work/no-ax211.json','w'))"
run --components "$work/no-ax211.json"
expect "uncovered ISC AND BSD-3-Clause source: src/drivers/wifi/intel-ax211/" "sources without a component are reported"
expect "uncovered third-party text without SPDX: src/drivers/wifi/intel-ax211/" "third-party text without SPDX is reported"

# 3.
python3 -c "
import json
d=json.load(open('tools/release/license-components.json'))
for c in d['components']:
    if c['id']=='remacs': c.pop('status',None)
json.dump(d,open('$work/gpl.json','w'))"
run --components "$work/gpl.json"
expect "GPL-family component without a decision: remacs" "a GPL component needs the decision mark"

# 4.
mkdir -p "$work/rootfs"
run --rootfs "$work/rootfs"
expect "missing notice of openssl: /usr/share/licenses/openssl/LICENSE.txt" "notices absent on disk are reported"

# 5.
fields=$(awk -F '\t' 'NF != 4' "$work/index.txt" | wc -l)
lines=$(wc -l < "$work/index.txt")
if [ "$fields" = 0 ] && [ "$lines" -ge 20 ]; then echo "ok: the index has $lines lines of 4 fields"; else echo "FAIL: index lines=$lines bad=$fields"; status=1; fi

[ $status = 0 ] && echo "license-inventory-test: PASS" || echo "license-inventory-test: FAIL"
exit $status
