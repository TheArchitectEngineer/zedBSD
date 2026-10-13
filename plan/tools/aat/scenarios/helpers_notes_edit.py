#!/usr/bin/env python3
"""The automatic helpers of Notes' PDF editing (tests/scenarios/apps/notes/pdf-*.md, ws175-p010) and of its drawing
tools (draw-tools.md and fill-region.md, ws216-p007).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    helpers_notes_edit.py --outdir OUTDIR [--only REGEX] -- TARGET-OPTIONS     (--list: the ids)

Each scenario gets a folder of its own on the target, /tmp/aat-work/notes-ID-PID/, with the document
(edit-basic.pdf, which plan/ws175/tests/make-edit-samples.py writes on the host) as notes-edit.pdf and the
pictures (picture.png with alpha, landscape.jpg; PIL on the host) beside it, so that Notes' journal of
another run is never recovered and the file chooser finds the pictures by their first letter.  Notes is
started as the session's user with the document; its places come from its own lines: NOTES LAYOUT (the
page in the window), NOTES BUTTONS (the toolbar's buttons by action), NOTES TEXT box open (the text box),
and the window's place from the compositor's.  The saved document is taken to the host and read with
qpdf, pdftotext, pdffonts and pdfimages.  What only a person can judge (how the page looks) is left to
the screenshots (needs-person).

The drawing tools' strokes are drawn with the test pen (peninject, in the image of plan/ws216/tests/config.mk), so that
they carry a pressure.  The injector admits four opens and aat-input holds all four, so the helper stops aat-input,
replays the pen's script and starts aat-input again (Pen.draw).
"""
import io
import math
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path

import aatlib
import common

run = aatlib.Run.from_command_line("helpers_notes_edit")

SAMPLES = aatlib.ROOT / "plan/ws175/tests/make-edit-samples.py"

# Notes' toolbar actions (userland/desktop/notes/app.h).
SELECT = 16
INSERT_IMAGE = 17
REPLACE_IMAGE = 18
DELETE_OBJECT = 19
RESET_OBJECT = 40
TEXT = 41
EDIT_TEXT = 42
FONT = 43
PEN = 1
# ws216-p006: the drawing tool button (the drawing tool used last, or its strip of drawing tools).
DRAW = 48
# ws216: the strip's drawing tools, the highlighter (Marker), the colours and widths by index, the eraser.
HIGHLIGHTER = 2
ERASER = 3
BRUSH = 49
COLOR = 20
WIDTH = 30

# The model's tool of a stroke (NOTES STROKE tool=, userland/desktop/notes/notes.h NOTES_TOOL_*).
STROKE_PEN = 0
STROKE_HIGHLIGHTER = 1
STROKE_BRUSH = 2
STROKE_PENCIL = 3
STROKE_WATERCOLOR = 4

# The test pen's script (peninject): its declared range, its full pressure, how long the compositor is given to find
# the new device, and the pause between samples.
PEN_RANGE = 32767
PEN_PRESSURE_MAX = 4095
PEN_SETTLE_MS = 3000
PEN_STEP_MS = 15
# A stroke whose highest pressure is above this (of NOTES_PRESSURE_MAX 65535) had the pen's pressure.
PEN_PRESSED = 30000

# How far below the window's top the page is clear of the toolbar, its band of notices and the strip of drawing tools
# (app.h NOTES_TOOLBAR_HEIGHT 68, ui.c UI_STRIP_TOP 70 and UI_STRIP_HEIGHT 40: the strip ends at 110).
TOOLBAR_CLEAR = 140

# edit-basic.pdf (make-edit-samples.py's BASIC_*): US Letter, the paragraph's baselines from 700 down by 20 at x 72,
# 14 points; the image's box (x, y, width, height) in PDF points.
PAGE_HEIGHT = 792
LEFT = 72
TOP = 700
STEP = 20
IMAGE = (72, 420, 240, 150)
# The white square inside the image (its pixels 130..190 by 70..130 of 320 by 200, make-edit-samples.py), a gradient round
# it: its middle in PDF points, a place the fill tool closes in (ws216-p007).
PICTURE_SQUARE = (72 + 160 * 240 / 320, 420 + 150 - 100 * 150 / 200)


