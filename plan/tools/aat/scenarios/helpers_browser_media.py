#!/usr/bin/env python3
"""The automatic helper of tests/scenarios/apps/browser/video.md (WS121 p004 to p006).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    helpers_browser_media.py --outdir OUTDIR [--only REGEX] -- TARGET-OPTIONS     (--list: the ids)

The video is the runner's sample (/tmp/aat-samples/sample.mp4: 12 s, MPEG-4 Part 2 640x360 and AAC); the page is
written here and put in /tmp/aat-work.  The browser's libmedia lines are "BROWSER MEDIA" lines.  The page has no
style: the video is at the body's margin (8, 8) of the page, which starts at the window's body.
"""
import re
import sys
import time

import aatlib
import common

run = aatlib.Run.from_command_line("helpers_browser_media")

PAGE = f"{aatlib.WORK}/video.html"


@run.define("apps.browser.video")
def video(item):
	# The page: the sample with controls, with sound (not muted).
	page = run.outdir / "video.html"
	page.write_text("<!doctype html><title>Video</title>"
		f"<video id=v src=\"{aatlib.SAMPLES}/sample.mp4\" controls></video><p>Under the video.</p>\n")
	run.aat("put", str(page), PAGE)
	run.sh(f"chmod 644 {PAGE}")
	mark = run.mark()
	window = run.open_as_user(item, f"/bin/browser {PAGE}")
	opened = run.wait(r"BROWSER MEDIA MEDIA open width=640 height=360 .*", mark, 20)
	item.step("the page with the video", opened or "")
	item.check(opened and "video=mpeg4" in opened, "the video did not open")
	item.check(opened and "audio=aac" in opened, "the video's sound did not open (audiod)")
	time.sleep(1.0)
	run.shot(item, "paused")
	# A click on the video plays it; a few seconds later another pauses it.
	x = window.x + 8 + 320
	y = window.y + 8 + 160
	mark = run.mark()
	run.click(x, y)
	played = run.wait(r"BROWSER MEDIA MEDIA play position_ms=\d+", mark, 10)
	time.sleep(3.0)
	run.shot(item, "playing")
	mark = run.mark()
	run.click(x, y)
	paused = run.wait(r"BROWSER MEDIA MEDIA pause position_ms=\d+", mark, 10)
	item.step("click, 3 s, click", f"{played}; {paused}")
	item.check(played, "the click did not play the video")
	position = int(re.search(r"position_ms=(\d+)", paused).group(1)) if paused else 0
	item.check(paused and position >= 2000, f"the video did not play on and pause ({position} ms)")
	run.close(item, window)
	item.person("the paused video with its bar (a play sign), the playing video with the pause sign and the time moved; "
		"the sound is heard on hardware only")


sys.exit(run.go(before=common.before(run), after=common.after(run)))
