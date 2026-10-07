#!/usr/bin/env python3
"""The automatic helper of tests/scenarios/apps/photos/browse.md (WS157 p003).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    helpers_photos.py --outdir OUTDIR [--only REGEX] -- TARGET-OPTIONS     (--list: the ids)

The photos are the ones plan/ws157/tests/make-photos.py --view makes on this host (PIL): nine pictures over three
months of 2026, in the albums Family and Trips and loose, a JPEG turned by its EXIF, a PNG, a GIF and one damaged
JPEG.  They go in kei's ~/Pictures and are taken away after, with Photos' marks.  The places in Photos' window are
view.c's layout: the lists' column 0.24 of the window's width within 200 and 260, the grid's card 8 to its right on
glass, the cells 20 in from the card's left, 116 down for the first month's row, four columns or more of about 150
square 6 apart.
"""
import pathlib
import subprocess
import sys

import aatlib
import common

run = aatlib.Run.from_command_line("helpers_photos")

PICTURES = "/home/kei/Pictures"
MARKS = "/home/kei/.config/keiland/photos.conf"
MAKER = pathlib.Path(__file__).resolve().parents[4] / "plan/ws157/tests/make-photos.py"


def put_photos(item) -> None:
	"""The photos, made on this host and put in kei's Pictures folder."""
	folder = run.outdir / "photos"
	subprocess.run([sys.executable, str(MAKER), "--view", str(folder)], check=True, timeout=120)
	run.sh(f"rm -rf {PICTURES} {MARKS}; mkdir -p {PICTURES}/Family {PICTURES}/Trips")
	names = []
	for path in sorted(folder.rglob("*")):
		if path.is_file():
			name = path.relative_to(folder).as_posix()
			run.aat("put", str(path), f"{PICTURES}/{name}")
			names.append(name)
	# The files' times are the dates of the PNG, the GIF and the damaged JPEG (no EXIF).
	run.sh(f"cd {PICTURES} && touch -t 202607141200 sticker.png && touch -t 202607101200 wave.gif && "
		f"touch -t 202607011200 broken.jpg; chown -R kei {PICTURES}; chmod -R a+rX {PICTURES}")
	item.step(f"{len(names)} files in ~/Pictures", ", ".join(names))


def cell(window, gap: int, index: int, row_y: int) -> tuple[int, int]:
	"""The middle of the cell of a column in a row of the grid (view.c's view_grid_columns)."""
	share = min(max(int(window.width * 0.24), 200), 260)
	inner = window.width - share - gap - 40
	columns = max(1, (inner + 6) // 156)
	side = (inner - (columns - 1) * 6) // columns
	x = window.x + share + gap + 20 + index * (side + 6) + side // 2
	return x, window.y + row_y + side // 2


@run.define("apps.photos.browse")
def browse(item):
	try:
		put_photos(item)
		mark = run.mark()
		window = run.launch(item, "Photos")
		library = run.wait(r"PHOTOS LIBRARY photos=\d+ albums=\d+ error=\d+", mark, 10)
		glass = run.wait(r"PHOTOS GLASS see_through=\d", mark, 5)
		broken = run.wait(r"PHOTOS THUMB photo=8 error=\d+", mark, 30)
		item.step("opened", f"{library}; {glass}; {broken}")
		run.shot(item, "timeline")
		item.check(library and library.endswith("photos=9 albums=2 error=0"), "the nine photos and two albums are not there")
		item.check(broken, "the damaged file's thumbnail did not fail (or the thumbnails were not made)")
		gap = 8 if glass and glass.endswith("=1") else 0
		# A double click on the second photo of September (hill.jpg, turned by its EXIF) shows it whole.
		mark = run.mark()
		x, y = cell(window, gap, 1, 116)
		run.click(x, y, "--count", "2")
		opened = run.wait(r"PHOTOS OPEN photo=1 name=hill\.jpg", mark, 10)
		picture = run.wait(r"PHOTOS PICTURE photo=1 error=\d+ width=\d+ height=\d+", mark, 15)
		item.step(f"double-clicked {x},{y}", f"{opened}; {picture}")
		run.shot(item, "whole")
		item.check(opened, "the photo was not shown whole")
		item.check(picture and picture.endswith("error=0 width=640 height=480"), "its picture was not made upright (640 by 480)")
		# Right, R, F: the next photo, turned, a favourite, kept.
		mark = run.mark()
		run.key("right")
		following = run.wait(r"PHOTOS OPEN photo=2 name=sea\.jpg", mark, 5)
		run.wait(r"PHOTOS PICTURE photo=2 error=0", mark, 15)
		run.key("r")
		turned = run.wait(r"PHOTOS TURN photo=2 turns=1", mark, 5)
		run.key("f")
		marked = run.wait(r"PHOTOS FAVORITE photo=2 on=1", mark, 5)
		saved = run.wait(r"PHOTOS SAVE error=\d+", mark, 5)
		item.step("Right, R, F", f"{following}; {turned}; {marked}; {saved}")
		run.shot(item, "turned")
		item.check(following and turned and marked, "Right, R or F did not do its part")
		item.check(saved and saved.endswith("error=0"), "the marks were not saved")
		_, kept = run.sh(f"cat {MARKS}")
		item.step("photos.conf", kept.strip())
		item.check("favorite /home/kei/Pictures/Trips/sea.jpg" in kept and "turn 1 /home/kei/Pictures/Trips/sea.jpg" in kept,
			"photos.conf does not hold the favourite and the turn")
		# Escape: the grid, the photo chosen, its heart.
		mark = run.mark()
		run.key("esc")
		back = run.wait(r"PHOTOS BACK photo=2", mark, 5)
		item.step("Escape", back or "")
		item.check(back, "Escape did not go back to the grid")
		# The slideshow: Space, two photos on in about six seconds, Space stops it.
		mark = run.mark()
		run.key("space")
		started = run.wait(r"PHOTOS SLIDESHOW on=1 photo=2", mark, 5)
		second = run.wait(r"PHOTOS SLIDE photo=4", mark, 10)
		run.shot(item, "slideshow")
		run.key("space")
		stopped = run.wait(r"PHOTOS SLIDESHOW on=0", mark, 5)
		item.step("Space, waited, Space", f"{started}; {second}; {stopped}")
		item.check(started and second and stopped, "the slideshow did not start, go on and stop")
		run.key("esc")
		run.close(item, window)
		# Open again: the marks come back.
		mark = run.mark()
		window = run.open_as_user(item, "/bin/photos", ready=r"PHOTOS READY ")
		marks = run.wait(r"PHOTOS MARKS error=\d+", mark, 10)
		run.click(window.x + min(max(int(window.width * 0.24), 200), 260) // 2, window.y + 128)
		listed = run.wait(r"PHOTOS LIST list=1 album=0", mark, 5)
		item.step("opened again, Favorites", f"{marks}; {listed}")
		run.shot(item, "favorites")
		item.check(marks and marks.endswith("error=0") and listed, "the marks were not read, or Favorites did not show")
		run.close(item, window)
		item.person("the timeline by month (a grey square for the damaged file), hill.jpg upright, sea.jpg turned with "
			"Unfavorite, the slideshow, Favorites with sea.jpg and its heart")
	finally:
		run.sh(f"rm -rf {PICTURES} {MARKS}")


sys.exit(run.go(before=common.before(run), after=common.after(run)))
