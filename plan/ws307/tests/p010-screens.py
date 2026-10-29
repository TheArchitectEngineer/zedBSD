#!/usr/bin/env python3
"""WS307 p010: the installer's screens in QEMU, each photographed.

Boots IMAGE (make installer-image with plan/ws307/tests/config-amd64-p010.mk and the harness's files) from USB with a
Venus GPU (the desktop's compositor needs Vulkan; plan/ws035/tests/zdesktop-guest.sh's set-up), a USB tablet, an empty
NVMe disk and a SATA disk of "another system" (GPT: an ESP with \\EFI\\BOOT\\BOOTX64.EFI, a Microsoft basic data
partition, free room).  The installer opens by itself; the script walks it as a person would, through QMP's keyboard
and tablet (every place clicked is a control the installer's log names: INSTALLER LAYOUT and CONTROL, in the session's
log read over SSH), and photographs each screen and dialog:

  S1, S2, S3 (partitions chosen), S4 and its notice, a partition created, S5, S6 and the no-swap dialog, S7 and the
  firmware's licences, the cancel dialog, S10 (with S4's writes), then S2 again, the NVMe disk whole, S7a, S8, the
  install dialog, S9, S11.

The pictures go to OUT/shots/NN-name.png (QMP screendump of the Venus head, or its VNC when screendump has no surface).
The summary is a line a step and "p010-screens: PASS|FAIL OUT".

    python3 plan/ws307/tests/p010-screens.py IMAGE

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
GUEST = ROOT / "plan/tools/guest/guest.py"
SHOT_VNC = ROOT / "plan/ws035/tests/zdesktop-shot.py"
SESSION_LOG = "/run/user/1000/session.log"
VENUS = Path(os.environ.get("VENUS_RENDERER", "/home/awe/zedBSD-claude1/build/ws035-sq-venus/install"))
ABS_MAX = 0x7FFF
ESP = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
BASIC = "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7"


class Failed(Exception):
	pass


class Run:
	def __init__(self, image: str):
		(ROOT / "build/ws307-test").mkdir(parents=True, exist_ok=True)
		self.out = Path(tempfile.mkdtemp(prefix="p010s.", dir=ROOT / "build/ws307-test"))
		(self.out / "shots").mkdir()
		self.env = dict(os.environ)
		self.env["GUEST_RUNTIME"] = str(self.out / "guest")
		self.env["LIBGL_ALWAYS_SOFTWARE"] = "1"
		self.env["MESA_LOADER_DRIVER_OVERRIDE"] = "zink"
		self.env["VK_DRIVER_FILES"] = self.env.get("VK_DRIVER_FILES", "/usr/share/vulkan/icd.d/lvp_icd.json")
		self.env["RENDER_SERVER_EXEC_PATH"] = str(VENUS / "libexec/virgl_render_server")
		self.env["LD_LIBRARY_PATH"] = str(VENUS / "lib/x86_64-linux-gnu")
		self.image = image
		self.shots = 0
		self.width = 1280
		self.height = 800
		self.qmp = None
		self.lines = []
		self.log = open(self.out / "steps.txt", "w")

	def say(self, text: str):
		print(text, flush=True)
		self.log.write(text + "\n")
		self.log.flush()

	def guest(self, *words, timeout=600):
		return subprocess.run([sys.executable, str(GUEST), *words], env=self.env, capture_output=True, text=True,
				      timeout=timeout)

	# The disks.
	def disks(self):
		shutil.copyfile(self.image, self.out / "boot.img")
		with open(self.out / "nvme.img", "wb") as f:
			f.truncate(12 << 30)
		other = self.out / "other.img"
		with open(other, "wb") as f:
			f.truncate(14 << 30)
		mib = 2048
		subprocess.run([sys.executable, str(ROOT / "plan/ws307/tests/make-gpt-4k.py"), "--sector", "512", str(other),
				f"{mib},{300 * mib},{ESP},{uuid.uuid4()},EFI system partition",
				f"{301 * mib},{4096 * mib},{BASIC},{uuid.uuid4()},Basic data partition"], check=True)
		offset = mib * 512
		subprocess.run(["mformat", "-i", f"{other}@@{offset}", "-F", "-T", str(300 * mib), "-v", "OTHERESP", "::"],
			       check=True, capture_output=True)
		loader = self.out / "BOOTX64.EFI"
		loader.write_bytes(b"MZ another system's loader\n")
		subprocess.run(["mmd", "-i", f"{other}@@{offset}", "::/EFI", "::/EFI/BOOT"], check=True, capture_output=True)
		subprocess.run(["mcopy", "-i", f"{other}@@{offset}", str(loader), "::/EFI/BOOT/BOOTX64.EFI"], check=True,
			       capture_output=True)

	def start(self):
		hostmem = os.environ.get("VENUS_HOSTMEM", "1G")
		out = self.out
		extra = (f"-object memory-backend-memfd,id=mem,size=8G,share=on -machine memory-backend=mem "
			 f"-device virtio-gpu-gl-pci,id=venus,venus=on,blob=on,hostmem={hostmem},max_outputs=1 "
			 f"-display egl-headless,rendernode=/dev/dri/renderD128 -vnc unix:{out}/guest/vnc.sock,display=venus "
			 f"-device usb-tablet,bus=xhci.0,port=4 "
			 f"-drive file={out}/nvme.img,if=none,id=p10n,format=raw -device nvme,serial=p010n,drive=p10n "
			 f"-device ahci,id=p10ahci -drive file={out}/other.img,if=none,id=p10s,format=raw "
			 f"-device ide-hd,drive=p10s,bus=p10ahci.0")
		started = self.guest("start", str(out / "boot.img"), "--disk", "usb", "--qemu-extra", extra)
		(out / "start.txt").write_text(started.stdout + started.stderr)
		if started.returncode != 0:
			raise Failed("start " + started.stderr.strip())
		waited = self.guest("wait", "--timeout", "600", timeout=700)
		(out / "wait.txt").write_text(waited.stdout + waited.stderr)
		if waited.returncode != 0:
			raise Failed("the live system did not answer over SSH")
		session = json.loads((out / "guest/session.json").read_text())
		self.qmp_open(session["monitor"])

	def stop(self):
		self.guest("stop", timeout=120)

	# QMP.
	def qmp_open(self, path: str):
		self.qmp = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
		self.qmp.connect(path)
		self.qmp_file = self.qmp.makefile("rw")
		self.qmp_file.readline()
		self.qmp_call("qmp_capabilities")

	def qmp_call(self, command: str, arguments=None):
		request = {"execute": command}
		if arguments is not None:
			request["arguments"] = arguments
		self.qmp_file.write(json.dumps(request) + "\n")
		self.qmp_file.flush()
		while True:
			line = self.qmp_file.readline()
			if not line:
				raise Failed("QMP closed")
			reply = json.loads(line)
			if "return" in reply or "error" in reply:
				return reply

	def events(self, events):
		reply = self.qmp_call("input-send-event", {"events": events})
		if "error" in reply:
			raise Failed("input " + json.dumps(reply["error"]))

	def key(self, *names):
		down = [{"type": "key", "data": {"down": True, "key": {"type": "qcode", "data": n}}} for n in names]
		up = [{"type": "key", "data": {"down": False, "key": {"type": "qcode", "data": n}}} for n in reversed(names)]
		self.events(down)
		time.sleep(0.05)
		self.events(up)
		time.sleep(0.15)

	def type(self, text: str):
		for c in text:
			if c.isdigit() or ("a" <= c <= "z"):
				self.key(c)
			elif "A" <= c <= "Z":
				self.key("shift", c.lower())
			elif c == "-":
				self.key("minus")
			elif c == " ":
				self.key("spc")
			else:
				raise Failed(f"no key for {c!r}")

	def point(self, x: float, y: float):
		ax = int(round(x * ABS_MAX / (self.width - 1)))
		ay = int(round(y * ABS_MAX / (self.height - 1)))
		self.events([{"type": "abs", "data": {"axis": "x", "value": ax}},
			     {"type": "abs", "data": {"axis": "y", "value": ay}}])

	def press(self, x: float, y: float):
		self.point(x, y)
		time.sleep(0.12)
		self.events([{"type": "btn", "data": {"down": True, "button": "left"}}])
		time.sleep(0.08)
		self.events([{"type": "btn", "data": {"down": False, "button": "left"}}])
		time.sleep(0.4)

	def wheel(self, x: float, y: float, down: bool, count: int = 3):
		self.point(x, y)
		for _ in range(count):
			button = "wheel-down" if down else "wheel-up"
			self.events([{"type": "btn", "data": {"down": True, "button": button}}])
			self.events([{"type": "btn", "data": {"down": False, "button": button}}])
			time.sleep(0.1)

	# The session's log.
	def read_log(self):
		got = self.guest("run", "cat", SESSION_LOG, timeout=60)
		self.lines = got.stdout.splitlines()
		return self.lines

	def wait_line(self, pattern: str, timeout: float = 120.0, after: int = 0):
		rx = re.compile(pattern)
		end = time.time() + timeout
		while time.time() < end:
			lines = self.read_log()
			for index in range(after, len(lines)):
				if rx.search(lines[index]):
					return index, lines[index]
			time.sleep(1.0)
		raise Failed(f"no line {pattern!r} in {timeout:.0f} s")

	def origin(self):
		lines = self.read_log()
		start = 0
		for index, line in enumerate(lines):
			if "KWL INSTALLER start" in line:
				start = index
		place = None
		for line in lines[start:]:
			m = re.search(r"KWL WINDOW centred surface=\d+ x=(-?\d+) y=(-?\d+)", line) or \
				re.search(r"KWL MAP client=\d+ surface=\d+ x=(-?\d+) y=(-?\d+)", line)
			if m:
				place = (int(m.group(1)), int(m.group(2)))
		if place is None:
			raise Failed("the installer's window is not in the compositor's log")
		return place

	def controls(self):
		lines = self.read_log()
		last = None
		for index, line in enumerate(lines):
			if "INSTALLER LAYOUT " in line:
				last = index
		if last is None:
			return "", {}
		screen = re.search(r"screen=(\S+)", lines[last]).group(1)
		found = {}
		for line in lines[last + 1:]:
			m = re.search(r"INSTALLER CONTROL name=(\S+) x=(-?\d+) y=(-?\d+) width=(\d+) height=(\d+) enabled=(\d)", line)
			if not m:
				if "INSTALLER LAYOUT " in line:
					break
				continue
			found[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
		return screen, found

	def wait_screen(self, word: str, timeout: float = 120.0):
		end = time.time() + timeout
		while time.time() < end:
			screen, found = self.controls()
			if screen == word and found:
				time.sleep(0.6)
				return found
			time.sleep(1.0)
		raise Failed(f"screen {word} not shown")

	def click(self, name: str, prefix: bool = False):
		_screen, found = self.controls()
		names = [n for n in found if (n.startswith(name) if prefix else n == name)]
		names = [n for n in names if found[n][4] == 1]
		if not names:
			raise Failed(f"no enabled control {name} ({sorted(found)})")
		x, y, w, h, _enabled = found[names[0]]
		ox, oy = self.origin()
		cy = oy + y + h / 2
		if cy > self.height - 80 and name not in ("next", "back", "close", "start-over", "reboot"):
			self.wheel(ox + 300, oy + 300, True, 5)
			time.sleep(0.8)
			_screen, found = self.controls()
			x, y, w, h, _enabled = found[names[0]]
			cy = oy + y + h / 2
		self.press(ox + x + w / 2, cy)
		return names[0]

	def shot(self, name: str):
		self.shots += 1
		path = self.out / "shots" / f"{self.shots:02d}-{name}.png"
		time.sleep(0.8)
		reply = self.qmp_call("screendump", {"filename": str(path), "device": "venus", "format": "png"})
		how = "screendump"
		if "error" in reply or not path.exists() or path.stat().st_size == 0:
			got = subprocess.run([sys.executable, str(SHOT_VNC), str(path), "--runtime", str(self.out / "guest")],
					     env=self.env, capture_output=True, text=True, timeout=120)
			how = "vnc"
			if got.returncode != 0:
				raise Failed(f"shot {name}: {reply.get('error')} / {got.stderr.strip()}")
		with open(path, "rb") as f:
			head = f.read(24)
		if head[:8] == b"\x89PNG\r\n\x1a\n":
			self.width, self.height = struct.unpack(">II", head[16:24])
		self.say(f"SHOT {path.relative_to(ROOT)} {how} {self.width}x{self.height}")


def walk(run: Run):
	# S1: the installer opens by itself; the disks are read.
	run.wait_line(r"INSTALLER READY", 300)
	run.wait_line(r"INSTALLER PROBE read", 120)
	run.wait_screen("s1-welcome")
	run.say("STEP s1 ok")
	run.shot("s1-welcome")

	# S2: the disks; the other system's SATA disk chosen.
	run.click("next")
	found = run.wait_screen("s2-disk")
	run.shot("s2-disk")
	sata = [n for n in found if n.startswith("disk-sd") and found[n][4] == 1]
	if not sata:
		raise Failed(f"no SATA disk to choose: {sorted(found)}")
	run.click(sata[0])
	run.shot("s2-disk-chosen")
	run.say(f"STEP s2 {sata[0]}")

	# S3: partitions chosen.
	run.click("next")
	run.wait_screen("s3-handling")
	run.click("chosen")
	run.shot("s3-handling")
	run.say("STEP s3 chosen")

	# S4: the notice, then a partition created.
	run.click("next")
	run.wait_screen("s4-edit")
	run.shot("s4-notice")
	run.key("ret")
	time.sleep(1.0)
	run.shot("s4-edit")
	mark = len(run.read_log())
	run.click("create")
	run.wait_line(r"INSTALLER EDIT (DONE|FAIL)", 180, mark)
	run.wait_line(r"INSTALLER PROBE read", 120, mark)
	time.sleep(1.0)
	run.shot("s4-created")
	_index, line = run.wait_line(r"INSTALLER EDIT (DONE|FAIL)", 5, mark)
	run.say(f"STEP s4 {line.split('INSTALLER ', 1)[1]}")

	# S5: the new partition.
	run.click("next")
	run.wait_screen("s5-root")
	run.click("root-", prefix=True)
	run.shot("s5-root")
	run.say("STEP s5 ok")

	# S6, and the dialog of no swap partition.
	run.click("next")
	run.wait_screen("s6-swap")
	run.shot("s6-swap")
	run.click("next")
	time.sleep(1.0)
	run.shot("s6-no-swap-dialog")
	run.key("ret")
	run.wait_screen("s7-items")
	run.say("STEP s6 ok")

	# S7, the firmware's licences.
	run.shot("s7-items")
	run.wheel(run.origin()[0] + 300, run.origin()[1] + 300, True, 10)
	time.sleep(1.0)
	run.shot("s7-items-lower")
	run.click("licenses")
	time.sleep(1.0)
	run.shot("s7-licenses")
	run.click("license-close")
	time.sleep(0.8)
	run.click("license-agree")
	run.say("STEP s7 ok")

	# Cancel: its dialog, then S10 with S4's writes.
	run.click("cancel")
	time.sleep(1.0)
	run.shot("cancel-dialog")
	run.key("ret")
	run.wait_screen("s10-failed")
	run.shot("s10-cancelled")
	run.say("STEP s10 ok")

	# Start over: the NVMe disk, whole.
	run.click("start-over")
	found = run.wait_screen("s2-disk")
	nvme = [n for n in found if n.startswith("disk-nvme") and found[n][4] == 1]
	if not nvme:
		raise Failed(f"no NVMe disk to choose: {sorted(found)}")
	run.click(nvme[0])
	run.click("next")
	run.wait_screen("s3-handling")
	run.click("whole")
	run.shot("s3-whole")
	run.click("next")
	run.wait_screen("s7-items")
	_screen, found = run.controls()
	if "license-agree" in found:
		pass
	run.say(f"STEP whole {nvme[0]}")

	# S7a: the user.
	run.click("next")
	run.wait_screen("s7a-user")
	run.click("user")
	run.type("alice")
	run.click("fullname")
	run.type("Alice")
	run.click("password")
	run.type("alice123")
	run.click("password-again")
	run.type("alice123")
	run.click("root-password")
	run.type("root123")
	run.click("root-password-again")
	run.type("root123")
	time.sleep(0.8)
	run.shot("s7a-user")
	run.say("STEP s7a ok")

	# S8, the erasure agreed, the install dialog.
	run.click("next")
	run.wait_screen("s8-confirm")
	run.click("erase-agree")
	run.shot("s8-confirm")
	run.click("next")
	time.sleep(1.0)
	run.shot("s8-install-dialog")
	mark = len(run.read_log())
	run.key("ret")
	run.wait_screen("s9-progress")
	run.wait_line(r"INSTALLER INSTALL step=6/", 600, mark)
	time.sleep(2.0)
	run.shot("s9-progress")
	run.say("STEP s9 ok")

	# S11.
	index, line = run.wait_line(r"INSTALLER SCREEN name=(s11-done|s10-failed)", 3600, mark)
	run.wait_screen(line.split("name=")[1])
	run.shot(line.split("name=")[1])
	if "s11-done" not in line:
		failed = [l for l in run.read_log()[mark:] if "INSTALLER INSTALL failed" in l or "FAIL" in l]
		raise Failed("the installation failed: " + " | ".join(failed[-3:]))
	run.say("STEP s11 ok")


def main():
	if len(sys.argv) != 2:
		print(__doc__, file=sys.stderr)
		return 2
	run = Run(sys.argv[1])
	status = 0
	try:
		run.disks()
		run.start()
		walk(run)
	except (Failed, subprocess.TimeoutExpired, OSError) as error:
		run.say(f"FAIL {error}")
		status = 1
	finally:
		try:
			lines = run.read_log()
			(run.out / "session.log").write_text("\n".join(l for l in lines if "INSTALLER" in l or "KWL" in l) + "\n")
		except Exception:  # noqa: BLE001
			pass
		run.stop()
	run.say(f"p010-screens: {'PASS' if status == 0 else 'FAIL'} {run.out.relative_to(ROOT)}")
	return status


if __name__ == "__main__":
	sys.exit(main())
