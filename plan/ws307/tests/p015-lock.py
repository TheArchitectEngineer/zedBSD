#!/usr/bin/env python3
"""WS307 p015: the screen lock's time without input, on a session of the AAT image (config-amd64-p015.mk).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

The items, in order, on one session (each judged by the session's own lines and screenshots over SSH, never the
console or the serial port):

  p015.default-never   the setting power.lock reads 0 (never) on a fresh image; the Settings Power page shows the
                       Screen Lock card at Never (a screenshot); then 11 minutes without input (longer than the ten
                       minutes the session locked after before) and no "KWL LOCK locked".
  p015.slider          on the Power page the lock slider is dragged to its shortest time (1 minute: "KWL PREFERENCES
                       key=power.lock applied value=1") and back to Never ("applied value=0"); then 150 seconds
                       without input and no lock.
  p015.locks           keiland-settings sets power.lock to 1: within 100 seconds without input "KWL LOCK locked
                       reason=idle" and the lock screen (a screenshot); the user's password unlocks it; power.lock is
                       set back to 0.

    plan/ws307/tests/p015-lock.py --outdir OUTDIR -- --qemu
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "plan/tools/aat/scenarios"))
import aatlib  # noqa: E402

run = aatlib.Run.from_command_line("p015")

# The minutes of the lock slider's steps (settings/page-power.c) and the slider's hit index there.
LOCK_STEPS = [0, 1, 2, 5, 10, 15, 30, 60]
LOCK_SLIDER = 4

# The waits without input: longer than the old fixed lock (ten minutes), and after the slider's round trip.
QUIET_DEFAULT = 660
QUIET_AFTER_SLIDER = 150


def quiet(item: aatlib.Item, seconds: int, what: str) -> None:
	"""Waits without any input, then checks that the session did not lock meanwhile."""
	since = run.mark()
	time.sleep(seconds)
	locked = run.lines(r"KWL LOCK locked ", since)
	item.step(f"{seconds} s without input ({what})", locked[-1] if locked else "no KWL LOCK locked")
	run.shot(item, f"after-{seconds}s")
	item.check(not locked, f"the session locked after {what}: {locked[-1] if locked else ''}")


def slider_point(window: aatlib.Window, controls: dict, minutes: int) -> tuple[int, int]:
	"""The point of the screen on the lock slider for a step (the track lies 12 pixels inside the control)."""
	x, y, width, height = controls[LOCK_SLIDER]
	step = LOCK_STEPS.index(minutes)
	along = 12 + (width - 24) * step / (len(LOCK_STEPS) - 1)
	return int(window.x + x + along), int(window.y + y + height // 2)


def drag_to(item: aatlib.Item, window: aatlib.Window, since: str, minutes: int) -> str | None:
	"""Drags the lock slider from its knob's place to a step; returns the compositor's line that applied it."""
	controls = run.controls(since, "power")
	item.check(LOCK_SLIDER in controls, "no lock slider among the Power page's controls")
	current = int(run.as_user("/bin/keiland-settings get power.lock", wait=True)[1].split("value=")[-1].split()[0])
	start = slider_point(window, controls, current if current in LOCK_STEPS else 0)
	end = slider_point(window, controls, minutes)
	mark = run.mark()
	run.drag(start[0], start[1], end[0], end[1])
	line = run.wait(rf"KWL PREFERENCES key=power\.lock applied value={minutes}\b", mark, 10)
	item.step(f"dragged the lock slider to {minutes} minutes ({start} to {end})", line)
	return line


@run.define("p015.default-never")
def default_never(item):
	status, text = run.as_user("/bin/keiland-settings get power.lock", wait=True)
	value = [line for line in text.splitlines() if "KEILAND-SETTINGS value key=power.lock" in line]
	item.step("keiland-settings get power.lock", value[-1] if value else text.strip())
	item.check(value and " value=0 " in value[-1] + " ", f"power.lock is not 0 on a fresh image: {text.strip()}")
	window, since = run.settings(item, "power")
	run.shot(item, "power-page")
	run.close(item, window)
	quiet(item, QUIET_DEFAULT, "the default, never")
	item.passed(f"power.lock 0, no lock in {QUIET_DEFAULT} s; look: power-page.png has the Screen Lock card at Never")


@run.define("p015.slider")
def slider(item):
	window, since = run.settings(item, "power")
	shortest = drag_to(item, window, since, 1)
	run.shot(item, "one-minute")
	item.check(shortest, "the slider's 1 minute was not applied")
	never = drag_to(item, window, since, 0)
	run.shot(item, "never")
	item.check(never, "the slider's Never was not applied")
	run.close(item, window)
	quiet(item, QUIET_AFTER_SLIDER, "1 minute and back to never")
	item.passed(f"1 minute and Never applied, no lock in {QUIET_AFTER_SLIDER} s; look: one-minute.png and never.png")


@run.define("p015.locks")
def locks(item):
	mark = run.mark()
	run.as_user("/bin/keiland-settings set power.lock 1", wait=True)
	applied = run.wait(r"KWL PREFERENCES key=power\.lock applied value=1\b", mark, 10)
	item.step("keiland-settings set power.lock 1", applied)
	item.check(applied, "power.lock 1 was not applied")
	locked = run.wait(r"KWL LOCK locked reason=idle ", mark, 100)
	item.step("up to 100 s without input", locked)
	run.shot(item, "locked")
	item.check(locked, "the session did not lock after 1 minute without input")
	time.sleep(1.0)
	since = run.mark()
	run.type(aatlib.PASSWORD)
	run.key("enter")
	unlocked = run.wait(r"KWL LOCK unlocked", since, 20)
	item.step("typed the password and Enter", unlocked)
	run.shot(item, "unlocked")
	item.check(unlocked, "the screen did not unlock")
	item.passed("locked by idle after 1 minute and unlocked")


def after(item):
	"""Puts the lock back to never after each item."""
	run.as_user("/bin/keiland-settings set power.lock 0", wait=True)


sys.exit(run.go(after=after))