class Notes:
	"""Notes on a document of the scenario's own folder: its window and its lines since it started."""

	def __init__(self, item, folder: str, name: str = "notes-edit.pdf", opened: str = r"NOTES OPEN pages=\d+"):
		"""Starts Notes on folder/name; opened is the line that says the document is there (NOTES START for a new
		notebook, whose file is not there yet)."""
		self.item = item
		self.folder = folder
		self.path = f"{folder}/{name}"
		self.start = run.mark()
		self.window = run.open_as_user(item, f"/bin/notes {shlex.quote(self.path)}", ready=r"NOTES FRAME first")
		self.opened = run.wait(opened, self.start, 10)
		item.step(f"Notes opened {self.path}", self.opened)
		item.check(self.opened, "Notes did not open the document")

	def layout(self) -> tuple[float, float, float]:
		"""The page's top left in the window and its pixels per point, from the last NOTES LAYOUT line."""
		lines = run.lines(r"NOTES LAYOUT window=", self.start)
		self.item.check(lines, "no NOTES LAYOUT line")
		match = re.search(r"page=(-?\d+),(-?\d+),\d+,\d+ scale=([0-9.]+)", lines[-1])
		return float(match.group(1)), float(match.group(2)), float(match.group(3))

	def at(self, x: float, y: float) -> tuple[int, int]:
		"""The screen's point of a point of the page shown (points, the top left the origin)."""
		left, top, scale = self.layout()
		return int(self.window.x + left + x * scale), int(self.window.y + top + y * scale)

	def at_pdf(self, x: float, y: float) -> tuple[int, int]:
		"""The screen's point of a point of an unrotated page's PDF space (points, y upward)."""
		return self.at(x, PAGE_HEIGHT - y)

	def scale(self) -> float:
		"""The page's pixels per point."""
		return self.layout()[2]

	def area(self) -> tuple[int, int, int, int]:
		"""The part of the page shown, on the screen (left, top, right, bottom): inside the window, below the toolbar's
		band and a margin in from the window's edges (NOTES LAYOUT window=WxH page=X,Y,W,H)."""
		lines = run.lines(r"NOTES LAYOUT window=", self.start)
		self.item.check(lines, "no NOTES LAYOUT line")
		match = re.search(r"window=(\d+)x(\d+) page=(-?\d+),(-?\d+),(\d+),(\d+)", lines[-1])
		width, height, left, top, page_width, page_height = (int(value) for value in match.groups())
		inside = (max(left, 24), max(top, TOOLBAR_CLEAR), min(left + page_width, width - 24), min(top + page_height, height - 24))
		self.item.check(inside[2] - inside[0] > 200 and inside[3] - inside[1] > 200, f"too little of the page is shown: {lines[-1]}")
		return (self.window.x + inside[0], self.window.y + inside[1], self.window.x + inside[2], self.window.y + inside[3])

	def button(self, action: int) -> tuple[int, int]:
		"""The middle of a toolbar button, from the last NOTES BUTTONS line that has it."""
		for line in reversed(run.lines(r"NOTES BUTTONS", self.start)):
			match = re.search(rf" {action}:(-?\d+),(-?\d+),(\d+),(\d+)", line)
			if match:
				x, y, width, height = (int(value) for value in match.groups())
				return self.window.x + x + width // 2, self.window.y + y + height // 2
		self.item.failed(f"the toolbar has no button of action {action}")

	def press(self, action: int, wait: str | None = None, what: str = "") -> str | None:
		"""Presses a toolbar button and waits for a line after it."""
		mark = run.mark()
		run.click(*self.button(action))
		line = run.wait(wait, mark, 10) if wait else None
		time.sleep(0.5)
		self.item.step(f"pressed the toolbar's {what or action}", line or "")
		if wait:
			self.item.check(line, f"no line {wait!r} after the toolbar's {what or action}")
		return line

	def pen(self) -> str | None:
		"""Chooses the pen with its key P (ws216-p006: the toolbar's Pen is on the strip of drawing tools now, whose
		buttons are logged only once the strip is drawn; the key is one step and needs no place)."""
		return self.act(lambda: run.key("p"), r"NOTES TOOL 1 name=pen", "the key P (the pen)")

	def act(self, action, wait: str, what: str, timeout: float = 10) -> str:
		"""Does something (a function of no arguments) and waits for a line after it."""
		mark = run.mark()
		action()
		line = run.wait(wait, mark, timeout)
		time.sleep(0.4)
		self.item.step(what, line or "")
		self.item.check(line, f"{what}: no line {wait!r}")
		return line

	def close(self) -> None:
		"""Closes Notes with Ctrl+W (it saves first) and waits for it to end."""
		mark = run.mark()
		run.key("ctrl+w")
		ended = run.wait(r"NOTES EXIT ", mark, 20)
		self.item.step("Ctrl+W", ended or "")
		self.item.check(ended, "Notes did not end at Ctrl+W")
		time.sleep(0.5)


def prepare(item, pictures: bool = False) -> str:
	"""The scenario's folder on the target with the document (and the pictures); returns the folder."""
	local = run.outdir / "notes-edit" / item.ident
	local.mkdir(parents=True, exist_ok=True)
	made = subprocess.run([sys.executable, str(SAMPLES), str(local)], capture_output=True, text=True, timeout=120)
	item.check(made.returncode == 0, f"make-edit-samples.py: {made.stderr.strip()[-200:]}")
	files = {"notes-edit.pdf": local / "edit-basic.pdf"}
	if pictures:
		files.update(make_pictures(local))
	folder = f"{aatlib.WORK}/notes-{item.ident.split('.')[-1]}-{int(time.time())}"
	run.sh(f"mkdir -p {folder} && chmod 755 {folder}")
	for name, path in files.items():
		run.aat("put", str(path), f"{folder}/{name}")
	run.sh(f"chown -R {aatlib.USER} {folder} && chmod 644 {folder}/*")
	item.step(f"put {', '.join(files)} in {folder}")
	return folder


def make_pictures(local: Path) -> dict[str, Path]:
	"""picture.png (RGBA, its right half see-through by half) and landscape.jpg, written on the host."""
	from PIL import Image, ImageDraw

	png = Image.new("RGBA", (160, 100), (40, 120, 220, 255))
	draw = ImageDraw.Draw(png)
	draw.rectangle([80, 0, 159, 99], fill=(220, 60, 40, 128))
	draw.ellipse([20, 20, 60, 60], fill=(255, 255, 255, 255))
	png.save(local / "picture.png")
	jpeg = Image.new("RGB", (200, 120), (30, 140, 60))
	ImageDraw.Draw(jpeg).rectangle([60, 30, 140, 90], fill=(250, 220, 40))
	jpeg.save(local / "landscape.jpg", quality=90)
	return {"picture.png": local / "picture.png", "landscape.jpg": local / "landscape.jpg"}


def choose(item, notes: Notes, action: int, letter: str, mode: str) -> None:
	"""Presses Image or Replace and picks the file whose name starts with letter in the chooser."""
	notes.press(action, rf"NOTES CHOOSER open mode={mode}", "Image" if action == INSERT_IMAGE else "Replace")
	time.sleep(1.5)
	run.shot(item, f"chooser-{mode}")
	run.type(letter)
	time.sleep(0.4)
	run.key("enter")


def fetch(item, notes: Notes, name: str) -> Path:
	"""Takes the document from the target to the host."""
	local = run.outdir / "notes-edit" / item.ident / name
	run.aat("get", notes.path, str(local))
	item.step(f"took {notes.path} to the host", str(local))
	return local


def host(item, *command: str) -> str:
	"""Runs a command of the host on a file taken back; returns its output (with its errors)."""
	done = subprocess.run(list(command), capture_output=True, text=True, timeout=120)
	output = (done.stdout + done.stderr).strip()
	item.step(" ".join(Path(word).name if "/" in word else word for word in command), output[-300:])
	return output


def line_y(index: int) -> float:
	"""The PDF y of a point on the paragraph's line (0 the first), a little above its baseline."""
	return TOP - STEP * index + 4


