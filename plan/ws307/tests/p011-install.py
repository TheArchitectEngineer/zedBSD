#!/usr/bin/env python3
"""WS307 p011: the installer end to end in QEMU, through its window.

Boots IMAGE (make installer-image with plan/ws307/tests/config-amd64-p010.mk and the harness's files) from USB with a
Venus GPU, a USB tablet, an empty NVMe disk and a SATA disk of another system (an ESP with \\EFI\\BOOT\\BOOTX64.EFI and
another file, a basic data partition, free room), and walks the installer as a person would
(plan/ws307/tests/installer_walk.py):

  refused   the live USB disk (S2), the empty disk's partitions (no GPT, S3)
  cancel    the NVMe disk whole, cancelled while the units unpack: S10 says where it stopped
  failure   the NVMe disk whole with a unit damaged (a byte appended): the helper refuses its SHA-256, S10
  whole     the NVMe disk whole, the user's automatic login off
  chosen    the disk of another system: a 1 GiB partition made on S4 for swap, the basic data partition chosen as
            the root (its erasure confirmed); the other system's files stay, the loader goes to \\EFI\\ZEDBSD

then, the live system stopped, checks the other system's ESP on the host, starts the NVMe disk alone to the console's
login prompt (plan/tools/boot-test.sh) and logs in to it over SSH as the user with the password chosen.  The SATA
disk is not started: its loader is in \\EFI\\ZEDBSD, which the firmware starts only through a Boot#### entry (p007).
The 4096-byte disk, the USB disk of a swap file, and the disks refused for their tables (MBR, zedbsd.cfg) are left
out: with any of them attached the live system with the Venus GPU did not start (2026-10-29, recorded in
plan/ws307/phase011/phase.md).

The pictures go to OUT/shots/; the summary is a line a step ("STEP name ok|fail ...") and "p011-install: PASS|FAIL OUT".

    python3 plan/ws307/tests/p011-install.py IMAGE

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from installer_walk import BASIC, ESP, ROOT, Failed, Run, blank, fat, fat_read, gpt  # noqa: E402

USER = "alice"
PASSWORD = "alice123"
HOSTNAME = "p011host"
OTHER_LOADER = b"MZ another system's loader\n"
OTHER_FILE = b"another system's file\n"
DAMAGED = "/opt/installer/units/fonts-hand-hershey.tar.gz"


class Walk:
	def __init__(self, run: Run):
		self.run = run
		self.names = {}
		self.agreed = False
		self.autologin = True
		self.status = 0

	def step(self, name: str, ok: bool, detail: str = ""):
		self.run.say(f"STEP {name} {'ok' if ok else 'fail'} {detail}".rstrip())
		if not ok:
			self.status = 1

	# The disks.
	def disks(self) -> str:
		"""The empty NVMe disk and the disk of another system (the disk set the live system starts with, below)."""
		out = self.run.out
		blank(out / "nvme.img", 12)
		other = out / "other.img"
		blank(other, 14)
		gpt(other, 512, (1, 300, ESP, "EFI system partition"), (301, 4096, BASIC, "Basic data partition"))
		fat(other, 1, 300, "OTHERESP", {"/EFI/BOOT/BOOTX64.EFI": OTHER_LOADER, "/OTHER/NOTE.TXT": OTHER_FILE})
		return (f"-drive file={out}/nvme.img,if=none,id=n1,format=raw -device nvme,serial=p011n,drive=n1 "
			f"-device ahci,id=a1 -drive file={other},if=none,id=s1,format=raw -device ide-hd,drive=s1,bus=a1.0")

	@staticmethod
	def region(path: Path, first_mib: int, mib: int) -> str:
		digest = hashlib.sha256()
		with open(path, "rb") as f:
			f.seek(first_mib << 20)
			left = mib << 20
			while left > 0:
				chunk = f.read(min(left, 8 << 20))
				if not chunk:
					break
				digest.update(chunk)
				left -= len(chunk)
		return digest.hexdigest()

	def map_disks(self):
		"""The live system's names of the disks, by their sizes (install-admin probe as root)."""
		code, text = self.run.root("/usr/libexec/install-admin probe")
		sizes = {"nvme": 12 << 30, "other": 14 << 30}
		for line in text.splitlines():
			field = line.split("\t")
			if field[0] != "disk":
				continue
			size = int(field[4]) * int(field[5])
			for name, want in sizes.items():
				if size == want:
					self.names[name] = field[1]
		self.step("map", len(self.names) == len(sizes), " ".join(f"{k}={v}" for k, v in sorted(self.names.items())))
		if len(self.names) != len(sizes):
			raise Failed(f"disks not found: {text}")

	def choose(self, disk: str):
		self.run.click("disk-" + self.names[disk])
		self.run.click("next")
		self.run.wait_screen("s3-handling")

	def items_user_confirm(self, label: str, autologin: bool = False):
		"""S7 (the licences agreed once), S7a, S8 (the erasure agreed for the whole disk), and the install dialog."""
		run = self.run
		run.wait_screen("s7-items")
		if not self.agreed:
			run.click("license-agree")
			self.agreed = True
		run.click("next")
		run.wait_screen("s7a-user")
		run.set_field("user", USER)
		run.set_field("fullname", "Alice")
		run.set_field("password", PASSWORD)
		run.set_field("password-again", PASSWORD)
		run.set_field("root-password", "root123")
		run.set_field("root-password-again", "root123")
		run.set_field("hostname", HOSTNAME)
		if autologin != self.autologin:
			run.click("autologin")
			self.autologin = autologin
		run.shot(f"{label}-s7a")
		run.click("next")
		found = run.wait_screen("s8-confirm")
		if "erase-agree" in found:
			run.click("erase-agree")
		run.shot(f"{label}-s8")
		run.click("next")
		time.sleep(1.0)
		mark = len(run.read_log())
		run.key("ret")
		run.wait_screen("s9-progress")
		return mark

	def finish(self, label: str, mark: int, timeout: float = 1800):
		"""Waits for S11 or S10 after an installation; returns the screen's word."""
		run = self.run
		_index, line = run.wait_line(r"INSTALLER SCREEN name=(s11-done|s10-failed)", timeout, mark)
		word = line.split("name=")[1]
		run.wait_screen(word)
		run.shot(f"{label}-{word}")
		return word

	def start_over(self):
		self.run.click("start-over")
		self.run.wait_screen("s2-disk")

	def walk(self):
		run = self.run
		run.wait_line(r"INSTALLER READY", 300)
		run.wait_line(r"INSTALLER PROBE read", 120)
		run.wait_screen("s1-welcome")
		self.map_disks()
		run.shot("s1-welcome")
		run.click("next")
		found = run.wait_screen("s2-disk")
		run.shot("s2-disks")
		live = [n for n in found if n.startswith("disk-") and found[n][4] == 0]
		self.step("refused-live", len(live) == 1, " ".join(live))

		# The empty NVMe disk's partitions cannot be chosen (no GPT).
		self.choose("nvme")
		found = run.wait_screen("s3-handling")
		run.shot("s3-nvme-empty")
		self.step("refused-chosen", found.get("chosen", (0, 0, 0, 0, 1))[4] == 0)
		run.click("back")
		run.wait_screen("s2-disk")

		# The NVMe disk whole, cancelled while the units unpack.
		self.choose("nvme")
		run.click("whole")
		run.click("next")
		mark = self.items_user_confirm("cancel")
		run.wait_line(r"INSTALLER INSTALL step=6/", 600, mark)
		run.click("cancel")
		time.sleep(1.0)
		run.shot("cancel-stop-dialog")
		run.key("ret")
		word = self.finish("cancel", mark)
		lines = run.read_log()[mark:]
		self.step("cancel", word == "s10-failed" and any("failed step=unpack" in l for l in lines))
		self.start_over()

		# The NVMe disk whole with a damaged unit.
		code, _ = run.root(f"cp -p {DAMAGED} /tmp/p011-unit && printf x >> {DAMAGED}")
		self.step("damage", code == 0)
		self.choose("nvme")
		run.click("whole")
		run.click("next")
		mark = self.items_user_confirm("failure")
		word = self.finish("failure", mark)
		failed = [l for l in run.read_log()[mark:] if "INSTALLER INSTALL failed" in l]
		self.step("failure", word == "s10-failed" and bool(failed), failed[-1] if failed else "")
		code, _ = run.root(f"cp -p /tmp/p011-unit {DAMAGED}")
		self.step("restore", code == 0)
		self.start_over()

		# The NVMe disk whole.
		self.choose("nvme")
		run.click("whole")
		run.click("next")
		mark = self.items_user_confirm("nvme")
		word = self.finish("nvme", mark)
		self.step("nvme", word == "s11-done")
		self.harness("nvme")
		self.start_over()

		# The disk of another system: a swap partition made, the second basic data partition the root.
		self.choose("other")
		run.click("chosen")
		run.shot("other-s3")
		run.click("next")
		run.wait_screen("s4-edit")
		run.key("ret")
		time.sleep(0.8)
		run.set_field("size", "1 GiB")
		mark = len(run.read_log())
		run.click("create")
		run.wait_line(r"INSTALLER EDIT (DONE|FAIL)", 180, mark)
		run.wait_line(r"INSTALLER PROBE read", 120, mark)
		time.sleep(1.0)
		run.shot("other-s4")
		run.click("next")
		run.wait_screen("s5-root")
		run.click("root-2")
		time.sleep(1.0)
		run.shot("other-erase-dialog")
		run.key("ret")
		time.sleep(0.8)
		run.shot("other-s5")
		run.click("next")
		run.wait_screen("s6-swap")
		run.click("swap-3")
		run.shot("other-s6")
		run.click("next")
		mark = self.items_user_confirm("other", autologin=True)
		word = self.finish("other", mark)
		zedbsd = any("STATE loader" in l and "ZEDBSD" in l for l in run.read_log()[mark:])
		self.step("other", word == "s11-done" and zedbsd)

	def harness(self, disk: str):
		"""The harness's network on an installed root (slot 3 of the whole disk's layout), for the SSH after."""
		name = self.names[disk]
		code, text = self.run.root(
			f"mkdir -p /mnt/p011 && part=$(ls /dev | grep '^{name}p\\?3$' | head -1) && mount -t ufs /dev/$part /mnt/p011 && "
			f"cp /etc/net.conf /mnt/p011/etc/net.conf && grep -c . /mnt/p011/var/db/installer/units && "
			f"grep '^{USER}:' /mnt/p011/etc/passwd && grep hostname /mnt/p011/etc/rc.conf; umount /mnt/p011")
		self.step(f"{disk}-harness", code == 0 and USER in text and HOSTNAME in text, text.replace("\n", "|"))


