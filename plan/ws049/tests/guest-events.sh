#!/bin/sh
# ws049-p007: checks that ACPI events reach the kernel in a QEMU guest (q35,
# OVMF, KVM): the SCI, the fixed power button and a GPE.
#   sh plan/ws049/tests/guest-events.sh IMAGE
# IMAGE is an SSH guest image (plan/tools/guest/build-ssh-image.sh or
# test-image.sh) whose kernel has CONFIG_DRIVER_ACPI.  The guest's console is
# not read: the kernel log is read with dmesg over SSH, and the events are
# raised through QMP.
#   1. sci:    the boot log has "acpi: SCI on IRQ N"
#   2. button: QMP system_powerdown (PM1 PWRBTN_STS) makes the kernel log
#              "acpi: power button" (the fixed event's handler)
#   3. gpe:    a hot-added CPU (QMP device_add on the slot
#              query-hotpluggable-cpus offers) raises GPE 2; \_GPE._E02 scans
#              the CPUs and notifies the new one, which the kernel logs as
#              "Notify(\_SB_.CPUS.Cxxx, 0x1)" (no driver takes it yet)
# Writes build/ws049/events/*.txt.  Exits 0 when all three pass.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
image=$1
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root" || exit 1
export GUEST_RUNTIME=${GUEST_RUNTIME:-build/ws049-events-run}
guest=plan/tools/guest/guest.py
qmp=plan/ws049/tests/qmp-send.py
out=build/ws049/events
rm -rf "$out"
mkdir -p "$out"
failed=0

# Boots the guest with room for two more CPUs.
python3 $guest stop > /dev/null 2>&1
python3 $guest start --disk nvme --qemu-extra "-smp 2,maxcpus=4" "$image" > /dev/null || exit 1
python3 $guest wait > /dev/null || { echo "FAIL no SSH"; python3 $guest stop > /dev/null; exit 1; }
monitor=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["monitor"])' "$GUEST_RUNTIME/session.json")

# 1. The SCI was taken at boot.
python3 $guest run 'dmesg' > "$out/boot.txt"
grep -i 'acpi' "$out/boot.txt" > "$out/boot-acpi.txt"
if grep -q 'acpi: SCI on IRQ' "$out/boot.txt"; then
	echo "sci: $(grep -m1 'acpi: SCI on IRQ' "$out/boot.txt")"
else
	echo "sci: FAILED, see $out/boot-acpi.txt"
	failed=1
fi

# 2. The fixed power button.
python3 $qmp "$monitor" system_powerdown > "$out/qmp-powerdown.txt"
sleep 3
python3 $guest run 'dmesg' > "$out/after-button.txt"
if grep -q 'acpi: power button' "$out/after-button.txt"; then
	echo "button: the kernel logged the power button"
else
	echo "button: FAILED, see $out/after-button.txt"
	failed=1
fi

# 3. A GPE: hot-adds a CPU in the first slot no CPU fills.
python3 $qmp "$monitor" query-hotpluggable-cpus > "$out/qmp-cpus.txt"
properties=$(python3 - "$out/qmp-cpus.txt" <<'PY'
import json, sys
answer = json.load(open(sys.argv[1]))
for slot in answer.get("return", []):
	if "qom-path" not in slot:
		arguments = {"driver": slot["type"], "id": "hotcpu"}
		arguments.update(slot["props"])
		print(json.dumps(arguments))
		break
PY
)
if [ -n "$properties" ]; then
	python3 $qmp "$monitor" device_add "$properties" > "$out/qmp-device-add.txt"
	sleep 3
	python3 $guest run 'dmesg' > "$out/after-gpe.txt"
	if grep -q 'Notify(\\_SB_\.CPUS\.C[0-9A-F]*, 0x1)' "$out/after-gpe.txt"; then
		echo "gpe: $(grep -m1 'Notify(\\_SB_\.CPUS' "$out/after-gpe.txt")"
	else
		echo "gpe: FAILED, see $out/after-gpe.txt and $out/qmp-device-add.txt"
		failed=1
	fi
else
	echo "gpe: FAILED, no free CPU slot, see $out/qmp-cpus.txt"
	failed=1
fi

python3 $guest stop > /dev/null
exit $failed