@run.define("apps.notes.pdf-edit-image")
def pdf_edit_image(item):
	folder = prepare(item, pictures=True)
	notes = Notes(item, folder)
	notes.press(SELECT, r"NOTES TOOL 16 name=select", "Select")
	run.shot(item, "select-tool")

	# The JPEG chosen at its middle.
	left, bottom, width, height = IMAGE
	middle = notes.at_pdf(left + width / 2, bottom + height / 2)
	chosen = notes.act(lambda: run.click(*middle), r"NOTES EDIT select page=0 object=\d+ kind=image", "clicked the image's middle")
	run.shot(item, "selected")
	item.check("inserted=0" in chosen, "the page's image was taken for an inserted one")

	# Moved 100 pixels right and down.
	moved = notes.act(lambda: run.drag(middle[0], middle[1], middle[0] + 100, middle[1] + 100), r"NOTES EDIT move page=0 object=\d+",
		"dragged the image 100 px right and down")
	run.shot(item, "moved")

	# Sized by its bottom right handle, 80 pixels out (the handle where the move left it).
	dx = float(aatlib.field(moved, "dx") or 0)
	dy = float(aatlib.field(moved, "dy") or 0)
	handle = notes.at(left + width + dx, PAGE_HEIGHT - bottom + dy)
	sized = notes.act(lambda: run.drag(handle[0], handle[1], handle[0] + 80, handle[1] + 80), r"NOTES EDIT resize page=0 object=\d+",
		"dragged the bottom right handle 80 px out")
	run.shot(item, "resized")
	sx = float(aatlib.field(sized, "sx") or 0)
	sy = float(aatlib.field(sized, "sy") or 0)
	item.check(sx > 1.0 and abs(sx - sy) < 1e-3, f"the resize was not proportional and larger: {sized}")

	# Replaced by picture.png.
	mark = run.mark()
	choose(item, notes, REPLACE_IMAGE, "p", "replace")
	replaced = run.wait(r"NOTES EDIT replace page=0 object=\d+ image=160x100", mark, 15)
	item.step("chose picture.png", replaced or "")
	run.shot(item, "replaced")
	item.check(replaced, "no NOTES EDIT replace with picture.png")

	# Undo, redo, then the image chosen again and deleted.
	notes.act(lambda: run.key("ctrl+z"), r"NOTES EDIT undo page=0", "Ctrl+Z")
	run.shot(item, "undo")
	notes.act(lambda: run.key("ctrl+shift+z"), r"NOTES EDIT redo page=0", "Ctrl+Shift+Z")
	middle = notes.at(left + (width * sx) / 2 + dx, PAGE_HEIGHT - bottom - height + (height * sy) / 2 + dy)
	# The redo chose the image again (app_reselect), so a click on it logs no new select; the click only makes sure.
	run.click(*middle)
	time.sleep(0.4)
	item.step("clicked the image (chosen again by the redo)")
	notes.act(lambda: run.key("delete"), r"NOTES EDIT delete page=0 object=\d+", "Delete")
	run.shot(item, "deleted")

	# Saved, and read on the host.
	saved = notes.act(lambda: run.key("ctrl+s"), r"NOTES SAVE reason=request .*edits=\d+ edited_pages=1 ", "Ctrl+S")
	notes.close()
	path = fetch(item, notes, "saved.pdf")
	check = host(item, "qpdf", "--check", str(path))
	item.check("No syntax or stream encoding errors" in check, "qpdf --check found errors")
	original = (run.outdir / "notes-edit" / item.ident / "edit-basic.pdf").read_bytes()
	item.check(path.read_bytes()[:len(original)] == original, "the saved file does not start with the original's bytes")
	images = host(item, "pdfimages", "-list", "-f", "1", "-l", "1", str(path))
	item.step("page 1's images after the delete", images)
	item.check(len([row for row in images.splitlines()[2:] if row.strip()]) == 0, "page 1 still draws an image")
	item.person(f"{saved}; the screenshots: the frame and handles, the moved, sized, replaced image, the undo and the delete")


@run.define("apps.notes.pdf-insert-image")
def pdf_insert_image(item):
	folder = prepare(item, pictures=True)
	notes = Notes(item, folder)
	notes.act(lambda: run.key("pagedown"), r"NOTES PAGE current=1 count=3", "Page Down")
	notes.press(SELECT, r"NOTES TOOL 16 name=select", "Select")

	# picture.png inserted, then moved up and left.
	mark = run.mark()
	choose(item, notes, INSERT_IMAGE, "p", "insert")
	first = run.wait(r"NOTES EDIT insert page=1 kind=image object=\d+ image=160x100", mark, 15)
	item.step("chose picture.png", first or "")
	run.shot(item, "inserted-png")
	item.check(first, "no NOTES EDIT insert of picture.png")
	middle = notes.at(612 / 2, 792 / 2)
	notes.act(lambda: run.drag(middle[0], middle[1], middle[0] - 120, middle[1] - 120), r"NOTES EDIT move page=1 object=\d+",
		"dragged it up and left")

	# landscape.jpg inserted.
	mark = run.mark()
	choose(item, notes, INSERT_IMAGE, "l", "insert")
	second = run.wait(r"NOTES EDIT insert page=1 kind=image object=\d+ image=200x120", mark, 15)
	item.step("chose landscape.jpg", second or "")
	run.shot(item, "inserted-jpeg")
	item.check(second and aatlib.number(second, "object") != aatlib.number(first, "object"), "the JPEG was not a second object")

	# Saved, closed, opened again on page 2, the JPEG chosen.
	notes.act(lambda: run.key("ctrl+s"), r"NOTES SAVE reason=request .*edits=2 edited_pages=1 ", "Ctrl+S")
	notes.close()
	again = Notes(item, folder)
	edits = run.wait(r"NOTES EDITS opened edits=2 edited_pages=1 rebased=0", again.start, 10)
	item.step("opened again", f"{again.opened}; {edits}")
	item.check(edits and "kind=annotated" in again.opened, "the edits were not opened again")
	again.act(lambda: run.key("pagedown"), r"NOTES PAGE current=1 count=3", "Page Down")
	again.press(SELECT, None, "Select")
	again.act(lambda: run.click(*again.at(612 / 2, 792 / 2)), r"NOTES EDIT select page=1 object=\d+ kind=image inserted=1",
		"clicked the JPEG")
	run.shot(item, "reopened")
	again.close()

	# On the host: two images on page 2, the PNG's with an SMask.
	path = fetch(item, again, "saved.pdf")
	check = host(item, "qpdf", "--check", str(path))
	item.check("No syntax or stream encoding errors" in check, "qpdf --check found errors")
	images = host(item, "pdfimages", "-list", "-f", "2", "-l", "2", str(path))
	rows = [row.split() for row in images.splitlines()[2:] if row.strip()]
	item.check(len([row for row in rows if row[2] == "image"]) == 2, f"page 2 does not draw two images: {images}")
	item.check(any(row[2] == "smask" for row in rows), "the PNG has no SMask")
	item.person("the screenshots: the PNG in the middle, see-through on its right half, then moved; the JPEG; both after opening again")