def boot(walk: Walk, name: str, mode: str, extra: dict):
	out = walk.run.out
	env = dict(os.environ, BOOT_MODE=mode, OUTPUT=str(out / f"boot-{name}"), BOOT_TIMEOUT="240", **extra)
	got = subprocess.run(["timeout", "500", "bash", str(ROOT / "plan/tools/boot-test.sh"), str(out / f"{name}.img")], env=env,
			     capture_output=True, text=True)
	(out / f"boot-{name}.txt").write_text(got.stdout + got.stderr)
	last = (got.stdout.strip().splitlines() or [""])[-1]
	walk.step(f"boot-{name}", got.returncode == 0, last)


def ssh_check(walk: Walk):
	out = walk.run.out
	env = dict(walk.run.env, GUEST_RUNTIME=str(out / "guest-nvme"))
	guest = [sys.executable, str(ROOT / "plan/tools/guest/guest.py")]
	subprocess.run(guest + ["start", str(out / "nvme.img"), "--disk", "nvme"], env=env, capture_output=True, timeout=120)
	port = json.loads((out / "guest-nvme/session.json").read_text())["ssh_port"]
	answer = ""
	for _ in range(60):
		got = subprocess.run(["sshpass", "-p", PASSWORD, "ssh", "-p", str(port), "-o", "StrictHostKeyChecking=no",
				      "-o", "UserKnownHostsFile=/dev/null", "-o", "ConnectTimeout=5", "-o", "PubkeyAuthentication=no",
				      f"{USER}@127.0.0.1", "id -un; hostname; cut -f1 /var/db/installer/units | tr '\\n' ' '"],
				     capture_output=True, text=True, timeout=60)
		if got.returncode == 0:
			answer = got.stdout
			break
		time.sleep(5)
	subprocess.run(guest + ["stop"], env=env, capture_output=True, timeout=120)
	walk.step("ssh-nvme", answer.startswith(USER) and HOSTNAME in answer, answer.replace("\n", "|"))


