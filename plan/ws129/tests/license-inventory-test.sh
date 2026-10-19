#!/bin/sh
# ws129-p002: host test of tools/release/license-inventory.py on the CI image's configuration.
#  1. The real table: no open item (G1-G4 added, remacs taken into the tree under zlib, 2026-10-04).
#  2. A table without the musl regex component: the TRE/musl regex sources (third-party copyright without
#     SPDX) are reported uncovered.  (Until ws218-p002 this case removed the AX211 driver component, and until
#     ws259-p001 the RTL8822B tables component; both drivers are now the project's own, Zlib.)
#  3. A table where OpenSSL were GPL without the decision mark: reported as a GPL component without a decision (it was
#     emacs until emacs left the CI image).
#  3b. ffmpeg "decided" without its decision's source: reported (ws122, 2026-10-05).
#  4. An empty root filesystem: every installed notice is reported missing on disk.
#  5. The index has one line per component, four fields each.
#   sh plan/ws129/tests/license-inventory-test.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
# The work directory stays in build/tmp (2026-10-06 user: deleting is Q1's step; plan/tools/q1-clean.sh removes it).
mkdir -p build/tmp
work=$(mktemp -d "$(pwd)/build/tmp/license-inventory.XXXXXX")
status=0
config=config/ci/config-amd64.mk
run() { timeout 300 python3 tools/release/license-inventory.py --config "$config" "$@" 2> "$work/err" > "$work/out"; }
expect() {
	if grep -q -- "$1" "$work/err"; then echo "ok: $2"; else echo "FAIL: $2"; status=1; fi
}

# 1.
run --index "$work/index.txt"
open=$(grep -c "^license-inventory: \(missing notice\|decision pending\|decided without\|uncovered\|unlisted\|GPL-family\)" "$work/err")
if [ "$open" = 0 ]; then echo "ok: the real table has no open item"; else echo "FAIL: the real table has $open open items"; status=1; fi
# The AX211 driver is the project's own since ws218-p002: no AX211 source may be reported as third-party.
if grep -q "src/drivers/wifi/intel-ax211/" "$work/err"; then echo "FAIL: an AX211 source is reported as third-party"; status=1; else echo "ok: the AX211 driver sources are the project's own"; fi
# The RTL8822B tables are the project's own since ws259-p001 (values referenced as data).
if grep -q "src/drivers/wifi/rtl8822b/" "$work/err"; then echo "FAIL: an RTL8822B source is reported as third-party"; status=1; else echo "ok: the RTL8822B driver sources are the project's own"; fi

# 2.
python3 -c "
import json,sys
d=json.load(open('tools/release/license-components.json'))
d['components']=[c for c in d['components'] if c['id'] != 'musl-regex']
json.dump(d,open('$work/no-components.json','w'))"
run --components "$work/no-components.json"
expect "uncovered third-party text without SPDX: src/libc/regex/" "third-party text without SPDX is reported"

# 3.
python3 -c "
import json
d=json.load(open('tools/release/license-components.json'))
for c in d['components']:
    if c['id']=='openssl': c['license']='GPL-3.0-or-later'
json.dump(d,open('$work/gpl.json','w'))"
run --components "$work/gpl.json"
expect "GPL-family component without a decision: openssl" "a GPL component needs the decision mark"

# 3b.
config=config/release/config-amd64-beta2.mk
python3 -c "
import json
d=json.load(open('tools/release/license-components.json'))
for c in d['components']:
    if c['id']=='ffmpeg': c.pop('decision')
json.dump(d,open('$work/undecided.json','w'))"
run --components "$work/undecided.json"
expect "decided without the decision's source: ffmpeg" "a decided component names its decision"
config=config/ci/config-amd64.mk

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
