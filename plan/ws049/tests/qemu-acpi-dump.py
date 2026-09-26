#!/usr/bin/env python3
"""Dumps the ACPI tables a QEMU machine gives its firmware.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    qemu-acpi-dump.py OUTPUT_DIR [QEMU_MACHINE_ARGUMENTS...]

Starts qemu-system-x86_64 (default: -machine q35) with the default SeaBIOS,
without a disk, lets the firmware install the tables, and reads guest memory
over QMP (pmemsave): the RSDP in the BIOS area, then the RSDT or XSDT, every
table they list, the DSDT from the FADT, and the FACS.  Each table is written
as OUTPUT_DIR/<signature>[N].dat (the acpidump -b naming: dsdt.dat,
ssdt1.dat, ...).  Nothing is read from the guest console.

QEMU_ACPI_WAIT (seconds, default 2) is how long the firmware runs first.
Extra arguments replace the machine arguments, e.g.
    qemu-acpi-dump.py build/ws049/tables/q35-smp4 -machine q35 -smp 4
"""
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time


class Qmp:
	"""One QMP connection."""

	def __init__(self, path):
		self.connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
		self.connection.connect(path)
		self.stream = self.connection.makefile("rw")
		self.stream.readline()
		self.execute("qmp_capabilities")

	def execute(self, command, arguments=None):
		request = {"execute": command}
		if arguments is not None:
			request["arguments"] = arguments
		self.stream.write(json.dumps(request) + "\n")
		self.stream.flush()
		while True:
			line = self.stream.readline()
			if not line:
				raise RuntimeError("QMP closed")
			reply = json.loads(line)
			if "return" in reply:
				return reply["return"]
			if "error" in reply:
				raise RuntimeError(reply["error"])


def read_physical(qmp, work, address, size):
	"""Reads guest physical memory through a temporary file."""
	path = os.path.join(work, "chunk.bin")
	qmp.execute("pmemsave", {"val": address, "size": size, "filename": path})
	with open(path, "rb") as stream:
		data = stream.read()
	os.unlink(path)
	return data


def find_rsdp(qmp, work):
	"""Finds the RSDP in the BIOS read-only area."""
	area = read_physical(qmp, work, 0xe0000, 0x20000)
	for offset in range(0, len(area) - 20, 16):
		if area[offset:offset + 8] == b"RSD PTR " and sum(area[offset:offset + 20]) & 0xff == 0:
			return area[offset:offset + 36]
	raise RuntimeError("no RSDP")


def read_table(qmp, work, address):
	"""Reads one table: the header first for its length."""
	header = read_physical(qmp, work, address, 36)
	length = struct.unpack_from("<I", header, 4)[0]
	return read_physical(qmp, work, address, length)


def main():
	if len(sys.argv) < 2:
		print(__doc__, file=sys.stderr)
		return 2
	output = sys.argv[1]
	machine = sys.argv[2:] or ["-machine", "q35"]
	os.makedirs(output, exist_ok=True)
	work = tempfile.mkdtemp(prefix="qemu-acpi-")
	qmp_path = os.path.join(work, "qmp.sock")
	command = ["qemu-system-x86_64"] + machine + [
		"-m", "512", "-display", "none", "-monitor", "none", "-serial", "none",
		"-nodefaults", "-no-reboot", "-qmp", "unix:%s,server=on,wait=off" % qmp_path]
	process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
	try:
		for _ in range(100):
			if os.path.exists(qmp_path):
				break
			time.sleep(0.05)
		# SeaBIOS installs the tables in its first second; it then fails to boot and waits.
		time.sleep(float(os.environ.get("QEMU_ACPI_WAIT", "2")))
		qmp = Qmp(qmp_path)
		qmp.execute("stop")
		rsdp = find_rsdp(qmp, work)
		revision = rsdp[15]
		entries = []
		if revision >= 2 and struct.unpack_from("<Q", rsdp, 24)[0] != 0:
			root = read_table(qmp, work, struct.unpack_from("<Q", rsdp, 24)[0])
			for offset in range(36, len(root), 8):
				entries.append(struct.unpack_from("<Q", root, offset)[0])
		else:
			root = read_table(qmp, work, struct.unpack_from("<I", rsdp, 16)[0])
			for offset in range(36, len(root), 4):
				entries.append(struct.unpack_from("<I", root, offset)[0])
		tables = [root]
		for address in entries:
			table = read_table(qmp, work, address)
			tables.append(table)
			if table[0:4] == b"FACP":
				dsdt = struct.unpack_from("<I", table, 40)[0]
				facs = struct.unpack_from("<I", table, 36)[0]
				if len(table) >= 148 and struct.unpack_from("<Q", table, 140)[0] != 0:
					dsdt = struct.unpack_from("<Q", table, 140)[0]
				tables.append(read_table(qmp, work, dsdt))
				if facs != 0:
					facs_header = read_physical(qmp, work, facs, 8)
					length = struct.unpack_from("<I", facs_header, 4)[0]
					tables.append(read_physical(qmp, work, facs, length))
		counts = {}
		for table in tables:
			signature = table[0:4].decode("ascii").lower()
			counts[signature] = counts.get(signature, 0) + 1
		seen = {}
		for table in tables:
			signature = table[0:4].decode("ascii").lower()
			seen[signature] = seen.get(signature, 0) + 1
			name = signature
			if counts[signature] > 1:
				name = "%s%d" % (signature, seen[signature])
			with open(os.path.join(output, name + ".dat"), "wb") as stream:
				stream.write(table)
			print("%s %d" % (name, len(table)))
		qmp.execute("quit")
	finally:
		try:
			process.wait(timeout=5)
		except subprocess.TimeoutExpired:
			process.kill()
		for name in os.listdir(work):
			os.unlink(os.path.join(work, name))
		os.rmdir(work)
	return 0


if __name__ == "__main__":
	sys.exit(main())