@run.define("apps.notes.pdf-edit-text")
def pdf_edit_text(item):
	folder = prepare(item)
	notes = Notes(item, folder)
	notes.press(SELECT, r"NOTES TOOL 16 name=select", "Select")

	# The first line double-clicked: chosen, then its words in the box.
	point = notes.at_pdf(LEFT + 60, line_y(0))
	mark = run.mark()
	run.click(*point, "--count", "2")
	chosen = run.wait(r'NOTES EDIT select page=0 object=\d+ kind=text inserted=0 clipped=0 fixed=0 text="The quick brown fox', mark, 10)
	opened = run.wait(r"NOTES TEXT box open kind=line page=0 object=\d+ font=original", mark, 10)
	item.step("double-clicked the first line", f"{chosen}; {opened}")
	run.shot(item, "box")
	item.check(chosen and opened, "the first line's box did not open")
	first = aatlib.number(chosen, "object")

	# "dog" made "fox": shown on the page as typed, then kept in the line's own font.
	run.key("end", "backspace", "backspace", "backspace")
	run.type("fox")
	time.sleep(0.6)
	run.shot(item, "typed")
	kept = notes.act(lambda: run.key("enter"), rf"NOTES EDIT text page=0 object={first} kind=line chars=43 font=original fallback=0",
		"Enter")
	run.shot(item, "fox")

	# The second line given capitals its font lacks: a replacement font, and the status says so.
	point = notes.at_pdf(LEFT + 60, line_y(1))
	mark = run.mark()
	run.click(*point, "--count", "2")
	opened = run.wait(r"NOTES TEXT box open kind=line page=0", mark, 10)
	item.step("double-clicked the second line", opened or "")
	item.check(opened, "the second line's box did not open")
	run.key("end")
	mark = run.mark()
	run.type(" ZEBRA")
	# The box has all six characters (the 39 bytes of the line and " ZEBRA") before the screenshot.
	typed = run.wait(r"NOTES TEXT box reported=\d+ bytes=45\b", mark, 10)
	if not typed:
		typed = run.wait(r"NOTES TEXT box (input|reported)", mark, 1)
	item.step("typed ZEBRA", typed or "no NOTES TEXT box line (the keys did not reach the box)")
	run.shot(item, "zebra-typed")
	mark = run.mark()
	run.key("esc")
	pattern = r"NOTES EDIT text page=0 object=\d+ kind=line .*font=original fallback=1"
	replaced = run.wait(pattern, mark, 5)
	if not replaced:
		# An input method's composition takes the first Esc; the second closes the box.
		run.key("esc")
		replaced = run.wait(pattern, mark, 5)
	item.step("Esc", replaced or "")
	item.check(replaced, f"Esc: no line {pattern!r}")
	run.shot(item, "zebra")

	# The third line deleted.
	notes.act(lambda: run.click(*notes.at_pdf(LEFT + 60, line_y(2))), r"NOTES EDIT select page=0 object=\d+ kind=text", "clicked the third line")
	notes.act(lambda: run.key("delete"), r"NOTES EDIT delete page=0 object=\d+", "Delete")
	run.shot(item, "deleted")

	# The fourth line in Sans.
	notes.act(lambda: run.click(*notes.at_pdf(LEFT + 60, line_y(3))), r"NOTES EDIT select page=0 object=\d+ kind=text", "clicked the fourth line")
	time.sleep(0.5)
	notes.press(FONT, r"NOTES EDIT font page=0 object=\d+ font=sans fallback=1", "Font")
	run.shot(item, "sans")

	# Saved, and read on the host.
	notes.act(lambda: run.key("ctrl+s"), r"NOTES SAVE reason=request ", "Ctrl+S")
	notes.close()
	path = fetch(item, notes, "saved.pdf")
	check = host(item, "qpdf", "--check", str(path))
	item.check("No syntax or stream encoding errors" in check, "qpdf --check found errors")
	text = host(item, "pdftotext", "-f", "1", "-l", "1", str(path), "-")
	item.check("the lazy fox" in text and "ZEBRA" in text, "pdftotext does not read the new words")
	item.check("a third line that the test deletes" not in text, "the deleted line is still read")
	fonts = host(item, "pdffonts", str(path))
	item.check(re.search(r"\+\S+\s+CID TrueType\s+Identity-H\s+yes\s+yes\s+yes", fonts), "no embedded subset replacement font")
	item.person(f"{kept}; {replaced}; the screenshots: the box under the line, the words on the page as typed, the fonts")


