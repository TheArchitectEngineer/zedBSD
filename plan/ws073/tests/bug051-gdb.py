# BUG-051: gdb (Python) script for the WS073 guest's gdbstub.  Stops the kernel when a process is ended by
# SIGSEGV (exit1_signal with signal 11), finds the user's interrupt frame through the TSS's rsp0 of the stopped
# CPU, and prints the user registers, the stack words below %rbp and the physical pages behind the stack.
#   gdb -q -batch -ex 'set $port=PORT' -ex 'set $exit1=ADDR' -x plan/ws073/tests/bug051-gdb.py build/amd64/vmunix
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import re
import gdb

NAMES = ["r15", "r14", "r13", "r12", "r11", "r10", "r9", "r8", "rbp", "rdi", "rsi", "rdx", "rcx", "rbx", "rax",
	 "vector", "error_code", "rip", "cs", "rflags", "rsp", "ss"]


def run(command):
	return gdb.execute(command, to_string=True)


def q(address):
	return int(gdb.parse_and_eval(f"*(unsigned long long *){address:#x}")) & 0xffffffffffffffff


port = int(gdb.parse_and_eval("$port"))
exit1 = int(gdb.parse_and_eval("$exit1")) & 0xffffffffffffffff
run("set pagination off")
run(f"target remote 127.0.0.1:{port}")
run(f"hbreak *{exit1:#x} if $rdi == 11")
hits = 0
while hits < 3:
	run("continue")
	hits += 1
	cpu = gdb.selected_thread().num - 1
	print(f"=== hit {hits}: cpu {cpu}")
	run(f"monitor cpu {cpu}")
	regs = run("monitor info registers")
	tr = re.search(r"TR =\w+ ([0-9a-f]+)", regs)
	cr3 = re.search(r"CR3=([0-9a-f]+)", regs)
	print(f"CR3={cr3.group(1) if cr3 else '?'}")
	base = int(tr.group(1), 16)
	rsp0 = q(base + 4)
	frame = rsp0 - 8 * len(NAMES)
	values = {name: q(frame + 8 * i) for i, name in enumerate(NAMES)}
	print(f"rsp0={rsp0:#x} frame={frame:#x}")
	print(" ".join(f"{n}={values[n]:#x}" for n in NAMES))
	rbp = values["rbp"]
	rsp = values["rsp"]
	print("stack words from rbp-0x140 to rbp:")
	for offset in range(-0x140, 0x10, 8):
		try:
			print(f"  rbp{offset:+#06x} [{rbp + offset:#x}] = {q(rbp + offset):#x}")
		except gdb.error as error:
			print(f"  rbp{offset:+#06x} unreadable: {error}")
	for address in sorted({rsp & ~0xfff, (rbp - 0x140) & ~0xfff, rbp & ~0xfff}):
		print(run(f"monitor gva2gpa {address:#x}").strip())
	print(run("monitor info registers").split("\n")[0])
	gdb.execute("detach")
	break
