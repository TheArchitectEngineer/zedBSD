#!/usr/bin/env python3
"""The automatic helpers of tests/scenarios/apps/calendar/ beyond open-from-home and navigate (WS155 p003, p004).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    helpers_calendar.py --outdir OUTDIR [--only REGEX] -- TARGET-OPTIONS     (--list: the ids)

Calendar keeps its events under ~/Documents/Calendar as iCalendar files (one each); the helper takes kei's folder away
before and after.  The places in the window are view.c's layout, from the window's right edge: the panel 300 wide,
its Work card at (right - 220, 146), the editor's Save at (right - 54, 274).  Today's cell is the one the view logs
(CALENDAR CELL today x= y= width= height=, in the window).  The system bar's clock is at the screen's top right.
"""
import re
import sys
import time

import aatlib
import common

run = aatlib.Run.from_command_line("helpers_calendar")

FOLDER = "/home/kei/Documents/Calendar"


@run.define("apps.calendar.event")
def event(item):
	run.sh(f"rm -rf {FOLDER}")
	try:
		mark = run.mark()
		window = run.launch(item, "Calendar")
		cell = run.wait(r"CALENDAR CELL today x=\d+ y=\d+ width=\d+ height=\d+", mark, 10)
		item.check(cell, "the view did not say where today is")
		x, y, width, height = (int(value) for value in re.search(r"x=(\d+) y=(\d+) width=(\d+) height=(\d+)", cell).groups())
		right = window.x + window.width
		# The Work card dragged onto today: the editor opens, Save keeps the event.
		mark = run.mark()
		run.drag(right - 220, window.y + 146, window.x + x + width // 2, window.y + y + height // 2, 20)
		opened = run.wait(r"CALENDAR EDIT new list=Work", mark, 10)
		item.step("dragged Work onto today", opened or "")
		run.shot(item, "editor")
		item.check(opened, "the editor did not open")
		mark = run.mark()
		run.click(right - 54, window.y + 274)
		saved = run.wait(r"CALENDAR EVENT saved index=\d+ list=Work .* error=0", mark, 10)
		item.step("Save", saved or "")
		time.sleep(0.5)
		run.shot(item, "saved")
		item.check(saved, "the event was not saved")
		status, found = run.sh(f"grep -l 'SUMMARY:New Work Event' {FOLDER}/Work/*.ics | wc -l")
		item.check(found.strip() == "1", f"the event is not in its file ({found.strip()})")
		# Closed, then opened again from the system bar's clock.
		run.close(item, window)
		screen_width, _ = run.screen()
		mark = run.mark()
		run.click(screen_width - 60, 15)
		opened = run.wait(r"KWL HOME open name=Calendar via=clock", mark, 10)
		# The window's READY (with its size; "READY today=" comes before the window) and its map (T1-320's shot,
		# taken after the first, was of the bare desktop).
		ready = run.wait(r"CALENDAR READY width=", mark, 15)
		run.wait(r"KWL MAP client=\d+ surface=\d+", mark, 10)
		item.step("clicked the clock", f"{opened}; {ready}")
		time.sleep(1.0)
		run.shot(item, "from-clock")
		item.check(opened and ready, "the clock did not open Calendar")
		item.person("the editor, the event on today's cell, and Calendar opened from the clock")
	finally:
		run.sh(f"rm -rf {FOLDER}")


sys.exit(run.go(before=common.before(run), after=common.after(run)))