@run.define("apps.notes.text-box-follow")
def text_box_follow(item):
	folder = prepare(item)
	notes = Notes(item, folder)
	notes.press(SELECT, r"NOTES TOOL 16 name=select", "Select")

	# The first line's box.
	point = notes.at_pdf(LEFT + 60, line_y(0))
	mark = run.mark()
	run.click(*point, "--count", "2")
	opened = run.wait(r"NOTES TEXT box open kind=line page=0 object=\d+ font=original", mark, 10)
	item.step("double-clicked the first line", opened or "")
	run.shot(item, "box")
	item.check(opened, "the first line's box did not open")
	first = aatlib.number(opened, "object")

	# Full screen: the box moves with the line (ws177-p012).
	mark = run.mark()
	run.key("f11")
	moved = run.wait(r"NOTES TEXT box moved rect=", mark, 10)
	closed = run.lines(r"NOTES TEXT box close", mark)
	item.step("F11", f"{moved}; {len(closed)} closes")
	time.sleep(0.8)
	run.shot(item, "fullscreen")
	item.check(moved and not closed, "the box did not move with the line, or it closed")

	# Typed; Ctrl+Z takes the last key back and Ctrl+Shift+Z does it again (ws177-p013: the box's own history).
	run.key("end")
	run.type(" again")
	time.sleep(0.4)
	mark = run.mark()
	run.key("ctrl+z")
	undone = run.wait(r"NOTES TEXT box reported=\d+ bytes=48\b", mark, 10)
	run.key("ctrl+shift+z")
	redone = run.wait(r"NOTES TEXT box reported=\d+ bytes=49\b", mark, 10)
	item.step("Ctrl+Z, Ctrl+Shift+Z in the box", f"{undone}; {redone}")
	item.check(undone and redone, "Ctrl+Z and Ctrl+Shift+Z did not take one key back and do it again")

	# Ctrl+S with the box open: the words kept and saved.
	mark = run.mark()
	run.key("ctrl+s")
	kept = run.wait(rf"NOTES EDIT text page=0 object={first} kind=line .*font=original", mark, 10)
	saved = run.wait(r"NOTES SAVE reason=request ", mark, 10)
	item.step("typed again, Ctrl+S", f"{kept}; {saved}")
	item.check(kept and saved, "Ctrl+S with the box open did not keep and save the words")

	# Back, closed, and read on the host.
	run.key("f11")
	time.sleep(0.8)
	notes.close()
	path = fetch(item, notes, "saved.pdf")
	text = host(item, "pdftotext", "-f", "1", "-l", "1", str(path), "-")
	item.check("again" in text, "pdftotext does not read the words typed before Ctrl+S")
	item.person("the screenshots: the box under the first line, before and after full screen")


@run.define("apps.notes.pdf-insert-text-font")
def pdf_insert_text_font(item):
	folder = prepare(item)
	old = common.current_method(run)
	common.set_method(run, item, 1)
	try:
		notes = Notes(item, folder)
		notes.press(TEXT, r"NOTES TOOL 41 name=text", "Text")
		run.shot(item, "text-tool")

		# "Hello Notes" at the page's lower margin, Sans.
		place = notes.at_pdf(LEFT, 200)
		notes.act(lambda: run.click(*place), r"NOTES TEXT box open kind=new page=0 object=-1 font=sans", "clicked the lower margin")
		run.type("Hello Notes")
		time.sleep(0.4)
		run.shot(item, "typing")
		inserted = notes.act(lambda: run.key("esc"), r"NOTES EDIT text page=0 object=\d+ kind=new chars=11 font=sans size=12 ",
			"Esc")
		run.shot(item, "inserted")
		index = aatlib.number(inserted, "object")

		# The text chosen with Select, and its font made Mono.
		notes.press(SELECT, r"NOTES TOOL 16 name=select", "Select")
		notes.act(lambda: run.click(place[0] + 20, place[1] + 6), rf"NOTES EDIT select page=0 object={index} kind=text inserted=1",
			"clicked the words")
		time.sleep(0.5)
		notes.press(FONT, rf"NOTES EDIT font page=0 object={index} font=mono$", "Font")
		run.shot(item, "mono")

		# Japanese through the input method, in the Japanese font.
		notes.press(TEXT, r"NOTES TOOL 41 name=text", "Text")
		notes.press(FONT, None, "Font (Mono)")
		notes.press(FONT, None, "Font (Japanese)")
		notes.act(lambda: run.click(*notes.at_pdf(LEFT, 140)), r"NOTES TEXT box open kind=new page=0 object=-1 font=cjk",
			"clicked another margin")
		common.to_language(run, item, "ja")
		run.type("nihongo")
		run.key("space")
		time.sleep(0.6)
		run.shot(item, "composing")
		run.key("enter")
		time.sleep(0.5)
		run.key("alt+space")
		time.sleep(0.4)
		japanese = notes.act(lambda: run.key("esc"), r"NOTES EDIT text page=0 object=\d+ kind=new chars=3 font=cjk ", "Esc")
		run.shot(item, "japanese")

		# Saved, and read on the host.
		notes.act(lambda: run.key("ctrl+s"), r"NOTES SAVE reason=request ", "Ctrl+S")
		notes.close()
	finally:
		if common.current_method(run) != old:
			common.set_method(run, item, old)
	path = fetch(item, notes, "saved.pdf")
	check = host(item, "qpdf", "--check", str(path))
	item.check("No syntax or stream encoding errors" in check, "qpdf --check found errors")
	text = host(item, "pdftotext", "-f", "1", "-l", "1", str(path), "-")
	item.check("Hello Notes" in text and "日本語" in text, "pdftotext does not read Hello Notes and 日本語")
	fonts = host(item, "pdffonts", str(path))
	item.check(len(re.findall(r"\+\S+\s+CID TrueType\s+Identity-H\s+yes\s+yes\s+yes", fonts)) >= 2, "not two embedded subsets")
	item.person(f"{japanese}; the screenshots: the box with the words, the preedit at the caret, Mono, the Japanese")


