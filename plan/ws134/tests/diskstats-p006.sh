#!/bin/sh
# ws134-p006: the kernel's per-disk statistics (sysctl hw.diskstats) on a running zedBSD guest (an image of this
# tree's kernel with sysctl and dd, for example the System Monitor's: plan/ws134/tests/build-monitor-image.sh BUILD,
# then plan/tools/files/files-guest.sh start BUILD/hdd-image.img; the boot disk is NVMe).  Over SSH and QMP, never
# the console:
#  1. The header and the boot disk: nvme0n1 of kind 2 (NVMe), the set's generation 1 or more, no partition listed.
#  2. 32 MiB read from the raw disk away from its start: its read ops and bytes grow (8 MiB or more), its read time
#     and busy time grow, its mean read latency is between 1 us and 1 s; its writes may grow a little (the system).
#  3. 32 MiB written to a file and synced: its write bytes grow by 8 MiB or more.
#  4. A USB stick plugged in through QMP (a 16 MiB file): a new disk of kind 3 (USB) or 4 (UAS) with another id and
#     the set's generation higher; 1 MiB read from it counts on it and not on the NVMe disk's reads beyond the
#     system's; unplugged: gone, the generation higher again.
#
#   plan/ws134/tests/diskstats-p006.sh [OUTDIR]
# Prints "diskstats-p006: PASS" or "diskstats-p006: FAIL".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws134-p006}
mkdir -p "$out"
out=$(cd "$out" && pwd)
status=0
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }

# Sends QMP commands (JSON objects, one an argument) and prints each answer.
qmp() {
	python3 - "$GUEST_RUNTIME/qmp.sock" "$@" <<'EOF'
import json, socket, sys, time
sock = socket.socket(socket.AF_UNIX)
sock.connect(sys.argv[1])
reader = sock.makefile("r")
reader.readline()
def call(command):
    sock.sendall((json.dumps(command) + "\n").encode())
    while True:
        line = reader.readline()
        if not line:
            return None
        answer = json.loads(line)
        if "return" in answer or "error" in answer:
            return answer
call({"execute": "qmp_capabilities"})
for text in sys.argv[2:]:
    print(json.dumps(call(json.loads(text))))
    time.sleep(0.2)
EOF
}

# Judges readings of hw.diskstats (files in OUTDIR) with a Python snippet that has read(name) giving
# (generation, {disk name: fields}).
judge() {
	python3 - "$out" "$1" <<'EOF' || status=1
import sys
directory, code = sys.argv[1], sys.argv[2]
ok = True
def expect(condition, what):
    global ok
    print(("ok: " if condition else "FAILED: ") + what)
    ok = ok and condition
def read(name):
    generation = None
    disks = {}
    for line in open(directory + "/" + name):
        if not line.startswith("hw.diskstats:"):
            continue
        fields = dict(f.split("=", 1) for f in line.split()[1:] if "=" in f)
        if "disks" in fields:
            generation = int(fields["generation"])
        elif "name" in fields:
            disks[fields["name"]] = {k: (v if k == "name" else int(v, 0)) for k, v in fields.items()}
    return generation, disks
exec(code)
sys.exit(0 if ok else 1)
EOF
}

# 1. The header and the boot disk.
guest 'sysctl hw.diskstats' > "$out/start.txt"
judge '
generation, disks = read("start.txt")
expect(generation is not None and generation >= 1, "the set has a generation: %s" % generation)
expect("nvme0n1" in disks and disks["nvme0n1"]["kind"] == 2, "the boot disk nvme0n1 is listed as NVMe")
expect(not any(name.startswith("nvme0n1p") for name in disks), "no partition is listed")
expect(all(d["read_ops"] > 0 for d in disks.values() if d["kind"] == 2), "the boot disk has read (the boot)")
'

# 2. A raw read away from the start (past the cache's likely pages).
guest 'sysctl hw.diskstats; dd if=/dev/nvme0n1 of=/dev/null bs=65536 skip=9000 count=512 2>&1; sysctl hw.diskstats' > "$out/read.txt"
grep '^hw.diskstats' "$out/read.txt" | head -n "$(($(grep -c '^hw.diskstats' "$out/read.txt") / 2))" > "$out/read-before.txt"
grep '^hw.diskstats' "$out/read.txt" | tail -n "$(($(grep -c '^hw.diskstats' "$out/read.txt") / 2))" > "$out/read-after.txt"
judge '
_, a = read("read-before.txt")
_, b = read("read-after.txt")
x, y = a.get("nvme0n1"), b.get("nvme0n1")
expect(x is not None and y is not None, "nvme0n1 in both readings")
if x and y:
    expect(y["read_ops"] > x["read_ops"], "read ops grew: %d" % (y["read_ops"] - x["read_ops"]))
    grown = y["read_bytes"] - x["read_bytes"]
    expect(grown >= 8 << 20, "read bytes grew by 8 MiB or more: %d" % grown)
    expect(y["read_ns"] > x["read_ns"], "read time grew")
    expect(y["busy_ns"] > x["busy_ns"], "busy time grew")
    ops = y["read_ops"] - x["read_ops"]
    if ops:
        mean = (y["read_ns"] - x["read_ns"]) / ops
        expect(1e3 <= mean <= 1e9, "mean read latency %.0f ns" % mean)
    expect(all(y[k] >= x[k] for k in ("read_ops", "write_ops", "read_bytes", "write_bytes", "read_ns", "write_ns", "busy_ns")), "every count only grows")
