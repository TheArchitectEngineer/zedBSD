#!/usr/bin/env python3
"""The automatic helper of tests/scenarios/apps/photos/browse.md (WS157 p005).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    helpers_photos.py --outdir OUTDIR [--only REGEX] -- TARGET-OPTIONS     (--list: the ids)

The photos are the ones plan/ws157/tests/make-photos.py --view makes on this host (PIL): nine pictures over three
months of 2026, a JPEG turned by its EXIF, a PNG, a GIF and one damaged JPEG.  They are put in kei's ~/AATPhotos and
imported with /bin/photos --import (ws157-p004: copied to ~/Pictures/Library/img/YYYY/MM/DD, the database in
~/Pictures/Library/db).  The library, the thumbnails' cache and ~/AATPhotos are taken away before and after.  The
places in Photos' window are view.c's layout: the lists' column 0.24 of the window's width within 200 and 260, the
grid's card 8 to its right on glass, the cells 20 in from the card's left, 116 down for the first month's row, four
columns or more of about 150 square 6 apart; the card of Add to Album 360 wide in the middle, 160 high without albums,
its name field 56 down and 16 in.
"""
import pathlib
import re
import subprocess
import sys

import aatlib
import common

run = aatlib.Run.from_command_line("helpers_photos")

SOURCE = "/home/kei/AATPhotos"
LIBRARY = "/home/kei/Pictures/Library"
CACHE = "/home/kei/.cache/keiland/photos"
MAKER = pathlib.Path(__file__).resolve().parents[4] / "plan/ws157/tests/make-photos.py"


def sized(window, mark):
	"""A window started outside App Home, its size taken from Photos' READY line (the compositor's lines give only
	its place)."""
	regex = r"PHOTOS READY width=(\d+) height=(\d+)"
	if window is None or window.sized():
		return window
	ready = run.wait(regex, mark, 5)
	if not ready:
		return window
	width, height = (int(value) for value in re.search(regex, ready).groups())
	return aatlib.Window(window.client, window.surface, window.x, window.y, width, height, window.docked)


