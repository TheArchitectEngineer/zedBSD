#!/usr/bin/env python3
"""Photographs both of the Venus guest's displays again and again (ws099-p004, C1).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

The guest has two displays: the standard VGA (the firmware's and the kernel's
picture: the boot logo, or the text console) taken through QMP's screendump,
and the Venus output (the compositor's picture: the greeter, the desktop)
taken through its VNC socket, as frames.py takes it.  A machine has one
screen, which shows the compositor's picture once it has one and the boot
picture before; so each moment's "seen" picture is the Venus one when it is
not black, else the VGA one.  Each is classified as frames.py does (black,
text: the text console, picture).  One line a moment:
    T ms  vga=KIND venus=KIND seen=KIND
The seen picture is kept as OUTDIR/seen-NNN.png when its kind changes and
every tenth moment.  The run ends after SECONDS, once the file --stop names
exists, or when the emulator is gone (neither display answers three times
after a picture was seen).

    c1-watch.py OUTDIR --runtime DIR --seconds 90 [--stop FILE] [--interval 0.25]
"""
import argparse
import json
import os
import socket
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "plan/ws014/tests"))
sys.path.insert(0, str(ROOT / "plan/ws035/tests"))

from venus_rfb import capture  # noqa: E402
from ppm2png import read_ppm, write_png  # noqa: E402
from frames import kind  # noqa: E402


def screendump(qmp_path, ppm):
	"""Takes the standard VGA's picture through QMP; returns (width, height, pixels) or None."""
	try:
		s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
		s.settimeout(5)
		s.connect(qmp_path)
		f = s.makefile("rw")
		f.readline()
		for command in ({"execute": "qmp_capabilities"},
				{"execute": "screendump", "arguments": {"filename": ppm, "format": "ppm"}}):
			f.write(json.dumps(command) + "\n")
			f.flush()
			while True:
				reply = json.loads(f.readline())
				if "return" in reply or "error" in reply:
					break
		s.close()
		if not os.path.exists(ppm):
			return None
		picture = read_ppm(ppm)
		os.unlink(ppm)
		return picture
	except (OSError, ValueError):
		return None


def venus(vnc_path, ppm):
	"""Takes the Venus output's picture through VNC; returns (width, height, pixels) or None."""
	try:
		report = capture(vnc_path, ppm, 3)
	except (OSError, ValueError, RuntimeError):
		return None
	if report is None or not os.path.exists(ppm):
		return None
	picture = read_ppm(ppm)
	os.unlink(ppm)
	return picture


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("outdir")
	parser.add_argument("--runtime", default=os.environ.get("GUEST_RUNTIME", str(ROOT / "build/ws035-sq-run")))
	parser.add_argument("--seconds", type=float, default=90.0)
	parser.add_argument("--stop", default="")
	parser.add_argument("--interval", type=float, default=0.25)
	arguments = parser.parse_args()
	out = Path(arguments.outdir)
	out.mkdir(parents=True, exist_ok=True)
	runtime = Path(arguments.runtime).resolve()
	start = time.monotonic()
	index = 0
	last = None
	seen_any = False
	missing = 0
	while time.monotonic() - start < arguments.seconds:
		if arguments.stop and os.path.exists(arguments.stop):
			break
		taken = time.monotonic() - start
		vga = screendump(str(runtime / "qmp.sock"), str(out / "vga.ppm"))
		ven = venus(str(runtime / "vnc.sock"), str(out / "venus.ppm"))
		if vga is None and ven is None:
			missing += 1
			print("%6d ms  none" % (taken * 1000), flush=True)
			if seen_any and missing >= 3:
				break
			time.sleep(arguments.interval)
			continue
		missing = 0
		vga_kind = kind(*vga) if vga else "none"
		ven_kind = kind(*ven) if ven else "none"
		seen, seen_kind = (ven, ven_kind) if ven_kind not in ("none", "black") else (vga, vga_kind)
		if seen is None:
			seen, seen_kind = ven, ven_kind
		seen_any = True
		print("%6d ms  vga=%s venus=%s seen=%s" % (taken * 1000, vga_kind, ven_kind, seen_kind), flush=True)
		if seen is not None and (seen_kind != last or index % 10 == 0):
			write_png(str(out / ("seen-%03d.png" % index)), *seen)
		last = seen_kind
		index += 1
		time.sleep(arguments.interval)
	return 0


if __name__ == "__main__":
	sys.exit(main())