@run.define("apps.notes.pdf-edit-multipage")
def pdf_edit_multipage(item):
	folder = prepare(item)
	notes = Notes(item, folder)
	notes.press(SELECT, r"NOTES TOOL 16 name=select", "Select")

	# Page 1's image moved right.
	left, bottom, width, height = IMAGE
	middle = notes.at_pdf(left + width / 2, bottom + height / 2)
	notes.act(lambda: run.drag(middle[0], middle[1], middle[0] + 120, middle[1]), r"NOTES EDIT move page=0 object=\d+", "dragged the image right")

	# Page 3 (/Rotate 90): its first line deleted (shown at x = its baseline's y, y = its x).
	notes.act(lambda: run.key("pagedown", "pagedown"), r"NOTES PAGE current=2 count=3", "Page Down twice")
	time.sleep(0.8)
	point = notes.at(TOP + 4, LEFT + 60)
	notes.act(lambda: run.click(*point), r"NOTES EDIT select page=2 object=\d+ kind=text", "clicked page 3's first line")
	notes.act(lambda: run.key("delete"), r"NOTES EDIT delete page=2 object=\d+", "Delete")
	run.shot(item, "page-3-deleted")

	# A pen line on page 3.
	notes.pen()
	start = notes.at(300, 300)
	stroke = notes.act(lambda: run.press_path([(start[0] + step * 20, start[1] + step * 8) for step in range(8)]),
		r"NOTES STROKE page=2 ", "drew a line on page 3")
	run.shot(item, "page-3-stroke")

	# Saved, closed, opened again: the edits and the line kept.
	notes.act(lambda: run.key("ctrl+s"), r"NOTES SAVE reason=request pages=3 strokes=1 edits=2 edited_pages=2 ", "Ctrl+S")
	notes.close()
	again = Notes(item, folder)
	edits = run.wait(r"NOTES EDITS opened edits=2 edited_pages=2 rebased=0", again.start, 10)
	item.step("opened again", f"{again.opened}; {edits}")
	item.check(edits and "strokes=1" in again.opened, "the edits or the line were not opened again")
	run.shot(item, "reopened-page-1")

	# Page 1's image put back with Reset.
	again.press(SELECT, r"NOTES TOOL 16 name=select", "Select")
	moved = again.at_pdf(left + width / 2 + 120 / again.scale(), bottom + height / 2)
	again.act(lambda: run.click(*moved), r"NOTES EDIT select page=0 object=\d+ kind=image", "clicked the moved image")
	time.sleep(0.5)
	again.press(RESET_OBJECT, r"NOTES EDIT reset page=0 object=\d+", "Reset")
	run.shot(item, "reset")
	again.act(lambda: run.key("ctrl+s"), r"NOTES SAVE reason=request ", "Ctrl+S")
	again.close()

	# On the host: page 2 as the original's.
	path = fetch(item, again, "saved.pdf")
	check = host(item, "qpdf", "--check", str(path))
	item.check("No syntax or stream encoding errors" in check, "qpdf --check found errors")
	original = run.outdir / "notes-edit" / item.ident / "edit-basic.pdf"
	pages = []
	for source, name in ((original, "original"), (path, "saved")):
		prefix = run.outdir / "notes-edit" / item.ident / f"page2-{name}"
		host(item, "pdftoppm", "-r", "50", "-f", "2", "-l", "2", "-png", str(source), str(prefix))
		pages.append(sorted(prefix.parent.glob(f"page2-{name}*.png")))
	item.check(pages[0] and pages[1], "pdftoppm made no picture of page 2")
	from PIL import Image, ImageChops
	same = ImageChops.difference(Image.open(pages[0][0]).convert("RGB"), Image.open(pages[1][0]).convert("RGB")).getbbox() is None
	item.check(same, "page 2 is not drawn as the original's")
	item.person(f"{stroke}; the screenshots: page 3's line gone and the pen line, both after opening again, the image put back")


def pen_script(points: list[tuple[float, float]]) -> str:
	"""A peninject script of one stroke through screen points: the pen tilted, its pressure from 0 up to full and back
	to 0 along the stroke (the compositor maps the whole tablet to the whole screen, plan/ws079/tests/notes-pen.sh)."""
	width, height = run.screen()

	def raw(x: float, y: float) -> tuple[int, int]:
		return round(x * PEN_RANGE / (width - 1)), round(y * PEN_RANGE / (height - 1))

	x, y = raw(*points[0])
	lines = [f"size {PEN_RANGE} {PEN_RANGE}", f"wait {PEN_SETTLE_MS}", "tool pen", f"hover {x} {y} 20 -15", "wait 200",
		f"down {x} {y} 0 20 -15"]
	last = len(points) - 1
	for index, point in enumerate(points[1:], start=1):
		x, y = raw(*point)
		pressure = round(PEN_PRESSURE_MAX * math.sin(index / last * math.pi))
		lines += [f"move {x} {y} {pressure} 20 -15", f"wait {PEN_STEP_MS}"]
	lines += ["up", "wait 500"]
	return "\n".join(lines) + "\n"


def has_pen() -> bool:
	"""Whether the image has the test pen (peninject: plan/ws216/tests/config.mk has it, the plain AAT image not)."""
	status, output = run.sh("test -x /bin/peninject && echo pen=yes || echo pen=no")
	return "pen=yes" in output


def pen_stroke(item, notes: Notes, name: str, points: list[tuple[float, float]], tool: int, pen: bool) -> str:
	"""Draws a stroke with the test pen and waits for its NOTES STROKE of the model's tool, with the pen's pressure and
	tilt.  aat-input holds every open of the injector, so it is stopped for the pen and started again after.  Without
	the pen (an image without peninject) the stroke is the mouse's, of no pressure, and the record says so."""
	if not pen:
		line = notes.act(lambda: run.press_path(points[::4] + [points[-1]]), rf"NOTES STROKE page=\d+ id=\d+ tool={tool} ",
			f"the {name} stroke with the mouse (no test pen in the image: no pressure)")
		return line
	local = run.outdir / "notes-draw" / item.ident / f"{name}.pen"
	local.parent.mkdir(parents=True, exist_ok=True)
	local.write_text(pen_script(points))
	remote = f"{notes.folder}/{name}.pen"
	run.aat("put", str(local), remote)
	width, height = run.screen()
	mark = run.mark()
	run.aat("stop")
	try:
		status, output = run.sh(f"timeout 60 /bin/peninject {shlex.quote(remote)}; echo peninject=$?", timeout=90)
	finally:
		run.aat("start", "--size", f"{width}x{height}")
		time.sleep(1.0)
	pattern = rf"NOTES STROKE page=\d+ id=\d+ tool={tool} points=\d+ strokes=\d+ pressure=\d+\.\.\d+ tilt=1"
	line = run.wait(pattern, mark, 15)
	item.step(f"the pen's {name} stroke (peninject)", f"{output.strip()[-80:]}; {line or 'no NOTES STROKE'}")
	item.check("peninject=0" in output, f"{name}: peninject failed: {output.strip()[-200:]}")
	item.check(line, f"{name}: no line {pattern!r}")
	highest = int(re.search(r"pressure=\d+\.\.(\d+)", line).group(1))
	item.check(highest > PEN_PRESSED, f"{name}: the stroke has no pen pressure: {line}")
	return line


