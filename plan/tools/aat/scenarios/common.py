"""What every AAT helper does around a scenario (WS173 p004).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

Before: the applications a scenario may have left are ended, App Home is
closed, /tmp/aat-work (where the steps write) is there for everyone.
After: a docked session's layout mode is brought back to windowed (the
restore button of the docked window in front, ws142-p008), the
applications are ended again, and /etc/shadow is put back when a scenario
kept a copy (a password changed).
"""
import time

import aatlib

SHADOW_COPY = "/tmp/aat-shadow.copy"


def before(run):
	"""The function run.go calls before each scenario."""
	def prepare(item):
		run.stop_programs()
		run.sh(f"mkdir -p {aatlib.WORK} && chmod 1777 {aatlib.WORK}")
		# App Home or a menu left open takes the keys: Esc twice closes either.
		run.key("esc", "esc")
	return prepare


def after(run):
	"""The function run.go calls after each scenario."""
	def tidy(item):
		run.back_to_windowed()
		run.stop_programs()
		restore_shadow(run)
	return tidy


def keep_shadow(run) -> None:
	"""Keeps a copy of /etc/shadow (root's, 0400) to put back (never on this host, --local)."""
	if "--local" in run.target:
		return
	run.sh(f"cp -p /etc/shadow {SHADOW_COPY}")


def restore_shadow(run) -> None:
	"""Puts the copy of /etc/shadow back, when there is one (never on this host, --local)."""
	if "--local" in run.target:
		return
	run.sh(f"if [ -f {SHADOW_COPY} ]; then cat {SHADOW_COPY} > /etc/shadow && rm -f {SHADOW_COPY}; fi; true")


# The input method (the Languages page of Settings, and Alt+Space).

METHOD_SWITCHES = {0: 1, 1: 2, 2: 3}


def current_method(run) -> int:
	"""The input method the desktop has now (0 none, 1 Japanese, 2 SKK; 0 when the log says none)."""
	lines = run.lines(r"ZWL IME method=\d+", None)
	return aatlib.number(lines[-1], "method") if lines else 0


def set_method(run, item, method: int) -> None:
	"""Chooses the input method on the Languages page (its switch, control 1, 2 or 3) and closes Settings."""
	if current_method(run) == method:
		item.step(f"the input method is {method} already")
		return
	window, since = run.settings(item, "languages")
	controls = run.controls(since, "languages")
	mark = run.mark()
	run.click_control(item, window, controls, METHOD_SWITCHES[method], f"the switch of method {method}")
	chosen = run.wait(rf"ZSETTINGS LANGUAGES ime method={method}", mark, 10)
	desktop = run.wait(rf"ZWL IME method={method}", mark, 10)
	item.step(f"chose input method {method}", f"{chosen}; {desktop}")
	item.check(chosen and desktop, f"input method {method} was not taken")
	run.stop_programs()


def to_language(run, item, language: str) -> None:
	"""Presses Alt+Space until the input method's language is the one asked (three tries)."""
	for _ in range(3):
		mark = run.mark()
		run.key("alt+space")
		line = run.wait(r"ZWL IME language=\S+", mark, 5)
		if line and aatlib.field(line, "language") == language:
			item.step(f"Alt+Space to {language}", line)
			return
		time.sleep(0.3)
	item.failed(f"Alt+Space did not reach the language {language}")
