#!/usr/bin/env python3
"""The automatic helpers of tests/scenarios/os/ (WS173 p004): each does its scenario's steps by the same id.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    helpers_os.py --outdir OUTDIR [--only REGEX] -- TARGET-OPTIONS     (--list: the ids)

The scenarios that need a person's hands (os.power.power-button, shutdown,
lid, ac-plug) have no helper: run-aat.sh marks them needs-person.
"""
import re
import shlex
import sys
import time

import aatlib
import common

run = aatlib.Run.from_command_line("helpers_os")


@run.define("os.boot.session-up")
def session_up(item):
	line = run.wait(r"ZWL READY socket=\S+ width=\d+ height=\d+ .* role=desktop", None, 120)
	item.step("waited for the desktop", line)
	item.check(line, "no ZWL READY ... role=desktop in the session's log")
	run.shot(item, "desktop")
	item.passed(f"{aatlib.field(line, 'width')}x{aatlib.field(line, 'height')}")


def kernel_log() -> str:
	"""The kernel's log: the file the boot keeps and dmesg."""
	_, text = run.sh("{ cat /var/log/kernel.log; dmesg; } 2>/dev/null")
	(run.outdir / "dmesg.txt").write_text(text)
	return text


@run.define("os.boot.kernel-log")
def boot_kernel_log(item):
	text = kernel_log()
	bad = [line.strip() for line in text.splitlines() if re.search(r"\bpanic\b|\bfatal\b|page fault", line, re.I)]
	item.step("read /var/log/kernel.log and dmesg (dmesg.txt)", bad[0] if bad else f"{len(text.splitlines())} lines, none bad")
	item.check(text.strip(), "the kernel's log is empty")
	item.check(not bad, f"kernel: {bad[0] if bad else ''}")
	item.passed()


@run.define("os.acpi.tables-and-touchpad")
def acpi_touchpad(item):
	text = kernel_log()
	ready = [line.strip() for line in text.splitlines() if "namespace ready" in line]
	item.step("looked for the namespace", ready[0] if ready else "none")
	item.check(ready, "no 'acpi: ... namespace ready' (BUG-195)")
	stopped = [line.strip() for line in text.splitlines() if re.search(r"ACPI: .*(stopped at offset|did not load)", line)]
	item.step("looked for a table that stopped", stopped[0] if stopped else "none")
	item.check(not stopped, f"{stopped[0] if stopped else ''} (BUG-195)")
	touch = [line.strip() for line in text.splitlines() if "i2c-hid:" in line]
	item.step("looked for the touch pad's driver", "; ".join(touch)[:300] or "none")
	item.check(any("reads on its interrupt" in line for line in touch), "the touch pad does not read on its interrupt")
	worse = [line for line in touch if re.search(r"samples its input|did not start|given up|not taken", line)]
	lost = [line.strip() for line in text.splitlines() if "fired for no pad" in line]
	item.step("looked for a lost interrupt", (worse + lost)[0] if worse + lost else "none")
	item.check(not worse and not lost, f"{(worse + lost)[0] if worse + lost else ''}")
	item.passed(next(line for line in touch if "reads on its interrupt" in line))


@run.define("os.power.battery-state")
def battery_state(item):
	status, text = run.sh("systemevents -p 2>&1")
	item.step("systemevents -p", text.strip())
	item.check(status == 0 and re.search(r"battery=\d+", text) and "ac=" in text, f"no battery or AC: {text.strip()[:120]}")
	power = run.lines(r"ZWL POWER source=", None)
	percent = aatlib.number(power[-1], "percent") if power else None
	item.step("read ZWL POWER in the session's log", power[-1] if power else "none")
	item.check(percent is not None and percent >= 0, "the compositor knows no battery")
	run.shot(item, "bar")
	item.person("the battery's icon left of the clock, with + while charging (the screenshot)")


def terminal(item):
	"""Opens Terminal from App Home and puts the pointer in it; returns its window."""
	window = run.launch(item, "Terminal")
	run.click(*window.middle())
	time.sleep(0.5)
	return window


def read_work(name: str) -> str:
	"""A file the steps wrote under /tmp/aat-work."""
	_, text = run.sh(f"cat {aatlib.WORK}/{name} 2>/dev/null")
	return text


