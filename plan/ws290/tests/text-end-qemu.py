#!/usr/bin/env python3
"""The console's leaving of the boot framebuffer, in a QEMU guest.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

Two scenes, each one guest from start to quit:

  trigger  An image whose kernel ends the console's use of the display a
           fixed time after the text layer starts (KERN_TEST_TEXT_END_SECONDS,
           plan/ws290/tests/config-amd64-text-end.mk).  Before the end a
           line written to /dev/console changes the framebuffer and
           /dev/graphics opens; after it the same line changes nothing in
           the framebuffer but reaches the console's cells, /dev/graphics
           does not open, the hardware layer's early console window and the
           backend's mapping are gone, and the serial line still answers.
  core     The graphical boot (plan/ws290/tests/config-amd64-text-end-gpu.mk)
           with the boot framebuffer on the standard VGA adapter and the
           compositor on a separate Venus virtio-gpu: after the compositor's
           first flip the GPU core has ended the console, and a line written
           to /dev/console changes nothing in the standard VGA framebuffer.

The framebuffer is read with QMP's pmemsave over the boot framebuffer's
physical range (the console log is never read); the kernel's state is read
through the emulator's gdb stub.  Only the summary is printed; the details go
to OUT/summary.txt and OUT/details.json.

    text-end-qemu.py trigger --image IMG --symbols VMUNIX --out DIR \\
        --ssh-port N --qmp-port N --gdb-port N [--limit SECONDS]
    text-end-qemu.py core ... --venus RENDERER-INSTALL-DIR

Nothing is removed: OUT must be a new directory.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import re
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
KEY = ROOT / "plan/tmp/guest/id_ed25519"

# The amd64 hardware layer's fixed device windows, which it can never remove.
FIXED_WINDOWS = ((0xa0000, 0x100000), (0xf0000000, 0xf1000000), (0xfec00000, 0xff000000))

# The kernel address of the hardware layer's early console window.
EARLY_WINDOW = 0xffffffffc2000000

EBUSY = 16


def load_serial():
	"""Loads plan/tools/guest/serial.py as a module."""
	path = ROOT / "plan/tools/guest/serial.py"
	spec = importlib.util.spec_from_file_location("zedbsd_serial", path)
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


class Stop(Exception):
	"""A step that could not be done; the scene ends as FAIL."""


class Guest:
	"""One emulator with fixed ports, its QMP connection and its gdb stub."""

	def __init__(self, arguments: argparse.Namespace) -> None:
		self.arguments = arguments
		self.out = Path(arguments.out)
		self.process = None
		self.qmp = None
		self.qmp_stream = None

	def start(self) -> None:
		"""Boots the guest from a copy of the image."""
		out = self.out
		disk = out / "disk.img"
		subprocess.run(["cp", "--reflink=auto", self.arguments.image, str(disk)], check=True)
		nvram = out / "uefi-vars.fd"
		subprocess.run(["cp", self.arguments.ovmf_vars, str(nvram)], check=True)
		acceleration = []
		if os.access("/dev/kvm", os.R_OK | os.W_OK):
			acceleration = ["-accel", "kvm"]
		command = [
			"qemu-system-x86_64", *acceleration,
			"-machine", "q35", "-m", "8192", "-smp", "4",
			"-cpu", "host" if acceleration else "max",
			"-drive", f"if=pflash,format=raw,readonly=on,file={self.arguments.ovmf_code}",
			"-drive", f"if=pflash,format=raw,file={nvram}",
			"-device", "qemu-xhci,id=xhci,p2=8,p3=8",
			"-drive", f"if=none,id=boot,file={disk},format=raw",
			"-device", "nvme,serial=zedbsd-boot,drive=boot,bootindex=1",
			"-netdev", "user,id=net0,net=10.0.2.0/24,host=10.0.2.2,dhcpstart=10.0.2.15,dns=10.0.2.3,"
				   f"hostfwd=tcp:127.0.0.1:{self.arguments.ssh_port}-:22",
			"-device", "usb-net,bus=xhci.0,port=2,id=ecm,netdev=net0,mac=52:54:00:33:00:01,msos-desc=on",
			"-device", "usb-kbd,bus=xhci.0,port=3",
			"-vga", "std", "-no-reboot",
			"-qmp", f"tcp:127.0.0.1:{self.arguments.qmp_port},server,nowait",
			"-serial", f"unix:{out / 'serial.sock'},server,nowait",
			"-gdb", f"tcp:127.0.0.1:{self.arguments.gdb_port}",
		]
		environment = dict(os.environ)
		if self.arguments.scene == "core":
			venus = Path(self.arguments.venus).resolve()
			environment["RENDER_SERVER_EXEC_PATH"] = str(venus / "libexec/virgl_render_server")
			environment["LD_LIBRARY_PATH"] = str(venus / "lib/x86_64-linux-gnu")
			environment["LIBGL_ALWAYS_SOFTWARE"] = "1"
			environment["MESA_LOADER_DRIVER_OVERRIDE"] = "zink"
			environment.setdefault("VK_DRIVER_FILES", "/usr/share/vulkan/icd.d/lvp_icd.json")
			command += [
				"-object", "memory-backend-memfd,id=mem,size=8G,share=on",
				"-machine", "memory-backend=mem",
				"-device", "virtio-gpu-gl-pci,id=venus,venus=on,blob=on,hostmem=1G,max_outputs=1",
				"-display", "egl-headless,rendernode=/dev/dri/renderD128",
			]
		else:
			command += ["-display", "none"]
		log = open(out / "qemu-stderr.txt", "wb")
		self.process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
						stdin=subprocess.DEVNULL, env=environment, start_new_session=True)
		(out / "qemu-command.txt").write_text(" ".join(command) + "\n")

		# QMP answers once the emulator is up.
		deadline = time.monotonic() + 30.0
		while True:
			try:
				self.qmp = socket.create_connection(("127.0.0.1", self.arguments.qmp_port), timeout=10.0)
				break
			except OSError:
				if time.monotonic() > deadline or self.process.poll() is not None:
					raise Stop("the emulator did not open its QMP port")
				time.sleep(0.5)
		self.qmp_stream = self.qmp.makefile("rw")
		self.qmp_stream.readline()
		self.command("qmp_capabilities")

	def command(self, name: str, arguments: dict | None = None) -> dict:
		"""Sends one QMP command and returns its reply."""
		request = {"execute": name}
		if arguments is not None:
			request["arguments"] = arguments
		self.qmp_stream.write(json.dumps(request) + "\n")
		self.qmp_stream.flush()
		while True:
			line = self.qmp_stream.readline()
			if not line:
				raise Stop(f"QMP closed during {name}")
			reply = json.loads(line)
			if "return" in reply or "error" in reply:
				return reply

	def hmp(self, line: str) -> str:
		"""Runs one monitor command and returns its text."""
		reply = self.command("human-monitor-command", {"command-line": line})
		if "error" in reply:
			return "error: " + reply["error"].get("desc", "")
		return reply["return"]

	def memory(self, physical: int, size: int, name: str) -> str:
		"""Saves a physical range and returns its SHA-256."""
		path = self.out / name
		reply = self.command("pmemsave", {"val": physical, "size": size, "filename": str(path)})
		if "error" in reply:
			raise Stop(f"pmemsave failed: {reply['error']}")
		return hashlib.sha256(path.read_bytes()).hexdigest()

	def gdb(self, expressions: list[str]) -> list[str]:
		"""Reads expressions through the gdb stub; the guest stops only while gdb is attached."""
		command = ["gdb", "-q", "-batch", "-nx",
			   "-ex", "set pagination off",
			   "-ex", f"target remote 127.0.0.1:{self.arguments.gdb_port}"]
		for expression in expressions:
			command += ["-ex", expression]
		command += ["-ex", "detach", self.arguments.symbols]
		result = subprocess.run(command, capture_output=True, text=True, timeout=120)
		values = re.findall(r"^\$\d+ = (.*)$", result.stdout, re.MULTILINE)
		with open(self.out / "gdb.txt", "a") as record:
			record.write("$ " + " | ".join(expressions) + "\n" + result.stdout + result.stderr + "\n")
		return values

	def value(self, expression: str) -> int:
		"""Reads one integer through the gdb stub (the kernel has symbols but no types: expressions carry casts)."""
		values = self.gdb([f"print/d {expression}"])
		if len(values) != 1:
			raise Stop(f"gdb could not read {expression}")
		return int(values[0].split()[0])

	def quit(self) -> None:
		"""Ends the emulator with QMP's quit, or a signal when QMP is gone."""
		if self.process is None:
			return
		try:
			if self.qmp_stream is not None:
				self.command("quit")
		except (OSError, Stop, ValueError):
			pass
		try:
			self.process.wait(timeout=20)
		except subprocess.TimeoutExpired:
			self.process.kill()
			self.process.wait(timeout=20)


