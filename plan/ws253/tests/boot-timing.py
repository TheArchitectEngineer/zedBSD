#!/usr/bin/env python3
"""Times one boot of a graphical image in QEMU, from the emulator's start to the desktop.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

The guest is the Venus desktop guest (plan/ws035/tests/zdesktop-guest.sh), so
the compositor draws as it does on a machine.  Nothing reads the serial port
or the emulator's console log: the host watches the Venus screen through VNC
(a picture each second, and the moments it changed) and SSH (when it first
answers), and afterwards reads the guest's own records over SSH: init's
timeline (/var/log/init.log, when the image has one), sessiond's log, the
session's log and the system log.

    plan/ws253/tests/boot-timing.py IMAGE OUTDIR [--seconds 150]

GUEST_RUNTIME names the guest's runtime directory (default build/ws253/run).
The Venus renderer is VENUS_RENDERER as zdesktop-guest.sh reads it.  The guest
is stopped at the end.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "plan/ws014/tests"))
sys.path.insert(0, str(ROOT / "plan/ws035/tests"))

from venus_rfb import capture  # noqa: E402
from ppm2png import read_ppm, write_png  # noqa: E402

GUEST = ROOT / "plan/ws035/tests/zdesktop-guest.sh"

# What is read from the guest once the boot has settled, each into a file of its own.
RECORDS = {
	"init.log": "cat /var/log/init.log",
	"sessiond.log": "cat /var/log/sessiond.log",
	"session.log": "cat /run/user/*/session.log",
	"messages": "cat /var/log/messages",
	"dmesg.txt": "dmesg",
	"services.txt": "service list",
	"boot.txt": "sysctl kern.boot.login; sysctl kern.boot.kmsg; date +%s",
}


def guest(runtime: Path, *words: str, capture_output: bool = True) -> subprocess.CompletedProcess:
	"""Runs the guest script with the runtime directory of this run."""
	environment = dict(os.environ, GUEST_RUNTIME=str(runtime))
	return subprocess.run([str(GUEST), *words], cwd=ROOT, env=environment,
			      capture_output=capture_output, text=True, timeout=120)


def shoot(runtime: Path, output: Path) -> str | None:
	"""Photographs the Venus screen, and returns a digest of its pixels (None without a picture)."""
	ppm = output.with_suffix(".ppm")
	try:
		report = capture(str(runtime / "vnc.sock"), str(ppm), 5)
	except (OSError, ValueError, RuntimeError):
		return None
	if report is None or not ppm.exists():
		return None
	width, height, pixels = read_ppm(str(ppm))
	write_png(str(output), width, height, pixels)
	ppm.unlink()
	return hashlib.sha256(pixels).hexdigest()[:16]


def main() -> int:
	"""Boots the image, watches it, and keeps what it saw."""
	parser = argparse.ArgumentParser()
	parser.add_argument("image")
	parser.add_argument("outdir")
	parser.add_argument("--seconds", type=float, default=150.0)
	arguments = parser.parse_args()

	runtime = Path(os.environ.get("GUEST_RUNTIME", str(ROOT / "build/ws253/run"))).resolve()
	outdir = Path(arguments.outdir).resolve()
	frames = outdir / "frames"
	frames.mkdir(parents=True, exist_ok=True)

	# The emulator starts; every time below is measured from here.
	started = time.monotonic()
	wall = time.time()
	result = guest(runtime, "start", str(Path(arguments.image).resolve()))
	if result.returncode != 0:
		print(result.stdout + result.stderr, file=sys.stderr)
		return 1

	# The screen each second, and SSH until it answers.
	events = []
	last = None
	ssh_at = None
	next_ssh = 0.0
	while time.monotonic() - started < arguments.seconds:
		moment = time.monotonic() - started
		picture = frames / f"{moment:06.1f}.png"
		digest = shoot(runtime, picture)
		if digest != last:
			events.append({"t": round(moment, 1), "screen": digest, "picture": picture.name if digest else None})
			last = digest
		elif digest is not None:
			picture.rename(frames / "same.png")
		if ssh_at is None and moment >= next_ssh:
			answer = guest(runtime, "run", "true")
			if answer.returncode == 0:
				ssh_at = round(time.monotonic() - started, 1)
				events.append({"t": ssh_at, "ssh": "answers"})
			next_ssh = moment + 2.0
		time.sleep(max(0.0, 1.0 - (time.monotonic() - started - moment)))

	# The guest's own records.
	for name, command in RECORDS.items():
		answer = guest(runtime, "run", command)
		(outdir / name).write_text(answer.stdout + answer.stderr)
	(outdir / "events.json").write_text(json.dumps({"started_wall": wall, "events": events}, indent=1) + "\n")
	guest(runtime, "stop")
	for event in events:
		print(event)
	return 0


if __name__ == "__main__":
	sys.exit(main())