@run.define("os.accounts.sudo")
def sudo(item):
	terminal(item)
	run.type(f"sudo id -u > {aatlib.WORK}/sudo.txt; echo rc=$? >> {aatlib.WORK}/sudo.txt")
	run.key("enter")
	time.sleep(1.5)
	run.type(aatlib.PASSWORD)
	run.key("enter")
	time.sleep(2.0)
	right = read_work("sudo.txt")
	item.step("sudo id -u with kei's password", right.strip())
	run.shot(item, "right")
	item.check(right.split() == ["0", "rc=0"], f"sudo with the right password: {right.strip()!r}")
	run.type(f"sudo -k; sudo true; echo rc=$? > {aatlib.WORK}/sudo-wrong.txt")
	run.key("enter")
	for _ in range(3):
		time.sleep(1.5)
		run.type("wrong-password")
		run.key("enter")
		time.sleep(2.5)
	time.sleep(2.0)
	wrong = read_work("sudo-wrong.txt")
	item.step("sudo true with a wrong password three times", wrong.strip())
	run.shot(item, "wrong")
	item.check(wrong.strip().startswith("rc=") and wrong.strip() != "rc=0", f"three wrong passwords: {wrong.strip()!r}")
	item.passed()


@run.define("os.accounts.su-root-locked")
def su_root(item):
	terminal(item)
	run.type(f"su -c true; echo rc=$? > {aatlib.WORK}/su.txt")
	run.key("enter")
	time.sleep(1.5)
	run.type("anything")
	run.key("enter")
	time.sleep(3.0)
	text = read_work("su.txt")
	item.step("su -c true with any password", text.strip())
	run.shot(item, "su")
	item.check(text.strip().startswith("rc=") and text.strip() != "rc=0", f"su: {text.strip()!r}")
	item.passed(text.strip())


def shadow_line() -> str:
	"""kei's line of /etc/shadow."""
	_, text = run.sh(f"grep '^{aatlib.USER}:' /etc/shadow")
	return text.strip()


@run.define("os.accounts.passwd")
def passwd(item):
	common.keep_shadow(run)
	before = shadow_line()
	terminal(item)
	for new, name in (("short", "short"), ("aat-pass-1", "long")):
		run.type("passwd")
		run.key("enter")
		for answer in (aatlib.PASSWORD, new, new):
			time.sleep(1.5)
			run.type(answer)
			run.key("enter")
		time.sleep(3.0)
		after = shadow_line()
		item.step(f"passwd to {new!r}", "kei's shadow line changed" if after != before else "kei's shadow line kept")
		run.shot(item, name)
		if name == "short":
			item.check(after == before, "the short password was taken")
		else:
			item.check(after != before, "the long password was not taken")
	common.restore_shadow(run)
	item.check(shadow_line() == before, "/etc/shadow was not put back")
	item.step("put /etc/shadow back", "kei's line is the first one again")
	item.passed()


def admin(request: list[str]) -> str:
	"""Runs account-admin as kei with a request; returns its answer."""
	text = "".join(line + "\n" for line in request)
	inner = f"printf %s {shlex.quote(text)} | /usr/libexec/account-admin"
	_, answer = run.sh(f"su {aatlib.USER} -c {shlex.quote(inner)} 2>&1", timeout=30)
	return answer.strip()


@run.define("os.accounts.account-admin")
def account_admin(item):
	run.sh("grep -q '^aatuser:' /etc/passwd && { printf 'kei\\nremove\\naatuser\\nremove-home\\n' | su kei -c /usr/libexec/account-admin; }; "
		"grep -q '^aatuser:' /etc/passwd || rm -rf /home/aatuser; true")
	cases = [
		("a wrong caller's password", ["wrong-pass", "add", "aatuser", "AAT User", "aat-pass-1", "user"], "error bad-password"),
		("a bad name", [aatlib.PASSWORD, "add", "Bad Name", "AAT User", "aat-pass-1", "user"], "error bad-name"),
		("a short password", [aatlib.PASSWORD, "add", "aatuser", "AAT User", "short", "user"], "error weak-password"),
		("aatuser added", [aatlib.PASSWORD, "add", "aatuser", "AAT User", "aat-pass-1", "user"], "ok"),
	]
	for what, request, expected in cases:
		answer = admin(request)
		item.step(f"account-admin: {what}", answer)
		item.check(answer == expected, f"{what}: {answer!r}, not {expected!r}")
	_, there = run.sh("grep -c '^aatuser:' /etc/passwd; test -d /home/aatuser && echo home")
	item.step("looked at /etc/passwd and /home/aatuser", there.strip())
	item.check(there.split() == ["1", "home"], f"aatuser after the addition: {there.strip()!r}")
	answer = admin([aatlib.PASSWORD, "remove", "aatuser", "remove-home"])
	_, gone = run.sh("grep -c '^aatuser:' /etc/passwd")
	item.step("account-admin: aatuser removed", f"{answer}; passwd count {gone.strip()}")
	item.check(answer == "ok" and gone.strip() == "0", f"removal: {answer!r}, count {gone.strip()}")
	_, as_root = run.sh("printf 'x\\nadd\\naatroot\\nAAT\\naat-pass-1\\nuser\\n' | /usr/libexec/account-admin 2>&1")
	item.step("account-admin run by root", as_root.strip())
	item.check(as_root.strip() != "ok", "root was taken as a caller")
	item.passed()


sys.exit(run.go(before=common.before(run), after=common.after(run)))