class Scene:
	"""The checks of one scene and their record."""

	def __init__(self, guest: Guest) -> None:
		self.guest = guest
		self.checks: list[tuple[str, bool, str]] = []
		self.facts: dict = {}

	def expect(self, name: str, condition: bool, detail: str = "") -> bool:
		"""Records one check."""
		self.checks.append((name, bool(condition), detail))
		return bool(condition)

	def framebuffer(self) -> tuple[int, int]:
		"""The boot framebuffer's physical base and size as the backend mapped them."""
		base = self.guest.value("*(unsigned long *)&linear_physical")
		size = self.guest.value("*(unsigned long *)&linear_bytes")
		return base, size

	def window(self, base: int) -> str:
		"""What the monitor translates the hardware layer's early console window to.

		The window starts at EARLY_WINDOW, 2 MiB aligned, so the framebuffer's
		first byte is there at its offset in its 2 MiB page
		(bootloader/include/amd64-handoff.h, src/hal/amd64/bsp-pcat/cons.c).
		"""
		address = EARLY_WINDOW + (base & 0x1fffff)
		return self.guest.hmp(f"gva2gpa {address:#x}").strip()

	def mapped_views(self, physical: int) -> int:
		"""Counts the hardware layer's dynamic device views of one physical base."""
		script = (
			# struct amd64_device_mapping begins with the next pointer and the physical base.
			"python\n"
			"import struct\n"
			"memory = gdb.selected_inferior()\n"
			"count = 0\n"
			"node = int(gdb.parse_and_eval('*(unsigned long *)&device_mappings')) & 0xffffffffffffffff\n"
			"while node != 0:\n"
			"    following, base = struct.unpack('<QQ', bytes(memory.read_memory(node, 16)))\n"
			f"    if base == {physical}:\n"
			"        count += 1\n"
			"    node = following\n"
			"gdb.execute('print %d' % count)\n"
			"end\n")
		path = self.guest.out / "views.gdb"
		path.write_text(script)
		values = self.guest.gdb([f"source {path}"])
		if len(values) != 1:
			raise Stop("gdb could not walk device_mappings")
		return int(values[0])

	def cells_hold(self, text: str) -> bool:
		"""Whether the text layer's cells hold a string, by the low byte of each cell."""
		script = (
			"python\n"
			"import struct\n"
			"memory = gdb.selected_inferior()\n"
			"columns = int(gdb.parse_and_eval('*(unsigned int *)&text_columns'))\n"
			"rows = int(gdb.parse_and_eval('*(unsigned int *)&text_rows'))\n"
			"start = int(gdb.parse_and_eval('(unsigned long)&text_cells')) & 0xffffffffffffffff\n"
			"cells = struct.unpack('<%dH' % (rows * columns), bytes(memory.read_memory(start, 2 * rows * columns)))\n"
			"lines = []\n"
			"for row in range(rows):\n"
			"    lines.append(''.join(chr(cells[row * columns + c] & 0xff) for c in range(columns)))\n"
			f"gdb.execute('print %d' % (1 if any({text!r} in line for line in lines) else 0))\n"
			"end\n")
		path = self.guest.out / "cells.gdb"
		path.write_text(script)
		values = self.guest.gdb([f"source {path}"])
		return len(values) == 1 and values[0] == "1"

	def fixed(self, base: int, size: int) -> bool:
		"""Whether a physical range overlaps a fixed window."""
		return any(base < high and low < base + size for low, high in FIXED_WINDOWS)