def put_photos(item) -> None:
	"""The photos, made on this host and put in kei's ~/AATPhotos; no library yet."""
	folder = run.outdir / "photos"
	subprocess.run([sys.executable, str(MAKER), "--view", str(folder)], check=True, timeout=120)
	run.sh(f"rm -rf {SOURCE} {LIBRARY} {CACHE}; mkdir -p {SOURCE}/Family {SOURCE}/Trips")
	names = []
	for path in sorted(folder.rglob("*")):
		if path.is_file():
			name = path.relative_to(folder).as_posix()
			run.aat("put", str(path), f"{SOURCE}/{name}")
			names.append(name)
	# The files' times are the dates of the PNG, the GIF and the damaged JPEG (no EXIF).
	run.sh(f"cd {SOURCE} && touch -t 202607141200 sticker.png && touch -t 202607101200 wave.gif && "
		f"touch -t 202607011200 broken.jpg; chown -R kei {SOURCE}; chmod -R a+rX {SOURCE}")
	item.step(f"{len(names)} files in ~/AATPhotos", ", ".join(names))


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
		# The import before the window: nine photos copied, the database written.
		mark = run.mark()
		window = run.open_as_user(item, f"/bin/photos --import={SOURCE}", ready=r"PHOTOS READY ", timeout=40)
		window = sized(window, mark)
		imported = run.wait(r"PHOTOS IMPORT done source=\S+ imported=\d+ duplicates=\d+ failed=\d+ error=\d+ save=\d+", mark, 30)
		library = run.wait(r"PHOTOS LIBRARY root=\S+ photos=\d+ albums=\d+ error=\d+", mark, 10)
		glass = run.wait(r"PHOTOS GLASS see_through=\d", mark, 5)
		broken = run.wait(r"PHOTOS THUMB photo=8 error=\d+", mark, 30)
		_, files = run.sh(f"cd {LIBRARY} && ls img/2026/09/20 img/2026/07/14 db/photos")
		item.step("imported and opened", f"{imported}; {library}; {glass}; {broken}; {' '.join(files.split())}")
		run.shot(item, "timeline")
		item.check(imported and imported.endswith("imported=9 duplicates=0 failed=0 error=0 save=0"), "the nine photos were not imported")
		item.check("lake.jpg" in files and "sticker.png" in files and "2026-09.tsv" in files and "2026-07.tsv" in files,
			"the files and the database are not where they belong")
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
		# Right, R, F: the next photo, turned, a favourite, in its month's line.
		mark = run.mark()
		run.key("right")
		following = run.wait(r"PHOTOS OPEN photo=2 name=sea\.jpg", mark, 5)
		run.wait(r"PHOTOS PICTURE photo=2 error=0", mark, 15)
		run.key("r")
		turned = run.wait(r"PHOTOS TURN photo=2 turns=1", mark, 5)
		run.key("f")
		marked = run.wait(r"PHOTOS FAVORITE photo=2 on=1", mark, 5)
		saved = run.wait(r"PHOTOS SAVE error=\d+", mark, 5)
		_, line = run.sh(f"grep 'sea.jpg' {LIBRARY}/db/photos/2026-08.tsv")
		item.step("Right, R, F", f"{following}; {turned}; {marked}; {saved}; {line.strip()}")
		run.shot(item, "turned")
		item.check(following and turned and marked, "Right, R or F did not do its part")
		item.check(saved and saved.endswith("error=0") and "\t1\t1\tsea.jpg" in line, "the line of 2026-08.tsv does not hold the favourite and the turn")
		# A: the card; a new album's name typed and Enter.
		mark = run.mark()
		run.key("a")
		card = run.wait(r"PHOTOS CARD album photo=2", mark, 5)
		top = window.y + (window.height - 160) // 2
		run.click(window.x + (window.width - 360) // 2 + 16 + 100, top + 56 + 15)
		run.type("Sea")
		run.key("enter")
		created = run.wait(r"PHOTOS ALBUM create name=Sea error=0", mark, 5)
		added = run.wait(r"PHOTOS ALBUM add album=Sea photo=2 error=0", mark, 5)
		saved = run.wait(r"PHOTOS SAVE error=\d+", mark, 5)
		_, album = run.sh(f"cat {LIBRARY}/db/albums/*.album")
		item.step("A, Sea, Enter", f"{card}; {created}; {added}; {saved}; {' | '.join(album.splitlines())}")
		item.check(card and created and added, "the album was not made with the photo")
		item.check("name\tSea" in album and album.count("photo\t") == 1, "the album's file does not hold the name and the photo")
		# Escape: the grid.  The slideshow: Space, two photos on, Space.
		run.key("esc")
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
		# Open again: the library read back, the album in the list; the thumbnails from the cache.
		mark = run.mark()
		window = run.open_as_user(item, f"/bin/photos --import={SOURCE}", ready=r"PHOTOS READY ", timeout=40)
		window = sized(window, mark)
		again = run.wait(r"PHOTOS IMPORT done source=\S+ imported=\d+ duplicates=\d+", mark, 30)
		library = run.wait(r"PHOTOS LIBRARY root=\S+ photos=\d+ albums=\d+", mark, 10)
		run.click(window.x + min(max(int(window.width * 0.24), 200), 260) // 2, window.y + 212)
		listed = run.wait(r"PHOTOS LIST list=2 album=0", mark, 5)
		_, cached = run.sh(f"ls {CACHE} | grep -c ppm")
		item.step("opened again, the album", f"{again}; {library}; {listed}; cache {cached.strip()}")
		run.shot(item, "album")
		item.check(again and "imported=0 duplicates=9" in again, "the same photos were imported again")
		item.check(library and "photos=9 albums=1" in library and listed, "the library or the album was not read back")
		item.check(cached.strip() == "8", "the thumbnails were not kept in the cache")
		run.close(item, window)
		item.person("the timeline by month (a grey square for the damaged file), hill.jpg upright, sea.jpg turned with "
			"Unfavorite, the slideshow, the album Sea with sea.jpg and its heart")
	finally:
		run.sh(f"rm -rf {SOURCE} {LIBRARY} {CACHE}")


sys.exit(run.go(before=common.before(run), after=common.after(run)))
