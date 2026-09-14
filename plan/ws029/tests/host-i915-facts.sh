#!/bin/sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
# Read-only inventory of the WS029 test host GPU/IOMMU/VFIO state. Runs over SSH on the host; changes nothing.
set -eu
host=${1:-awe@10.0.10.25}
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=${2:-$repo/plan/ws029/phase001/host-facts.json}
mkdir -p "$(dirname "$out")"
ssh -o BatchMode=yes "$host" 'python3 - <<"PY"
import json, os, re, subprocess
def run(*argv):
    try:
        return subprocess.run(argv, capture_output=True, text=True, timeout=30).stdout
    except Exception as error:
        return "ERROR %s" % error
def read(path):
    try:
        return open(path).read().strip()
    except Exception as error:
        return "ERROR %s" % error
facts = {
  "product": run("sudo", "-n", "dmidecode", "-s", "system-product-name").strip(),
  "bios": run("sudo", "-n", "dmidecode", "-s", "bios-version").strip(),
  "kernel": run("uname", "-r").strip(),
  "cmdline": read("/proc/cmdline"),
  "cpu": run("sh", "-c", "grep -m1 \"model name\" /proc/cpuinfo").strip(),
  "gpu_lspci": run("sudo", "-n", "lspci", "-nnk", "-s", "00:02.0"),
  "gpu_lspci_vvv": run("sudo", "-n", "lspci", "-vvv", "-s", "00:02.0"),
  "gpu_resource": read("/sys/bus/pci/devices/0000:00:02.0/resource"),
  "gpu_driver": os.path.basename(os.readlink("/sys/bus/pci/devices/0000:00:02.0/driver")) if os.path.islink("/sys/bus/pci/devices/0000:00:02.0/driver") else None,
  "gpu_driver_override": read("/sys/bus/pci/devices/0000:00:02.0/driver_override"),
  "iommu_group": os.path.basename(os.readlink("/sys/bus/pci/devices/0000:00:02.0/iommu_group")),
  "iommu_group_devices": sorted(os.listdir("/sys/bus/pci/devices/0000:00:02.0/iommu_group/devices")),
  "iommu_groups_total": len(os.listdir("/sys/kernel/iommu_groups")),
  "vfio_modprobe_dry": run("sudo", "-n", "modprobe", "-n", "-v", "vfio-pci"),
  "vfio_bound_devices": sorted(os.listdir("/sys/bus/pci/drivers/vfio-pci")) if os.path.isdir("/sys/bus/pci/drivers/vfio-pci") else [],
  "dev_vfio": sorted(os.listdir("/dev/vfio")) if os.path.isdir("/dev/vfio") else [],
  "kconfig": run("sh", "-c", "grep -E \"^CONFIG_(VFIO|VFIO_PCI|VFIO_PCI_IGD|VFIO_IOMMU_TYPE1|KVM_INTEL|INTEL_IOMMU_DEFAULT_ON)=\" /boot/config-$(uname -r)").split(),
  "i915_capabilities": run("sudo", "-n", "sh", "-c", "cat /sys/kernel/debug/dri/0/i915_capabilities 2>/dev/null | head -120"),
  "drm_engines": sorted(os.listdir("/sys/class/drm/card0/engine")) if os.path.isdir("/sys/class/drm/card0/engine") else [],
  "drm_nodes": sorted(os.listdir("/sys/class/drm")),
  "gdm": run("systemctl", "is-active", "gdm").strip(),
  "sessions": run("loginctl", "list-sessions", "--no-legend"),
  "qemu": run("qemu-system-x86_64", "-version").split("\n")[0],
  "qemu_vfio_devices": [l.strip() for l in run("sh", "-c", "qemu-system-x86_64 -device help | grep -i vfio").splitlines()],
  "firmware_i915_adlp": sorted(f for f in os.listdir("/lib/firmware/i915") if f.startswith("adlp")) if os.path.isdir("/lib/firmware/i915") else [],
  "memory_gib": round(int(re.search(r"MemTotal:\s+(\d+)", read("/proc/meminfo")).group(1)) / 1048576, 1),
  "nproc": os.cpu_count(),
  "dmesg_dmar": run("sudo", "-n", "sh", "-c", "dmesg | grep -i -m6 \"DMAR\\|IOMMU\""),
}
print(json.dumps(facts, ensure_ascii=False, indent=2))
PY' > "$out"
python3 -c "
import json,sys
f=json.load(open(sys.argv[1]))
print('product', f['product'], '| kernel', f['kernel'], '| driver', f['gpu_driver'], '| group', f['iommu_group'], f['iommu_group_devices'])
print('gdm', f['gdm'], '| vfio bound', f['vfio_bound_devices'], '| dev/vfio', f['dev_vfio'], '| kconfig', f['kconfig'])
print('engines', f['drm_engines'], '| adlp firmware', f['firmware_i915_adlp'])
" "$out"