def serial_console(guest: Guest, serial):
	"""Connects to the serial line and logs in as root."""
	deadline = time.monotonic() + 30.0
	while True:
		try:
			console = serial.Console(str(guest.out / "serial.sock"), 120.0)
			break
		except OSError:
			if time.monotonic() > deadline:
				raise Stop("no serial socket")
			time.sleep(0.5)
	deadline = time.monotonic() + 300.0
	while True:
		try:
			serial.login(console, "root")
			return console
		except SystemExit:
			if time.monotonic() > deadline:
				raise Stop("no login on the serial line")


def serial_run(console, serial, line: str) -> int:
	"""Runs one command on the serial line, its output kept out of the summary."""
	saved = sys.stdout
	sys.stdout = open(os.devnull, "w")
	try:
		return serial.run(console, line)
	except SystemExit:
		raise Stop(f"no answer on the serial line to {line!r}")
	finally:
		sys.stdout.close()
		sys.stdout = saved


def ssh_run(guest: Guest, line: str) -> int:
	"""Runs one command over SSH and returns its status."""
	command = ["ssh", "-i", str(KEY), "-p", str(guest.arguments.ssh_port),
		   "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
		   "-o", "LogLevel=ERROR", "-o", "ConnectTimeout=5", "-o", "BatchMode=yes",
		   "root@127.0.0.1", line]
	try:
		return subprocess.run(command, capture_output=True, timeout=60).returncode
	except subprocess.TimeoutExpired:
		return 255


