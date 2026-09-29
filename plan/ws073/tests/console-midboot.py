#!/usr/bin/env python3
"""BUG-031 check: kernel lines on the screen during boot are never mixed.

Boots IMAGE COUNT times from USB with the WS073 guest harness (the USB and
storage probes then log from several CPUs at once).  While each boot runs,
the screen is taken through QMP as fast as the monitor answers (about
every 0.1 s) and every frame is read afterwards with the console font
(plan/tools/boot-test.py's reader), so the lines of the middle of the boot
are seen before they scroll away.  After the login prompt, the kernel's
records are fetched (dmesg over SSH) and every kernel-looking line seen on
any frame is looked up among the records cut into 80 columns.  A line that
is in no record was mixed with another one.  Frames with such a line are
kept as PNG in OUT.

    python3 plan/ws073/tests/console-midboot.py IMAGE COUNT [OUT]

GUEST_RUNTIME defaults to build/ws073-mix.  Prints CONSOLE-MIDBOOT:PASS.
Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import importlib.util
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("boot_test", ROOT / "plan/tools/boot-test.py")
boot_test = importlib.util.module_from_spec(spec)
spec.loader.exec_module(boot_test)

KERNEL = re.compile(r"^(vfs|usb[-a-z0-9]*|input|nvme|xhci|ehci|uhci|pci|loop[0-9]|swap|boot|acpi|hda|net|"
		    r"cdc[-a-z]*|sd[a-z]|disk|fat|ufs|tty|smp|cpu[0-9]*|kernel|graphics|i915|venus|rtl[0-9a-z]*|"
		    r"wifi|ax211|kern|xhci[0-9]*): ")


def guest(*words, timeout=600):
	return subprocess.run([sys.executable, str(ROOT / "plan/tools/guest/guest.py"), *words],
			      capture_output=True, text=True, timeout=timeout, stdin=subprocess.DEVNULL)


def main():
	image, count = sys.argv[1], int(sys.argv[2])
	out = Path(sys.argv[3] if len(sys.argv) > 3 else ROOT / "build/ws073-p036/midboot")
	out.mkdir(parents=True, exist_ok=True)
	os.environ.setdefault("GUEST_RUNTIME", str(ROOT / "build/ws073-mix"))
	runtime = Path(os.environ["GUEST_RUNTIME"])
	font = boot_test.load_font(ROOT / "src/drivers/platform/pcat/graphics/vgafont.c")
	mixed_boots = 0
	compared = 0
	for boot in range(1, count + 1):
		guest("stop")
		started = guest("start", "--disk", "usb", image)
		if started.returncode != 0:
			print(f"boot {boot}: start failed: {started.stdout} {started.stderr}")
			continue
		monitor = boot_test.Monitor(str(runtime / "qmp.sock"), timeout=30.0)
		frames_dir = out / f"boot{boot}-frames"
		frames_dir.mkdir(exist_ok=True)
		# Takes frames as fast as the monitor gives them and reads them afterwards,
		# so the fast-scrolling middle of the boot is not missed while one is read.
		paths = []
		deadline = time.monotonic() + 150
		while time.monotonic() < deadline:
			frame = frames_dir / f"f{len(paths) + 1:04d}.ppm"
			monitor.screendump(frame)
			paths.append(frame)
			if len(paths) % 20 == 0:
				lines = boot_test.read_text(frame, font)
				if any(re.search(r"^(?:\S+ )?login: ?", line) for line in lines):
					break
			time.sleep(0.03)
		frames = len(paths)
		seen = {}
		for number, frame in enumerate(paths, 1):
			for line in boot_test.read_text(frame, font):
				line = line.rstrip()
				if KERNEL.match(line) and line not in seen:
					seen[line] = number
		waited = guest("wait", timeout=300)
		if waited.returncode != 0:
			print(f"boot {boot}: no SSH ({frames} frames, {len(seen)} kernel lines)")
			continue
		dmesg = guest("run", "dmesg").stdout
		chunks = set()
		for record in dmesg.splitlines():
			record = record.rstrip()
			for at in range(0, max(len(record), 1), 80):
				chunks.add(record[at:at + 80].rstrip())
		suspects = [line for line in seen
			    if line not in chunks and not any(chunk.startswith(line) for chunk in chunks)]
		compared += 1
		(out / f"boot{boot}-lines.txt").write_text("\n".join(seen) + "\n")
		# Keeps the frames that show a suspect line, as PNG, and drops the others.
		keep = {seen[line] for line in suspects}
		for number, frame in enumerate(paths, 1):
			if number in keep:
				boot_test.write_png(frame, frame.with_suffix(".png"))
			frame.unlink(missing_ok=True)
		if suspects:
			mixed_boots += 1
			print(f"boot {boot}: {frames} frames, {len(seen)} kernel lines, suspect lines:")
			for line in suspects:
				print(f"    [frame {seen[line]}] {line}")
		else:
			print(f"boot {boot}: {frames} frames, {len(seen)} kernel lines, all in dmesg")
	guest("stop")
	print(f"boots {count}, compared {compared}, boots with suspect lines {mixed_boots}")
	print("CONSOLE-MIDBOOT:PASS" if compared == count and mixed_boots == 0 else "CONSOLE-MIDBOOT:FAIL")


if __name__ == "__main__":
	main()
