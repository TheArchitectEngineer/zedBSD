#!/usr/bin/python3 -I
"""ws305: the GPU passthrough VM on the desktop host, the host's half (run as root on 10.0.10.3 by vfio.sh).

The host is Debian booted with the "Debian VFIO" grub entry (41_zedbsd_vfio: amdgpu not loaded, the firmware's
framebuffer not taken, the IOMMU in pass-through), so the GPU has no driver until this binds it to vfio-pci.

    vfio-host.py status              the boot, the GPU's functions and their drivers, the VM (running or not)
    vfio-host.py bind                every function of the GPU's slot to vfio-pci
    vfio-host.py start [--rom FILE]  the VM: a fresh copy of the image and of the UEFI variables, QEMU daemonized
    vfio-host.py stop                QMP quit, SIGKILL when QEMU has not ended within 10 s
    vfio-host.py qmp COMMAND [JSON]  one QMP command and its answer
    vfio-host.py reset-method [METHOD]
                                     the GPU's reset_method shown, or set ("" stops the host's resets, "default")
    vfio-host.py rom FILE            the GPU's video BIOS read through sysfs into FILE (the GPU on vfio-pci)

The files are in RUN (default /home/awe/ws305): hdd-image.img (pushed by vfio.sh), run/disk.img, run/vars.fd,
run/qmp.sock, run/qemu.pid, run/qemu.log (QEMU's own messages, never the guest's console).  The guest's SSH
is forwarded from 127.0.0.1:SSH_PORT and gdbstub listens on 127.0.0.1:GDB_PORT, both on the host only.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""

import argparse
import json
import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

RUN = Path(os.environ.get("WS305_RUN", "/home/awe/ws305"))
SSH_PORT = int(os.environ.get("WS305_SSH_PORT", "10322"))
GDB_PORT = int(os.environ.get("WS305_GDB_PORT", "10323"))
OVMF_CODE = "/usr/share/OVMF/OVMF_CODE_4M.fd"
OVMF_VARS = "/usr/share/OVMF/OVMF_VARS_4M.fd"
PCI = Path("/sys/bus/pci/devices")
# The display classes: VGA (0x0300) and 3D (0x0302) controllers of AMD and NVIDIA.
GPU_VENDORS = ("0x1002", "0x10de")
GPU_CLASSES = ("0x030000", "0x030200")


def fail(message: str) -> None:
	print(f"vfio-host: {message}", file=sys.stderr)
	sys.exit(1)


def read(path: Path) -> str:
	try:
		return path.read_text().strip()
	except OSError:
		return ""


def gpu_slot() -> str:
	"""The slot (domain:bus:device) of the discrete GPU, or WS305_SLOT."""
	slot = os.environ.get("WS305_SLOT", "")
	if slot:
		return slot
	for device in sorted(PCI.iterdir()):
		if read(device / "vendor") in GPU_VENDORS and read(device / "class") in GPU_CLASSES:
			return device.name.rsplit(".", 1)[0]
	fail("no AMD or NVIDIA display controller")
	return ""


def functions(slot: str) -> list:
	return sorted(device for device in PCI.iterdir() if device.name.rsplit(".", 1)[0] == slot)


def driver(device: Path) -> str:
	link = device / "driver"
	return os.path.basename(os.readlink(link)) if link.is_symlink() else "-"


def group(device: Path) -> str:
	link = device / "iommu_group"
	return os.path.basename(os.readlink(link)) if link.is_symlink() else "-"


def qemu_pid() -> int:
	text = read(RUN / "run" / "qemu.pid")
	if not text:
		return 0
	pid = int(text)
	try:
		os.kill(pid, 0)
	except OSError:
		return 0
	return pid


def status(arguments: argparse.Namespace) -> int:
	print("cmdline", read(Path("/proc/cmdline")))
	slot = gpu_slot()
	for device in functions(slot):
		members = sorted(entry.name for entry in (device / "iommu_group" / "devices").iterdir()) \
			if (device / "iommu_group").exists() else []
		print("function", device.name, read(device / "vendor"), read(device / "device"),
		      "driver", driver(device), "group", group(device), "members", ",".join(members),
		      "reset_method", read(device / "reset_method") or "-")
	pid = qemu_pid()
	print("vm", f"running pid {pid}" if pid else "stopped", "ssh", f"127.0.0.1:{SSH_PORT}", "gdb", f"127.0.0.1:{GDB_PORT}")
	return 0


def bind(arguments: argparse.Namespace) -> int:
	subprocess.run(["modprobe", "vfio-pci"], check=True)
	slot = gpu_slot()
	for device in functions(slot):
		(device / "driver_override").write_text("vfio-pci")
		current = driver(device)
		if current not in ("-", "vfio-pci"):
			(device / "driver" / "unbind").write_text(device.name)
		if driver(device) != "vfio-pci":
			Path("/sys/bus/pci/drivers_probe").write_text(device.name)
	bad = [device.name for device in functions(slot) if driver(device) != "vfio-pci"]
	if bad:
		fail("not on vfio-pci: " + " ".join(bad))
	# Every member of each function's group must be on vfio-pci or have no driver (bridges are left alone).
	for device in functions(slot):
		for member in (device / "iommu_group" / "devices").iterdir():
			kind = read(member / "class")
			if not kind.startswith("0x0604") and driver(member) not in ("-", "vfio-pci"):
				fail(f"{member.name} shares the group and is on {driver(member)}")
	print("bound", " ".join(device.name for device in functions(slot)))
	return 0


def start(arguments: argparse.Namespace) -> int:
	if qemu_pid():
		fail("the VM runs already")
	slot = gpu_slot()
	devices = functions(slot)
	if any(driver(device) != "vfio-pci" for device in devices):
		bind(arguments)
	image = RUN / "hdd-image.img"
	if not image.is_file():
		fail(f"no image {image}")
	run = RUN / "run"
	run.mkdir(parents=True, exist_ok=True)
	subprocess.run(["cp", "--reflink=auto", str(image), str(run / "disk.img")], check=True)
	subprocess.run(["cp", OVMF_VARS, str(run / "vars.fd")], check=True)
	gpu = []
	for index, device in enumerate(devices):
		function = device.name.rsplit(".", 1)[1]
		option = f"vfio-pci,host={device.name},bus=rp1,addr=00.{function}"
		if index == 0:
			option += ",multifunction=on"
			if arguments.rom:
				option += f",romfile={arguments.rom}"
		gpu += ["-device", option]
	command = [
		"qemu-system-x86_64", "-accel", "kvm", "-machine", "q35", "-cpu", "host",
		"-smp", str(arguments.cpus), "-m", arguments.memory,
		"-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_CODE}",
		"-drive", f"if=pflash,format=raw,file={run / 'vars.fd'}",
		"-device", "pcie-root-port,id=rp1,chassis=1,slot=1",
		*gpu,
		"-device", "qemu-xhci,id=xhci,p2=8,p3=8",
		"-drive", f"if=none,id=boot,file={run / 'disk.img'},format=raw",
		"-device", "nvme,serial=zedbsd-boot,drive=boot,bootindex=1",
		"-netdev", "user,id=net0,net=10.0.2.0/24,host=10.0.2.2,dhcpstart=10.0.2.15,dns=10.0.2.3,"
			   f"hostfwd=tcp:127.0.0.1:{SSH_PORT}-:22",
		"-device", "usb-net,bus=xhci.0,port=2,netdev=net0,mac=52:54:00:33:05:01,msos-desc=on",
		"-device", "usb-kbd,bus=xhci.0,port=3",
		"-vga", "none", "-display", "none",
		"-qmp", f"unix:{run / 'qmp.sock'},server,nowait",
		"-serial", f"unix:{run / 'serial.sock'},server,nowait",
		"-gdb", f"tcp:127.0.0.1:{GDB_PORT}",
		"-pidfile", str(run / "qemu.pid"), "-daemonize",
	]
	with open(run / "qemu.log", "wb") as log:
		result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=60)
	if result.returncode != 0:
		fail(f"qemu exited {result.returncode}, see {run / 'qemu.log'}")
	print("started pid", read(run / "qemu.pid"))
	return 0


def qmp_call(command: str, args=None, timeout: float = 10.0):
	path = RUN / "run" / "qmp.sock"
	connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	connection.settimeout(timeout)
	connection.connect(str(path))
	stream = connection.makefile("rw")
	stream.readline()
	stream.write(json.dumps({"execute": "qmp_capabilities"}) + "\n")
	stream.flush()
	stream.readline()
	request = {"execute": command}
	if args:
		request["arguments"] = args
	stream.write(json.dumps(request) + "\n")
	stream.flush()
	while True:
		line = stream.readline()
		if not line:
			return None
		answer = json.loads(line)
		if "return" in answer or "error" in answer:
			connection.close()
			return answer


def qmp(arguments: argparse.Namespace) -> int:
	args = json.loads(arguments.json) if arguments.json else None
	print(json.dumps(qmp_call(arguments.command, args)))
	return 0


def stop(arguments: argparse.Namespace) -> int:
	pid = qemu_pid()
	if not pid:
		print("stopped already")
		return 0
	try:
		qmp_call("quit", timeout=5.0)
	except (OSError, ValueError):
		pass
	deadline = time.monotonic() + 10.0
	while time.monotonic() < deadline and qemu_pid():
		time.sleep(0.5)
	if qemu_pid():
		os.kill(pid, signal.SIGKILL)
		deadline = time.monotonic() + 10.0
		while time.monotonic() < deadline and qemu_pid():
			time.sleep(0.5)
		print("killed", pid)
	else:
		print("quit", pid)
	return 0 if not qemu_pid() else 1


def reset_method(arguments: argparse.Namespace) -> int:
	device = functions(gpu_slot())[0]
	if arguments.method is not None:
		(device / "reset_method").write_text(arguments.method + "\n")
	print(device.name, "reset_method", read(device / "reset_method") or "(none)")
	return 0


def rom(arguments: argparse.Namespace) -> int:
	device = functions(gpu_slot())[0]
	(device / "rom").write_text("1")
	try:
		data = (device / "rom").read_bytes()
	finally:
		(device / "rom").write_text("0")
	if len(data) < 512 or data[:2] != b"\x55\xaa":
		fail(f"no ROM image ({len(data)} bytes)")
	Path(arguments.file).write_bytes(data)
	print("rom", len(data), "bytes")
	return 0


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
	sub = parser.add_subparsers(dest="action", required=True)
	sub.add_parser("status").set_defaults(run=status)
	sub.add_parser("bind").set_defaults(run=bind)
	command = sub.add_parser("start")
	command.add_argument("--rom")
	command.add_argument("--cpus", type=int, default=8)
	command.add_argument("--memory", default="8G")
	command.set_defaults(run=start)
	sub.add_parser("stop").set_defaults(run=stop)
	command = sub.add_parser("qmp")
	command.add_argument("command")
	command.add_argument("json", nargs="?")
	command.set_defaults(run=qmp)
	command = sub.add_parser("reset-method")
	command.add_argument("method", nargs="?")
	command.set_defaults(run=reset_method)
	command = sub.add_parser("rom")
	command.add_argument("file")
	command.set_defaults(run=rom)
	arguments = parser.parse_args()
	return arguments.run(arguments)


if __name__ == "__main__":
	sys.exit(main())