def wait_value(guest: Guest, expression: str, want: int, limit: float) -> bool:
	"""Polls an integer through the gdb stub until it has a value."""
	deadline = time.monotonic() + limit
	while time.monotonic() < deadline:
		try:
			if guest.value(expression) == want:
				return True
		except (Stop, subprocess.TimeoutExpired):
			pass
		time.sleep(5.0)
	return False


def scene_trigger(scene: Scene) -> None:
	"""The test trigger's end, with the console drawing before and not after."""
	guest = scene.guest
	serial = load_serial()
	console = serial_console(guest, serial)

	# Before the end: the window is held, the backend maps the framebuffer.
	done = guest.value("*(int *)&text_end_test_done")
	if not scene.expect("before: the trigger has not fired yet", done == 0, "raise KERN_TEST_TEXT_END_SECONDS"):
		return
	base, size = scene.framebuffer()
	fixed = scene.fixed(base, size)
	translated = scene.window(base)
	scene.facts.update({"framebuffer": [hex(base), hex(size)], "fixed_window": fixed, "gva2gpa_before": translated})
	scene.expect("before: the backend maps a boot framebuffer", base != 0 and size != 0)
	scene.expect("before: the early console window translates to it", translated.endswith(f"{base:#x}"), translated)
	scene.expect("before: the backend has a view of the framebuffer", fixed or scene.mapped_views(base) == 1)

	# Before the end the console draws and /dev/graphics opens.
	first = guest.memory(base, size, "before-0.bin")
	scene.expect("before: echo reaches the serial line", serial_run(console, serial, "echo P3MARKONE > /dev/console") == 0)
	time.sleep(1.0)
	second = guest.memory(base, size, "before-1.bin")
	scene.expect("before: a console line changes the framebuffer", first != second)
	scene.expect("before: /dev/graphics opens", serial_run(console, serial, ": < /dev/graphics") == 0)

	# The trigger fires.
	fired = wait_value(guest, "*(int *)&text_end_test_done", 1, guest.arguments.limit)
	if not scene.expect("end: the trigger fired", fired):
		return
	result = guest.value("*(int *)&text_end_test_result")
	scene.facts["result"] = result
	scene.expect("end: kern_text_end's result", result == (EBUSY if fixed else 0), f"result {result}, fixed {fixed}")
	scene.expect("end: text_ended", guest.value("*(int *)&text_ended") == 1)
	translated = scene.window(base)
	scene.facts["gva2gpa_after"] = translated
	if not fixed:
		scene.expect("end: the early console window no longer translates", "gpa:" not in translated, translated)
		scene.expect("end: no view of the framebuffer is left", scene.mapped_views(base) == 0)

	# After the end: nothing draws, the cells follow, the serial line answers, /dev/graphics refuses.
	first = guest.memory(base, size, "after-0.bin")
	scene.expect("after: echo still answers on the serial line", serial_run(console, serial, "echo P3MARKTWO > /dev/console") == 0)
	time.sleep(1.0)
	second = guest.memory(base, size, "after-1.bin")
	scene.expect("after: a console line leaves the framebuffer alone", first == second)
	scene.expect("after: the cells hold the line", scene.cells_hold("P3MARKTWO"))
	scene.expect("after: /dev/graphics does not open", serial_run(console, serial, ": < /dev/graphics") != 0)


