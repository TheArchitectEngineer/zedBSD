#!/usr/bin/env python3
"""Photographs the screen of an amd64 boot, from the loader to the login (ws035-p096〜p098).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

The image is copied, and its ESP's zedbsd.cfg may be given more lines (for
example logo=logo.ppm and kmsg=quiet) with mtools.  The copy boots the way
plan/tools/boot-test.sh boots it (OVMF, NVMe, the standard VGA, 8 GiB, KVM
when the host has it), and the screen is taken through QMP every INTERVAL
seconds for SECONDS seconds.  Each picture that differs from the one before
is kept as OUTDIR/NNN-T.png (T the time in tenths of a second).  With
--serial the COM1 output goes to OUTDIR/serial.txt (for dmesg-like checks the
test makes itself, not for judging the boot).

    boot-shots.py IMAGE OUTDIR [--cfg LINE]... [--seconds 40] [--interval 0.25]
      [--replace-cfg FILE] [--extra 'QEMU ARGS'] [--no-kvm] [--pause-at ADDRESS]

--no-kvm (TCG) makes the steps slower, so a short one is caught more often.
--pause-at stops the machine at a kernel address through the gdb stub (a
hardware breakpoint, gdb in batch) and takes OUTDIR/paused.png there: the
kernel's entry (0xffffffff80200000) shows what the loader left on the screen.
"""
import argparse
import hashlib
import json
import os
import shlex
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from ppm2png import read_ppm, write_png  # noqa: E402

CODE = "/usr/share/OVMF/OVMF_CODE_4M.fd"
VARS = "/usr/share/OVMF/OVMF_VARS_4M.fd"
ESP_OFFSET = 2048 * 512


def qmp_connect(path, timeout=20.0):
	"""Connects to QMP and leaves it in command mode."""
	deadline = time.time() + timeout
	while True:
		try:
			connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
			connection.connect(str(path))
			break
		except OSError:
			if time.time() > deadline:
				raise
			time.sleep(0.1)
	stream = connection.makefile("rw")
	stream.readline()
	command(stream, "qmp_capabilities")
	return stream


def command(stream, name, **arguments):
	"""Sends one QMP command and returns its reply (events are skipped)."""
	stream.write(json.dumps({"execute": name, "arguments": arguments}) + "\n")
	stream.flush()
	while True:
		reply = json.loads(stream.readline())
		if "return" in reply or "error" in reply:
			return reply


def payload_offset(image):
	"""The byte offset of the GPT partition named "zedBSD Payload" (the BIOS image's boot FAT)."""
	with open(image, "rb") as disk:
		disk.seek(1024)
		entries = disk.read(128 * 128)
	for index in range(128):
		entry = entries[index * 128:(index + 1) * 128]
		name = entry[56:128].decode("utf-16-le").rstrip("\0")
		if name == "zedBSD Payload":
			return int.from_bytes(entry[32:40], "little") * 512
	raise SystemExit("boot-shots: no zedBSD Payload partition")