'

# 3. A file written and synced.
guest 'sysctl hw.diskstats > /tmp/p006-a.txt; dd if=/dev/zero of=/root/p006.bin bs=65536 count=512 2>&1; sync; sleep 1; sysctl hw.diskstats > /tmp/p006-b.txt; rm -f /root/p006.bin; sync' > "$out/write.log"
guest 'cat /tmp/p006-a.txt' > "$out/write-before.txt"
guest 'cat /tmp/p006-b.txt' > "$out/write-after.txt"
judge '
_, a = read("write-before.txt")
_, b = read("write-after.txt")
x, y = a.get("nvme0n1"), b.get("nvme0n1")
expect(x is not None and y is not None, "nvme0n1 in both readings")
if x and y:
    grown = y["write_bytes"] - x["write_bytes"]
    expect(grown >= 8 << 20, "write bytes grew by 8 MiB or more: %d" % grown)
    expect(y["write_ops"] > x["write_ops"] and y["write_ns"] > x["write_ns"], "write ops and time grew")
'

# 4. A USB stick in and out.
truncate -s 16M "$out/stick.img"
guest 'sysctl hw.diskstats' > "$out/usb-before.txt"
qmp "{\"execute\": \"blockdev-add\", \"arguments\": {\"driver\": \"raw\", \"node-name\": \"p006usb\", \"file\": {\"driver\": \"file\", \"filename\": \"$out/stick.img\"}}}" \
    '{"execute": "device_add", "arguments": {"driver": "usb-storage", "id": "p006stick", "drive": "p006usb", "bus": "xhci.0"}}' > "$out/qmp-add.txt"
sleep 6
guest 'sysctl hw.diskstats' > "$out/usb-in.txt"
stick=$(sed -n 's/^hw.diskstats: name=\([^ ]*\) kind=[34] .*/\1/p' "$out/usb-in.txt" | head -1)
echo "stick: ${stick:-none}"
guest "sysctl hw.diskstats > /tmp/p006-c.txt; dd if=/dev/${stick:-none} of=/dev/null bs=65536 count=16 2>&1; sysctl hw.diskstats > /tmp/p006-d.txt" > "$out/usb-read.log"
guest 'cat /tmp/p006-c.txt' > "$out/usb-read-before.txt"
guest 'cat /tmp/p006-d.txt' > "$out/usb-read-after.txt"
qmp '{"execute": "device_del", "arguments": {"id": "p006stick"}}' > "$out/qmp-del.txt"
sleep 6
qmp '{"execute": "blockdev-del", "arguments": {"node-name": "p006usb"}}' >> "$out/qmp-del.txt"
guest 'sysctl hw.diskstats' > "$out/usb-out.txt"
judge "
stick = '${stick:-}'
g0, d0 = read('usb-before.txt')
g1, d1 = read('usb-in.txt')
g2, d2 = read('usb-out.txt')
expect(stick != '' and stick not in d0, 'a new disk %s appeared' % stick)
if stick and stick in d1:
    expect(d1[stick]['kind'] in (3, 4), 'it is a USB disk (kind %d)' % d1[stick]['kind'])
    expect(all(d1[stick]['id'] != d['id'] for d in d0.values()), 'with an id of its own')
    expect(g1 is not None and g0 is not None and g1 > g0, 'the generation rose: %s to %s' % (g0, g1))
    _, a = read('usb-read-before.txt')
    _, b = read('usb-read-after.txt')
    if stick in a and stick in b:
        expect(b[stick]['read_bytes'] - a[stick]['read_bytes'] >= 1 << 20, 'the read counted on the stick')
expect(stick not in d2, 'unplugged, it is gone')
expect(g2 is not None and g1 is not None and g2 > g1, 'the generation rose again: %s to %s' % (g1, g2))
"

[ $status -eq 0 ] && echo "diskstats-p006: PASS" || echo "diskstats-p006: FAIL"
exit $status