def row(area: tuple[int, int, int, int], fraction: float, start: float = 0.15, end: float = 0.85, wave: float = 0.0,
		steps: int = 40) -> list[tuple[float, float]]:
	"""The screen points of a stroke across the page shown, at a fraction of its height, with a wave of a height."""
	left, top, right, bottom = area
	y = top + (bottom - top) * fraction
	points = []
	for step in range(steps + 1):
		t = step / steps
		points.append((left + (right - left) * (start + (end - start) * t), y + wave * math.sin(t * 2 * math.pi)))
	return points


def pdf_parts(item, path: Path) -> bytes:
	"""The saved PDF's objects uncompressed (qpdf --qdf), for its graphics states and patterns."""
	plain = path.with_name(path.stem + "-qdf.pdf")
	host(item, "qpdf", "--qdf", "--object-streams=disable", str(path), str(plain))
	return plain.read_bytes() if plain.exists() else b""


def page_picture(item, path: Path, name: str) -> None:
	"""Page 1 of a saved PDF drawn by Poppler (pdftoppm) beside the records, for a person to compare with the screen."""
	prefix = run.pictures / f"{item.ident}-{name}"
	run.pictures.mkdir(parents=True, exist_ok=True)
	host(item, "pdftoppm", "-r", "72", "-f", "1", "-l", "1", "-png", "-singlefile", str(path), str(prefix))
	picture = prefix.with_suffix(".png")
	if picture.exists():
		relative = str(picture.relative_to(run.outdir))
		item.pictures.append(relative)
		item.steps[-1]["pictures"].append(relative)


@run.define("apps.notes.draw-tools")
def draw_tools(item):
	folder = prepare(item)
	notes = Notes(item, folder, "draw.pdf", opened=r"NOTES START ")
	area = notes.area()
	pen = has_pen()
	item.step("the test pen", "peninject" if pen else "none: the strokes are the mouse's, of no pressure")

	# The brush, the pencil and the watercolour by their keys, each a stroke of the pen from no pressure to full and back.
	notes.act(lambda: run.key("b"), r"NOTES TOOL 49 name=brush", "the key B (the brush)")
	notes.press(COLOR, None, "black")
	notes.press(WIDTH + 1, None, "the middle width")
	brush = pen_stroke(item, notes, "brush", row(area, 0.15, wave=18), STROKE_BRUSH, pen)
	notes.act(lambda: run.key("l"), r"NOTES TOOL 50 name=pencil", "the key L (the pencil)")
	pencil = pen_stroke(item, notes, "pencil", row(area, 0.32, wave=18), STROKE_PENCIL, pen)
	run.shot(item, "brush-pencil")

	# Two watercolour strokes, blue then red, crossing: their overlap is darker, the colours mixed as paint (Multiply).
	notes.act(lambda: run.key("w"), r"NOTES TOOL 51 name=watercolor", "the key W (the watercolour)")
	notes.press(COLOR + 1, None, "blue")
	water = pen_stroke(item, notes, "water-blue", row(area, 0.52), STROKE_WATERCOLOR, pen)
	notes.press(COLOR + 2, None, "red")
	left, top, right, bottom = area
	cross = [(left + (right - left) * (0.35 + 0.3 * step / 40), top + (bottom - top) * (0.42 + 0.2 * step / 40)) for step in range(41)]
	pen_stroke(item, notes, "water-red", cross, STROKE_WATERCOLOR, pen)
	run.shot(item, "watercolor")

	# The pipelines of the grain (the pencil) and Multiply (the watercolour) were made, none fell back.
	made = run.lines(r"NOTES PIPE made name=(grain|multiply)", notes.start)
	fallback = run.lines(r"NOTES PIPE fallback", notes.start)
	grain = run.lines(r"NOTES GRAIN hash=", notes.start)
	item.step("the pipelines", "; ".join(made + fallback + grain[-1:]))
	item.check(any("name=grain" in line for line in made) and any("name=multiply" in line for line in made),
		"no NOTES PIPE made name=grain and name=multiply")
	item.check(not fallback, f"a pipeline fell back: {fallback[:1]}")

	# The drawing tool button opens the strip; its Marker chooses the highlighter and closes it.
	mark = run.mark()
	notes.press(DRAW, r"NOTES DRAWS open", "the drawing tool button")
	strip = run.wait(rf"NOTES BUTTONS .* {BRUSH}:", mark, 10)
	item.check(strip, "the strip's buttons were not logged (no NOTES BUTTONS with the brush)")
	run.shot(item, "strip")
	mark = run.mark()
	notes.press(HIGHLIGHTER, r"NOTES TOOL 2 name=highlighter", "the strip's Marker")
	item.check(run.wait(r"NOTES DRAWS closed", mark, 5), "the strip did not close after the Marker")
	marker = notes.act(lambda: run.press_path(row(area, 0.78, steps=12)), rf"NOTES STROKE page=0 id=\d+ tool={STROKE_HIGHLIGHTER} ",
		"drew a Marker line with the mouse")
	strokes = aatlib.number(marker, "strokes")
	run.shot(item, "all-tools")

	# Undo and redo of the last stroke.
	notes.act(lambda: run.key("ctrl+z"), rf"NOTES UNDO page=0 strokes={strokes - 1} ", "Ctrl+Z")
	notes.act(lambda: run.key("ctrl+shift+z"), rf"NOTES REDO page=0 strokes={strokes} ", "Ctrl+Shift+Z")
	notes.press(COLOR, None, "black (put back for the next scenarios)")
	notes.act(lambda: run.key("p"), r"NOTES TOOL 1 name=pen", "the key P (the pen, put back)")

	# Saved, closed, opened again: every stroke there, drawn as before.
	notes.act(lambda: run.key("ctrl+s"), rf"NOTES SAVE reason=request pages=\d+ strokes={strokes} ", "Ctrl+S")
	notes.close()
	again = Notes(item, folder, "draw.pdf")
	item.check(f"strokes={strokes} " in again.opened and "kind=notes" in again.opened, f"not opened again with the strokes: {again.opened}")
	time.sleep(1.0)
	run.shot(item, "reopened")
	fallback = run.lines(r"NOTES PIPE fallback", again.start)
	item.check(not fallback, f"a pipeline fell back after opening again: {fallback[:1]}")
	again.close()

	# On the host: a sound PDF with the watercolour's Multiply and the pencil's grain pattern, drawn by Poppler.
	path = fetch(item, again, "draw.pdf")
	check = host(item, "qpdf", "--check", str(path))
	item.check("No syntax or stream encoding errors" in check, "qpdf --check found errors")
	parts = pdf_parts(item, path)
	item.check(b"/Multiply" in parts, "the saved PDF has no /BM /Multiply (the watercolour)")
	item.check(b"/PatternType 1" in parts, "the saved PDF has no tiling pattern (the pencil's grain)")
	page_picture(item, path, "poppler")
	if not pen:
		item.failed("the image has no test pen (peninject), so the strokes had no pressure: build it with plan/ws216/tests/config.mk")
	item.person(f"{brush}; {pencil}; {water}; {made}; the screenshots: the brush thin at its ends and thick in the middle, "
		"the pencil's grain with no lines at its sections' joins, the watercolours darker where they cross (blue and red mixed "
		"as paint), the strip under the toolbar, the same page after opening again, and Poppler's page near the screen's")


