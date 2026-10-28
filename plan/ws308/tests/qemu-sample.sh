#!/bin/sh
# ws308: a sampling profile of a guest load through the gdbstub.  Boots IMAGE (8 CPUs, KVM, a blank 2 GiB NVMe disk),
# runs SETUP and then LOAD in the background on the guest's serial line, and while the load runs halts the guest
# SAMPLES times through the gdbstub, each time taking every CPU's innermost frames (`bt 4`), resolved with VMUNIX's
# symbols.  Prints the load's own output and a table of the innermost kernel functions (and of the callers of the top
# ones) by how many CPU samples they had; idle CPUs count as sched_idle.
#   sh plan/ws308/tests/qemu-sample.sh IMAGE VMUNIX 'SETUP' 'LOAD' [SAMPLES]
# IMAGE: plan/tools/posix/config-amd64-serial.mk with randread in /root (ZEDBSD_EXTRA_FILES).  W is
# build/tmp/qemu-sample (a fresh run each time).  GDB_PORT (22212) and the QEMU name p1-sample are this run's.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
. plan/tools/fresh-out.sh
IMAGE=${1:?image}
VMUNIX=${2:?vmunix}
SETUP=${3:?setup}
LOAD=${4:?load}
SAMPLES=${5:-60}
GDB_PORT=${GDB_PORT:-22212}
fresh_out build/tmp/qemu-sample
W=build/tmp/qemu-sample
truncate -s 2G "$W/nvme.img"
cp /usr/share/OVMF/OVMF_VARS_4M.fd "$W/vars.fd"
timeout 1800 qemu-system-x86_64 -name p1-sample -machine q35 -m 4G -smp 8 -accel kvm -cpu host \
  -drive "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd" \
  -drive "if=pflash,format=raw,file=$W/vars.fd" \
  -drive "if=none,id=boot,file=$IMAGE,format=raw,snapshot=on" \
  -device nvme,serial=zedbsd-boot,drive=boot,bootindex=1 \
  -drive "if=none,id=d1,file=$W/nvme.img,format=raw,cache=unsafe" \
  -device nvme,serial=zedbsd-probe,drive=d1 \
  -vga std -display none -no-reboot -gdb "tcp:127.0.0.1:$GDB_PORT" \
  -serial "unix:$W/serial.sock,server=on,wait=off" > "$W/qemu.log" 2>&1 &
pid=$!
trap 'kill $pid 2> /dev/null' EXIT
i=0
while [ ! -S "$W/serial.sock" ] && [ $i -lt 600 ]; do sleep 0.1; i=$((i + 1)); done
S="python3 plan/tools/guest/serial.py --socket $W/serial.sock --timeout 600"
$S expect 'login: ' > /dev/null || { echo "qemu-sample: FAIL no login"; exit 1; }
$S login > /dev/null
$S run "$SETUP" > /dev/null
$S run "($LOAD) > /tmp/load.txt 2>&1 &" > /dev/null
sleep 3
: > "$W/samples.txt"
n=0
while [ "$n" -lt "$SAMPLES" ]; do
	timeout 30 gdb -batch -nx -ex "file $VMUNIX" -ex "target remote 127.0.0.1:$GDB_PORT" \
	    -ex 'thread apply all bt 4' >> "$W/samples.txt" 2>&1
	n=$((n + 1))
	sleep 0.2
done
i=0
until $S run 'cat /tmp/load.txt' 2> /dev/null | grep -q randread || [ $i -ge 60 ]; do sleep 2; i=$((i + 1)); done
$S run 'cat /tmp/load.txt'
python3 -I - "$W/samples.txt" <<'PY'
import collections, re, sys
leaf = collections.Counter()
pair = collections.Counter()
frames = []
def flush():
    if frames:
        leaf[frames[0]] += 1
        if len(frames) > 1:
            pair[frames[0] + " <- " + frames[1]] += 1
for line in open(sys.argv[1], errors="replace"):
    if line.startswith("Thread "):
        flush()
        frames = []
        continue
    m = re.match(r"#\d+\s+(?:0x[0-9a-f]+ in )?([A-Za-z_][A-Za-z0-9_.]*) \(", line)
    if m:
        frames.append(m.group(1))
flush()
total = sum(leaf.values())
print("qemu-sample: %d CPU samples" % total)
for name, count in leaf.most_common(25):
    print("%6d %5.1f%% %s" % (count, 100.0 * count / total, name))
print("callers:")
for name, count in pair.most_common(25):
    print("%6d %5.1f%% %s" % (count, 100.0 * count / total, name))
PY
