#!/usr/bin/env python3
"""ws174-p003: the boot keys' QEMU cells (for the test runner T1).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

Boots the image built from plan/ws174/tests/config-amd64-keys.mk (graphical
boot: logo=logo.ppm login=graphical kmsg=quiet) once per cell, each time from
a fresh copy of the image and of the OVMF variables, one QEMU at a time:

  C0 none        no key
  C1 ctrl        Ctrl held, Space tapped every 0.1 s, from power-on
  C2 shift       Shift held, Space tapped likewise
  C3 ctrl-shift  Ctrl and Shift held, Space tapped likewise
  C4 late        no key until the loader has set its mode, then 5 s later
                 Ctrl+Space and Shift+Space for 2 s (after the loader)

Keys go by QMP input-send-event (the default input handler; QMP cannot
name the usb-kbd as the target), as a person does it: the modifiers stay
down and Space is tapped (down, 50 ms, up) every 0.1 s.  Every 0.1 s the
modifiers are pressed down again, which changes nothing while they are
down but presses them again after the firmware's or the kernel's USB reset
has made the emulated keyboard forget them.  The modifiers are let go when
the cell stops sending: 2 s after the screen shows the loader has run (the
splash mode 1920x1080, a switch down to 640x480 from a larger firmware
mode, or kernel text), or after 60 s.  A screendump is taken every second
for the first 40 s; each frame whose kind (text, logo, other) differs from
the one before, and the evidence, are kept as PNG.

The verdict reads only screendumps and SSH (no console or serial log):
  (a) the screen: C0, C2, C4 show the logo; C1, C3 show kernel text without
      the logo.
  (b) the login prompt (plan/tools/boot-test.py) is reached.
  (c) over SSH, `sysctl kern.boot.login` is graphical (C0, C1, C4) or
      console (C2, C3), and the kernel's `boot: parameters:` line in dmesg
      holds kmsg=console and no logo= (C1, C3) or kmsg=quiet and logo=
      (C0, C2, C4).  There is no kern.boot.kmsg sysctl; dmesg is the
      kernel's log buffer read over SSH, not the console log.

    plan/ws174/tests/run-boot-keys-qemu.sh IMAGE OUTDIR [--cells C0,C1,...]
        [--no-usb-kbd] [--ctrl-alone]

--no-usb-kbd drops the usb-kbd device (the keys then reach the PS/2
keyboard): the retry when C1 to C3 all fail to detect the keys.
--ctrl-alone runs the reference cell R0 (Ctrl held, no Space) first; its
result is recorded, never part of the verdict.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

# The PPM screens are read with plan/tools/boot-test.py's own font and cell reader.
_spec = importlib.util.spec_from_file_location("boot_test", ROOT / "plan/tools/boot-test.py")
boot_test = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(boot_test)

# The SSH key pair and options of plan/tools/guest/guest.py.
sys.path.insert(0, str(ROOT / "plan/tools/guest"))
import guest  # noqa: E402

# How often Space is tapped (and the modifiers pressed again), and how long Space stays down.
KEY_PERIOD = 0.1
KEY_TAP = 0.05

# The longest a cell sends keys, and how long it goes on after the mode changes.
SEND_LIMIT = 60.0
SEND_AFTER_MODE = 2.0

# The frames are taken every second for this long from power-on.
FRAME_PERIOD = 1.0
FRAME_WINDOW = 40.0

# The login prompt and SSH limits.
LOGIN_TIMEOUT = 240.0
SSH_TIMEOUT = 240.0

# Kernel text: a line with one of these words, or three lines of 10 characters.
TEXT_WORDS = ("A64", "zedBSD", "hal", "init", "boot:")
TEXT_LINE_MIN = 10
TEXT_LINES_MIN = 3

# The logo: no text, and this share of the middle third's pixels not black.
LOGO_SHARE_MIN = 0.10

# The cells: the chords sent and what each must show.
CELLS = {
	"C0": {"keys": [], "late": False, "screen": "logo", "login": "graphical", "kmsg": "quiet"},
	"C1": {"keys": [["ctrl", "spc"]], "late": False, "screen": "text", "login": "graphical", "kmsg": "console"},
	"C2": {"keys": [["shift", "spc"]], "late": False, "screen": "logo", "login": "console", "kmsg": "quiet"},
	"C3": {"keys": [["ctrl", "shift", "spc"]], "late": False, "screen": "text", "login": "console", "kmsg": "console"},
	"C4": {"keys": [["ctrl", "spc"], ["shift", "spc"]], "late": True, "screen": "logo", "login": "graphical", "kmsg": "quiet"},
	"R0": {"keys": [["ctrl"]], "late": False, "screen": "text", "login": "graphical", "kmsg": "console"},
}


class Guest:
	"""One QEMU booted from a fresh copy of the image."""

	def __init__(self, image: Path, runtime: Path, usb_kbd: bool) -> None:
		runtime.mkdir(parents=True, exist_ok=True)
		self.runtime = runtime
		self.disk = runtime / "disk.img"
		subprocess.run(["cp", "--reflink=auto", str(image), str(self.disk)], check=True)
		nvram = runtime / "uefi-vars.fd"
		subprocess.run(["cp", "/usr/share/OVMF/OVMF_VARS_4M.fd", str(nvram)], check=True)
		self.monitor_path = runtime / "qmp.sock"
		self.monitor_path.unlink(missing_ok=True)
		self.ssh_port = guest.free_port()
		acceleration = []
		if os.access("/dev/kvm", os.R_OK | os.W_OK):
			acceleration = ["-accel", "kvm"]
		command = [
			"qemu-system-x86_64", *acceleration,
			"-machine", "q35", "-m", "8192", "-smp", "4",
			"-cpu", "host" if acceleration else "max",
			"-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
			"-drive", f"if=pflash,format=raw,file={nvram}",
			"-device", "qemu-xhci,id=xhci,p2=8,p3=8",
			"-drive", f"if=none,id=boot,file={self.disk},format=raw",
			"-device", "nvme,serial=zedbsd-boot,drive=boot,bootindex=1",
			"-netdev", "user,id=net0,net=10.0.2.0/24,host=10.0.2.2,"
				   "dhcpstart=10.0.2.15,dns=10.0.2.3,"
				   f"hostfwd=tcp:127.0.0.1:{self.ssh_port}-:22",
			"-device", "usb-net,bus=xhci.0,port=2,id=ecm,netdev=net0,"
				   "mac=52:54:00:33:00:01,msos-desc=on",
		]
		if usb_kbd:
			command += ["-device", "usb-kbd,bus=xhci.0,port=3"]
		command += [
			"-vga", "std", "-display", "none", "-no-reboot",
			"-qmp", f"unix:{self.monitor_path},server,nowait",
		]
		self.log = open(runtime / "qemu.log", "wb")
		self.process = subprocess.Popen(command, stdout=self.log, stderr=subprocess.STDOUT,
						stdin=subprocess.DEVNULL, start_new_session=True)
		self.started = time.monotonic()
		self.monitor = boot_test.Monitor(str(self.monitor_path), timeout=30.0)
		self.lock = threading.Lock()

	def command(self, name: str, **arguments) -> dict:
		"""Sends one QMP command, one at a time across the threads."""
		with self.lock:
			return self.monitor.command(name, **arguments)

	def send_keys(self, keys: list[str], down: bool) -> None:
		"""Presses (or lets go of) the keys, in order, at once."""
		events = [{"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": key}}}
			  for key in keys]
		if events:
			self.command("input-send-event", events=events)

	def send_chord(self, chord: list[str], held: list[str]) -> list[str]:
		"""Holds the chord's modifiers and taps its other keys.

		The modifiers of the chord before (held) that this chord has not
		are let go first.  Returns the modifiers now held.
		"""
		modifiers = [key for key in chord if key in ("ctrl", "shift", "alt")]
		taps = [key for key in chord if key not in modifiers]
		self.send_keys([key for key in held if key not in modifiers], False)
		self.send_keys(modifiers + taps, True)
		if taps:
			time.sleep(KEY_TAP)
			self.send_keys(taps, False)
		return modifiers

	def screendump(self, path: Path) -> None:
		"""Writes the screen as PPM."""
		with self.lock:
			self.monitor.screendump(path)

	def close_monitor(self) -> None:
		"""Leaves QMP to plan/tools/boot-test.py."""
		self.monitor.socket.close()

	def ssh(self, command: str) -> subprocess.CompletedProcess:
		"""Runs one command in the guest."""
		session = {"ssh_port": self.ssh_port}
		return subprocess.run(["ssh", *guest.ssh_options(session), "root@127.0.0.1", command],
				      capture_output=True, text=True, timeout=60)

	def stop(self) -> None:
		"""Stops QEMU and deletes the disk copy."""
		if self.process.poll() is None:
			self.process.terminate()
			try:
				self.process.wait(timeout=10)
			except subprocess.TimeoutExpired:
				self.process.kill()
				self.process.wait()
		self.log.close()
		self.disk.unlink(missing_ok=True)


def ppm_size(path: Path) -> tuple[int, int]:
	"""Reads a PPM's width and height only."""
	width, height, _ = boot_test.read_ppm(path)
	return width, height