@run.define("apps.notes.fill-region")
def fill_region(item):
	folder = prepare(item)
	notes = Notes(item, folder, "fill.pdf", opened=r"NOTES START ")
	notes.pen()
	area = notes.area()
	left, top, right, bottom = area
	middle = ((left + right) / 2, (top + bottom) / 2)
	radius = min(right - left, bottom - top) * 0.25

	# A closed circle with the pen (the mouse), its end past its start.
	circle = [(middle[0] + radius * math.cos(step / 36 * 2 * math.pi), middle[1] + radius * math.sin(step / 36 * 2 * math.pi))
		for step in range(40)]
	notes.act(lambda: run.press_path(circle), rf"NOTES STROKE page=0 id=\d+ tool={STROKE_PEN} ", "drew a closed circle")

	# The fill tool by its key, a click inside the circle: filled under the line, with no background to draw.
	notes.act(lambda: run.key("f"), r"NOTES TOOL 52 name=fill", "the key F (the fill)")
	mark = run.mark()
	run.click(*middle)
	made = run.wait(r"NOTES FILL made place=\d+ ", mark, 120)
	bands = run.wait(r"NOTES FILL bands=\d+ background_us=\d+", mark, 5)
	item.step("clicked inside the circle", f"{bands}; {made}")
	item.check(made, "no NOTES FILL made")
	item.check(aatlib.number(made, "place") == 0, f"the fill is not under the circle's line: {made}")
	item.check(bands and aatlib.number(bands, "background_us") == 0, f"a page without a background drew one: {bands}")
	time.sleep(1.0)
	run.shot(item, "filled")

	# The same place again: refused, the fill there already ("Already filled").
	refused = notes.act(lambda: run.click(*middle), r"NOTES FILL refused reason=same ", "clicked the same place again", timeout=120)
	run.shot(item, "already-filled")

	# A drag with the fill tool fills nothing ("Tap to fill").
	drag = [(middle[0] + step * 8, middle[1] + step * 4) for step in range(8)]
	notes.act(lambda: run.press_path(drag), r"NOTES FILL moved", "dragged on the fill")
	run.shot(item, "tap-to-fill")

	# The eraser (whole strokes) at the circle's middle takes the fill, not the line; Ctrl+Z brings it back.
	tool = notes.act(lambda: run.key("e"), rf"NOTES TOOL {ERASER} parts=\d name=eraser", "the key E (the eraser)")
	if "parts=1" in tool:
		notes.act(lambda: run.key("e"), rf"NOTES TOOL {ERASER} parts=0 name=eraser", "the key E again (whole strokes)")
	notes.act(lambda: run.click(*middle), r"NOTES ERASE page=0 removed=1 strokes=1\b", "clicked the eraser inside the circle")
	run.shot(item, "erased")
	notes.act(lambda: run.key("ctrl+z"), r"NOTES UNDO page=0 strokes=2 ", "Ctrl+Z")
	run.shot(item, "undone")
	notes.act(lambda: run.key("p"), r"NOTES TOOL 1 name=pen", "the key P (the pen, put back)")
	notes.close()

	# On a PDF: the white square inside edit-basic.pdf's picture (a gradient round it) filled, its background drawn.
	pdf = Notes(item, folder)
	pdf.act(lambda: run.key("f"), r"NOTES TOOL 52 name=fill", "the key F (the fill)")
	square = pdf.at_pdf(*PICTURE_SQUARE)
	shown = pdf.area()
	item.check(shown[0] <= square[0] <= shown[2] and shown[1] <= square[1] <= shown[3], f"the picture's square {square} is not shown")
	mark = run.mark()
	run.click(*square)
	on_pdf = run.wait(r"NOTES FILL made place=\d+ ", mark, 120)
	background = run.wait(r"NOTES FILL bands=\d+ background_us=\d+", mark, 5)
	item.step("clicked the white square inside the PDF's picture", f"{background}; {on_pdf}")
	item.check(on_pdf, "no NOTES FILL made on the PDF")
	item.check(background and aatlib.number(background, "background_us") > 0, f"the PDF's page was not drawn for the fill: {background}")
	time.sleep(1.0)
	run.shot(item, "pdf-filled")
	pdf.act(lambda: run.key("p"), r"NOTES TOOL 1 name=pen", "the key P (the pen, put back)")
	pdf.act(lambda: run.key("ctrl+s"), r"NOTES SAVE reason=request pages=3 strokes=1 ", "Ctrl+S")
	pdf.close()

	# On the host: a sound PDF, drawn by Poppler.
	path = fetch(item, pdf, "saved.pdf")
	check = host(item, "qpdf", "--check", str(path))
	item.check("No syntax or stream encoding errors" in check, "qpdf --check found errors")
	page_picture(item, path, "poppler")
	item.person(f"{made}; {bands}; {refused}; {on_pdf}; {background}; the screenshots: the circle filled up to its line with "
		"no white gap and the line on top, the notice Already filled, the status Tap to fill, the fill gone and back, the PDF's "
		"white square filled (and Poppler's page the same)")


sys.exit(run.go(before=common.before(run), after=common.after(run)))