def host_checks(walk: Walk):
	other = walk.run.out / "other.img"
	walk.step("other-loader-kept", fat_read(other, 1, "/EFI/BOOT/BOOTX64.EFI") == OTHER_LOADER)
	walk.step("other-file-kept", fat_read(other, 1, "/OTHER/NOTE.TXT") == OTHER_FILE)
	ours = fat_read(other, 1, "/EFI/ZEDBSD/BOOTX64.EFI")
	walk.step("other-loader-ours", ours is not None and ours[:2] == b"MZ" and ours != OTHER_LOADER)
	cfg = fat_read(other, 1, "/zedbsd.cfg") or b""
	walk.step("other-cfg", b"kernel=rootpart:/boot/vmunix" in cfg and b"swap0=PARTUUID=" in cfg, cfg.decode(errors="replace").replace("\n", "|"))


def main():
	if len(sys.argv) != 2:
		print(__doc__, file=sys.stderr)
		return 2
	run = Run(sys.argv[1], "p011.")
	walk = Walk(run)
	try:
		run.start(walk.disks())
		walk.walk()
	except (Failed, subprocess.TimeoutExpired, OSError) as error:
		run.say(f"FAIL {error}")
		walk.status = 1
	finally:
		try:
			lines = run.read_log()
			(run.out / "session.log").write_text("\n".join(l for l in lines if "INSTALLER" in l or "KWL" in l) + "\n")
		except Exception:  # noqa: BLE001
			pass
		run.stop()
	if walk.status == 0:
		host_checks(walk)
		boot(walk, "nvme", "uefi-nvme", {})
		ssh_check(walk)
	run.say(f"p011-install: {'PASS' if walk.status == 0 else 'FAIL'} {run.out.relative_to(ROOT)}")
	return walk.status


if __name__ == "__main__":
	sys.exit(main())