def read_grid(width: int, height: int, pixels: bytes, font: dict, left: int, top: int) -> list[str]:
	"""Reads at most 80x30 cells of 8x16 from the origin (fast on a large screen)."""
	rows: list[str] = []
	columns = min(80, (width - left) // boot_test.GLYPH_WIDTH)
	for cell_row in range(min(30, (height - top) // boot_test.GLYPH_HEIGHT)):
		line = []
		for cell_column in range(columns):
			bitmap = bytearray()
			for row in range(boot_test.GLYPH_HEIGHT):
				y = top + cell_row * boot_test.GLYPH_HEIGHT + row
				bits = 0
				base = (y * width + left + cell_column * boot_test.GLYPH_WIDTH) * 3
				for column in range(boot_test.GLYPH_WIDTH):
					offset = base + column * 3
					if pixels[offset] + pixels[offset + 1] + pixels[offset + 2] >= 128 * 3:
						bits |= 0x80 >> column
				bitmap.append(bits)
			line.append(font.get(bytes(bitmap), "�"))
		rows.append("".join(line).rstrip())
	return rows


def text_rows(path: Path, font: dict) -> list[str]:
	"""Reads the console text at the origins the consoles use.

	The kernel console writes from the corner (or the margins that centre a
	partial cell); the HAL early console centres an 80x30 grid; a few
	consoles centre 80x25.  Only an 80x30 window is read at each origin.
	"""
	width, height, pixels = boot_test.read_ppm(path)
	origins = [(0, 0),
		   ((width % boot_test.GLYPH_WIDTH) // 2, (height % boot_test.GLYPH_HEIGHT) // 2),
		   (max(0, (width - 640) // 2), max(0, (height - 480) // 2)),
		   (max(0, (width - 640) // 2), max(0, (height - 400) // 2))]
	rows: list[str] = []
	for origin in dict.fromkeys(origins):
		rows += read_grid(width, height, pixels, font, origin[0], origin[1])
	return rows


def has_text(rows: list[str]) -> bool:
	"""Kernel text: a known word, or three lines of 10 ASCII letters or digits.

	Only letters and digits count: a lit or dark area of a picture reads as
	the font's block glyphs and spaces, which are not text.
	"""
	long_lines = 0
	for line in rows:
		if "�" in line:
			continue
		words = re.findall(r"[A-Za-z0-9.:]+", line)
		if any(word.rstrip(":") in TEXT_WORDS or word in TEXT_WORDS for word in words):
			return True
		letters = sum(1 for ch in line if ch.isascii() and ch.isalnum())
		if letters >= TEXT_LINE_MIN:
			long_lines += 1
	return long_lines >= TEXT_LINES_MIN


def logo_share(path: Path) -> float:
	"""The share of the middle third's pixels that are not black (sampled)."""
	width, height, pixels = boot_test.read_ppm(path)
	lit = 0
	total = 0
	for y in range(height // 3, 2 * height // 3, 4):
		for x in range(width // 3, 2 * width // 3, 4):
			offset = (y * width + x) * 3
			total += 1
			if pixels[offset] + pixels[offset + 1] + pixels[offset + 2] > 48:
				lit += 1
	return lit / total if total else 0.0


def classify(path: Path, font: dict) -> str:
	"""Reports a frame as text, logo or other."""
	rows = text_rows(path, font)
	if has_text(rows):
		return "text"
	if logo_share(path) >= LOGO_SHARE_MIN:
		return "logo"
	return "other"


def run_cell(name: str, image: Path, outdir: Path, usb_kbd: bool, font: dict) -> dict:
	"""Boots one cell and returns its record."""
	cell = CELLS[name]
	cell_dir = outdir / name
	shutil.rmtree(cell_dir, ignore_errors=True)
	frames = cell_dir / "frames"
	frames.mkdir(parents=True)
	record: dict = {"cell": name, "usb_kbd": usb_kbd, "expected": cell}
	vm = Guest(image, cell_dir / "runtime", usb_kbd)
	stop_sending = threading.Event()
	state = {"mode_changed_at": None, "first_mode": None, "chords": 0}

	def sender() -> None:
		"""Sends the cell's chords on their schedule, then lets the modifiers go."""
		held: list[str] = []
		try:
			send_chords(held)
		finally:
			try:
				vm.send_keys(held, False)
			except (OSError, SystemExit):
				pass

	def send_chords(held: list[str]) -> None:
		"""Holds the modifiers and taps Space until the cell stops sending."""
		start = time.monotonic()
		while not stop_sending.is_set():
			now = time.monotonic()
			changed = state["mode_changed_at"]
			if cell["late"]:
				# After the loader: 5 s after its mode change, for 2 s.
				if changed is None or now < changed + 5.0:
					time.sleep(KEY_PERIOD)
					continue
				if now > changed + 7.0:
					return
			else:
				if now - start > SEND_LIMIT:
					return
				if changed is not None and now > changed + SEND_AFTER_MODE:
					return
			chord = cell["keys"][state["chords"] % len(cell["keys"])]
			try:
				held[:] = vm.send_chord(chord, held)
			except (OSError, SystemExit):
				return
			state["chords"] += 1
			time.sleep(KEY_PERIOD)

	thread = None
	if cell["keys"]:
		thread = threading.Thread(target=sender, daemon=True)
		thread.start()

	# The frames, one a second.  The loader has sampled the keys once the
	# screen shows its work: the splash mode 1920x1080, a switch down to
	# 640x480 from a larger firmware mode (Ctrl), or kernel text.
	saved: list[tuple[float, Path, int, int, str, bool]] = []
	widest = 0
	index = 0
	while time.monotonic() - vm.started < FRAME_WINDOW:
		if vm.process.poll() is not None:
			break
		frame = frames / f"{index:03d}.ppm"
		try:
			vm.screendump(frame)
		except SystemExit:
			break
		at = time.monotonic() - vm.started
		width, height = ppm_size(frame)
		kind = classify(frame, font)
		loader = False
		if (width, height) == (1920, 1080):
			loader = True
		if (width, height) == (640, 480) and widest > 640:
			loader = True
		if kind == "text":
			loader = True
		widest = max(widest, width)
		if loader and state["mode_changed_at"] is None:
			state["mode_changed_at"] = time.monotonic()
			state["first_mode"] = f"{width}x{height} at {at:.1f}s"
		saved.append((at, frame, width, height, kind, state["mode_changed_at"] is not None))
		index += 1
		time.sleep(FRAME_PERIOD)
	stop_sending.set()
	if thread is not None:
		thread.join(timeout=5)
	record["chords_sent"] = state["chords"]
	record["loader_seen"] = state["first_mode"]

	# (a) the screen: the first frame of the expected kind once the loader
	# has run is the evidence; for Ctrl no logo may come before it.  The
	# frames before the loader are the firmware's (its own logo, if any).
	kinds = []
	evidence = None
	logo_before = False
	previous_kind = None
	for at, frame, width, height, kind, after_loader in saved:
		kinds.append({"at": round(at, 1), "mode": f"{width}x{height}", "kind": kind,
			      "after_loader": after_loader})
		keep = evidence is None and after_loader and kind == cell["screen"]
		if evidence is None and after_loader and kind == "logo":
			logo_before = True
		if keep:
			evidence = frame.with_suffix(".png")
		# Keeps the evidence and every frame whose kind differs from the one before.
		if keep or kind != previous_kind:
			boot_test.write_png(frame, frame.with_suffix(".png"))
		previous_kind = kind
		frame.unlink(missing_ok=True)
	record["frames"] = kinds
	record["screen_evidence"] = str(evidence) if evidence else None
	record["loader_modes"] = sorted({entry["mode"] for entry in kinds})
	record["screen_pass"] = evidence is not None
	if cell["screen"] == "text" and logo_before:
		record["screen_pass"] = False

	# (b) the login prompt, with boot-test.py's reader.
	vm.close_monitor()
	login_png = cell_dir / "login.png"
	result = subprocess.run([sys.executable, str(ROOT / "plan/tools/boot-test.py"),
				 "--monitor", str(vm.monitor_path), "--screenshot", str(login_png),
				 "--timeout", str(LOGIN_TIMEOUT)], capture_output=True, text=True)
	record["login_pass"] = result.returncode == 0
	record["login_png"] = str(login_png)

	# (c) SSH: kern.boot.login and the record the kernel logged.
	deadline = time.monotonic() + SSH_TIMEOUT
	answered = False
	while time.monotonic() < deadline and vm.process.poll() is None:
		try:
			probe = vm.ssh("true")
		except subprocess.TimeoutExpired:
			continue
		if probe.returncode == 0:
			answered = True
			break
		time.sleep(2.0)
	record["ssh"] = answered
	if answered:
		# zedBSD's sysctl has no -n: it prints "kern.boot.login: VALUE".
		answer = vm.ssh("sysctl kern.boot.login")
		login = None
		for line in answer.stdout.splitlines():
			if line.startswith("kern.boot.login:"):
				login = line[len("kern.boot.login:"):].strip()
		parameters = vm.ssh("dmesg | grep 'boot: parameters:'").stdout.strip()
		record["kern_boot_login"] = login
		record["sysctl_output"] = {"status": answer.returncode, "stdout": answer.stdout,
					   "stderr": answer.stderr}
		record["boot_parameters"] = parameters
		tokens = parameters.split()
		kmsg_ok = f"kmsg={cell['kmsg']}" in tokens
		logo_present = any(token.startswith("logo=") for token in tokens)
		logo_ok = logo_present == (cell["kmsg"] == "quiet")
		record["login_value_pass"] = login == cell["login"]
		record["kmsg_pass"] = kmsg_ok and logo_ok
	else:
		record["login_value_pass"] = False
		record["kmsg_pass"] = False
	vm.stop()
	record["pass"] = all(record[key] for key in ("screen_pass", "login_pass", "login_value_pass", "kmsg_pass"))
	(cell_dir / "result.json").write_text(json.dumps(record, indent=1) + "\n")
	return record


def main() -> int:
	"""Runs the cells in order and prints the verdicts."""
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("image")
	parser.add_argument("outdir")
	parser.add_argument("--cells", default="C0,C1,C2,C3,C4")
	parser.add_argument("--no-usb-kbd", action="store_true")
	parser.add_argument("--ctrl-alone", action="store_true")
	arguments = parser.parse_args()

	image = Path(arguments.image).resolve()
	outdir = Path(arguments.outdir).resolve()
	outdir.mkdir(parents=True, exist_ok=True)
	guest.make_keys()
	font = boot_test.load_font(ROOT / "src/drivers/platform/pcat/graphics/vgafont.c")
	cells = arguments.cells.split(",")
	if arguments.ctrl_alone:
		cells = ["R0"] + cells
	records = []
	for name in cells:
		record = run_cell(name, image, outdir, not arguments.no_usb_kbd, font)
		records.append(record)
		verdict = "PASS" if record["pass"] else "FAIL"
		if name == "R0":
			verdict = "REFERENCE " + verdict
		print(f"{name}: {verdict} screen={record['screen_pass']} login={record['login_pass']} "
		      f"kern.boot.login={record.get('kern_boot_login')!r} kmsg={record['kmsg_pass']} "
		      f"modes={record['loader_modes']} chords={record['chords_sent']}")
	(outdir / "summary.json").write_text(json.dumps(records, indent=1) + "\n")
	failed = [r for r in records if not r["pass"] and r["cell"] != "R0"]
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