def scene_core(scene: Scene) -> None:
	"""The GPU core's end at the compositor's first flip."""
	guest = scene.guest

	# The compositor flips on Venus; the core ends the console.
	ended = wait_value(guest, "*(int *)&gpu_console_ended", 1, guest.arguments.limit)
	if not scene.expect("core: the first flip ended the console", ended):
		return
	scene.expect("core: text_ended", guest.value("*(int *)&text_ended") == 1)
	base, size = scene.framebuffer()
	scene.facts["framebuffer"] = [hex(base), hex(size)]
	scene.facts["gva2gpa_after"] = scene.window(base)
	scene.expect("core: the backend had mapped a boot framebuffer", base != 0 and size != 0)
	if not scene.fixed(base, size):
		scene.expect("core: the early console window no longer translates",
			     "gpa:" not in scene.facts["gva2gpa_after"], scene.facts["gva2gpa_after"])
		scene.expect("core: no view of the framebuffer is left", scene.mapped_views(base) == 0)

	# SSH comes up; a console line leaves the standard VGA framebuffer alone.
	deadline = time.monotonic() + 240.0
	while ssh_run(guest, "true") != 0:
		if time.monotonic() > deadline:
			raise Stop("no answer on SSH")
		time.sleep(3.0)
	first = guest.memory(base, size, "after-0.bin")
	scene.expect("core: echo to /dev/console", ssh_run(guest, "echo P3MARKCORE > /dev/console") == 0)
	time.sleep(1.0)
	second = guest.memory(base, size, "after-1.bin")
	scene.expect("core: a console line leaves the framebuffer alone", first == second)
	scene.expect("core: the cells hold the line", scene.cells_hold("P3MARKCORE"))
	scene.expect("core: /dev/graphics does not open", ssh_run(guest, ": < /dev/graphics") != 0)


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("scene", choices=("trigger", "core"))
	parser.add_argument("--image", required=True)
	parser.add_argument("--symbols", required=True)
	parser.add_argument("--out", required=True)
	parser.add_argument("--ssh-port", type=int, required=True)
	parser.add_argument("--qmp-port", type=int, required=True)
	parser.add_argument("--gdb-port", type=int, required=True)
	parser.add_argument("--venus", default=None)
	parser.add_argument("--limit", type=float, default=240.0)
	parser.add_argument("--ovmf-code", default="/usr/share/OVMF/OVMF_CODE_4M.fd")
	parser.add_argument("--ovmf-vars", default="/usr/share/OVMF/OVMF_VARS_4M.fd")
	arguments = parser.parse_args()
	if arguments.scene == "core" and not arguments.venus:
		parser.error("core needs --venus")
	out = Path(arguments.out)
	out.mkdir(parents=True, exist_ok=False)

	# A timeout's SIGTERM still quits the emulator.
	def terminate(number, frame):
		raise SystemExit(124)
	signal.signal(signal.SIGTERM, terminate)

	guest = Guest(arguments)
	scene = Scene(guest)
	error = ""
	try:
		guest.start()
		if arguments.scene == "trigger":
			scene_trigger(scene)
		else:
			scene_core(scene)
	except Stop as stop:
		error = str(stop)
	finally:
		guest.quit()

	failed = [check for check in scene.checks if not check[1]]
	lines = [f"{'PASS' if ok else 'FAIL'} {name}{(' (' + detail + ')') if detail and not ok else ''}"
		 for name, ok, detail in scene.checks]
	if error:
		lines.append(f"FAIL stopped: {error}")
	verdict = "PASS" if not failed and not error and scene.checks else "FAIL"
	lines.append(f"{verdict} text-end-qemu {arguments.scene}: {len(scene.checks) - len(failed)} passed, "
		     f"{len(failed)} failed ({out})")
	(out / "summary.txt").write_text("\n".join(lines) + "\n")
	(out / "details.json").write_text(json.dumps(scene.facts, indent=1) + "\n")
	print("\n".join(lines))
	return 0 if verdict == "PASS" else 1


if __name__ == "__main__":
	sys.exit(main())