def patch_cfg(image, lines, replace, offset=ESP_OFFSET):
	"""Adds lines to (or replaces) the boot FAT's zedbsd.cfg of the copy."""
	target = f"{image}@@{offset}"
	if replace is not None:
		text = Path(replace).read_text()
	else:
		text = subprocess.run(["mtype", "-i", target, "::/zedbsd.cfg"], check=True,
		                      capture_output=True, text=True).stdout
	if lines:
		text = text.rstrip("\n") + "\n" + "\n".join(lines) + "\n"
	temporary = Path(str(image) + ".cfg")
	temporary.write_text(text)
	subprocess.run(["mcopy", "-o", "-i", target, str(temporary), "::/zedbsd.cfg"], check=True)
	temporary.unlink()
	return text


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("image")
	parser.add_argument("outdir")
	parser.add_argument("--cfg", action="append", default=[])
	parser.add_argument("--replace-cfg")
	parser.add_argument("--seconds", type=float, default=40.0)
	parser.add_argument("--interval", type=float, default=0.25)
	parser.add_argument("--serial", action="store_true")
	parser.add_argument("--extra", default="")
	parser.add_argument("--no-kvm", action="store_true")
	parser.add_argument("--bios", action="store_true", help="the legacy BIOS (SeaBIOS) and IDE: the BIOS image")
	parser.add_argument("--pause-at", help="a kernel address (the entry); the screen is taken there, stopped")
	arguments = parser.parse_args()

	out = Path(arguments.outdir)
	shutil.rmtree(out, ignore_errors=True)
	out.mkdir(parents=True)
	disk = out / "disk.img"
	nvram = out / "vars.fd"
	monitor = out / "qmp.sock"
	shutil.copyfile(arguments.image, disk)
	shutil.copyfile(VARS, nvram)
	offset = ESP_OFFSET
	if arguments.bios:
		offset = payload_offset(disk)
	cfg = patch_cfg(disk, arguments.cfg, arguments.replace_cfg, offset)
	(out / "zedbsd.cfg").write_text(cfg)

	qemu = ["qemu-system-x86_64", "-machine", "q35", "-m", "8G", "-smp", "4", "-cpu", "max",
	        "-drive", f"if=pflash,format=raw,readonly=on,file={CODE}",
	        "-drive", f"if=pflash,format=raw,file={nvram}",
	        "-device", "qemu-xhci,id=xhci",
	        "-drive", f"if=none,id=boot,file={disk},format=raw",
	        "-device", "nvme,serial=zedbsd-boot,drive=boot,bootindex=1",
	        "-device", "usb-kbd,bus=xhci.0,port=2",
	        "-vga", "std", "-display", "none", "-no-reboot",
	        "-qmp", f"unix:{monitor},server,nowait"]
	if arguments.bios:
		qemu = ["qemu-system-x86_64", "-machine", "pc", "-m", "8G", "-smp", "4", "-cpu", "max",
		        "-drive", f"file={disk},format=raw,if=ide",
		        "-vga", "std", "-display", "none", "-no-reboot",
		        "-qmp", f"unix:{monitor},server,nowait"]
	if not arguments.no_kvm and os.access("/dev/kvm", os.R_OK | os.W_OK):
		qemu += ["-accel", "kvm"]
	if arguments.serial:
		qemu += ["-serial", f"file:{out / 'serial.txt'}"]
	qemu += shlex.split(arguments.extra)
	hit = out / "paused"
	if arguments.pause_at:
		port = 45000 + os.getpid() % 5000
		qemu += ["-S", "-gdb", f"tcp:127.0.0.1:{port}"]
	log = open(out / "qemu.log", "w")
	process = subprocess.Popen(qemu, stdout=log, stderr=subprocess.STDOUT)
	debugger = None
	try:
		stream = qmp_connect(monitor)
		if arguments.pause_at:
			debugger = subprocess.Popen(
				["gdb", "-nx", "-batch", "-ex", "set architecture i386:x86-64",
				 "-ex", f"target remote 127.0.0.1:{port}", "-ex", f"hbreak *{arguments.pause_at}",
				 "-ex", "continue", "-ex", f"shell touch {hit}", "-ex", "shell sleep 4"],
				stdout=open(out / "gdb.log", "w"), stderr=subprocess.STDOUT)
			deadline = time.time() + arguments.seconds
			while not hit.exists() and time.time() < deadline:
				time.sleep(0.1)
			if hit.exists():
				shot = out / "shot.ppm"
				command(stream, "screendump", filename=str(shot), format="ppm")
				width, height, pixels = read_ppm(shot)
				write_png(out / "paused.png", width, height, pixels)
				print(f"boot-shots: stopped at {arguments.pause_at}: {out / 'paused.png'}")
			else:
				print(f"boot-shots: never stopped at {arguments.pause_at}")
			debugger.wait(timeout=30)
		started = time.time()
		previous = None
		count = 0
		while time.time() - started < arguments.seconds:
			shot = out / "shot.ppm"
			reply = command(stream, "screendump", filename=str(shot), format="ppm")
			if "error" not in reply and shot.exists():
				data = shot.read_bytes()
				digest = hashlib.sha1(data).hexdigest()
				if digest != previous:
					previous = digest
					width, height, pixels = read_ppm(shot)
					tenths = int((time.time() - started) * 10)
					write_png(out / f"{count:03d}-{tenths:04d}.png", width, height, pixels)
					count += 1
			time.sleep(arguments.interval)
		print(f"boot-shots: {count} pictures in {out}")
	finally:
		if debugger is not None and debugger.poll() is None:
			debugger.kill()
		process.terminate()
		try:
			process.wait(timeout=10)
		except subprocess.TimeoutExpired:
			process.kill()
		disk.unlink(missing_ok=True)
		(out / "shot.ppm").unlink(missing_ok=True)


if __name__ == "__main__":
	main()
