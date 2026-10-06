#!/usr/bin/env python3
"""Makes the m4a files of Music's host tests (ws120-p008): MP4 boxes with tags and no real sound.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    make-m4a.py FOLDER     (writes the tests' collection under FOLDER/Music and two files beside it)

The files have a movie header, tracks with their handlers (sound, pictures), the iTunes-style list of tags, and an
empty media box; tags.c reads only those.  Their packets are not there: Music's player is not tested with them.
"""
import struct
import sys
from pathlib import Path


def box(kind: bytes, payload: bytes) -> bytes:
	return struct.pack(">I", 8 + len(payload)) + kind + payload


def large_box(kind: bytes, payload: bytes) -> bytes:
	"""A box with a 64-bit size."""
	return struct.pack(">I", 1) + kind + struct.pack(">Q", 16 + len(payload)) + payload


def movie_header(milliseconds: int, version: int = 0) -> bytes:
	if version == 1:
		return box(b"mvhd", bytes([1, 0, 0, 0]) + struct.pack(">QQIQ", 0, 0, 1000, milliseconds) + bytes(80))
	return box(b"mvhd", bytes(4) + struct.pack(">IIII", 0, 0, 1000, milliseconds) + bytes(80))


def track(handler: bytes) -> bytes:
	hdlr = box(b"hdlr", bytes(4) + bytes(4) + handler + bytes(12) + b"\0")
	return box(b"trak", box(b"mdia", hdlr))


def data(kind: int, value: bytes) -> bytes:
	return box(b"data", struct.pack(">I", kind) + bytes(4) + value)


def tags(items: dict, quicktime: bool = False) -> bytes:
	listed = b""
	for name, value in items.items():
		code = name.encode("latin-1")
		if name == "trkn":
			listed += box(code, data(0, struct.pack(">HHHH", 0, value, 10, 0)))
		elif name == "covr":
			listed += box(code, data(13, value))
		else:
			listed += box(code, data(1, value.encode("utf-8")))
	handler = box(b"hdlr", bytes(8) + b"mdirappl" + bytes(9))
	inner = handler + box(b"ilst", listed)
	if not quicktime:
		inner = bytes(4) + inner
	return box(b"udta", box(b"meta", inner))


def m4a(path: Path, items: dict, milliseconds: int = 180000, handlers=(b"soun",), quicktime=False, mdat_first=False,
		version=0) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	movie = movie_header(milliseconds, version)
	for handler in handlers:
		movie += track(handler)
	if items:
		movie += tags(items, quicktime)
	ftyp = box(b"ftyp", b"M4A " + bytes(4) + b"M4A mp42isom")
	moov = box(b"moov", movie)
	if mdat_first:
		path.write_bytes(ftyp + large_box(b"mdat", bytes(64)) + moov)
	else:
		path.write_bytes(ftyp + moov + box(b"mdat", bytes(64)))


def main() -> None:
	folder = Path(sys.argv[1])
	music = folder / "Music"
	cover_a = b"\xff\xd8\xff\xe0JPEG-A"
	cover_b = b"\xff\xd8\xff\xe0JPEG-B"
	# An album of two, numbered out of the order of their names.
	m4a(music / "Ann/Blue/02 second.m4a", {"\xa9nam": "Second Song", "\xa9ART": "Ann", "\xa9alb": "Blue", "trkn": 2,
		"covr": cover_a}, 200500)
	m4a(music / "Ann/Blue/01 first.m4a", {"\xa9nam": "First Song", "\xa9ART": "Ann", "\xa9alb": "Blue", "trkn": 1,
		"covr": cover_b}, 61000, quicktime=True, mdat_first=True)
	# An album of several artists, under its album's artist, a movie header of version 1.
	m4a(music / "mix/bob.m4a", {"\xa9nam": "Bob's Tune", "\xa9ART": "Bob", "aART": "Various", "\xa9alb": "Mix",
		"trkn": 1}, 3723000, version=1)
	m4a(music / "mix/deep/er/carol.m4a", {"\xa9nam": "Café", "\xa9ART": "Carol", "aART": "Various", "\xa9alb": "Mix",
		"trkn": 2})
	# No tags: named after its file, an unknown artist and album.
	m4a(music / "untagged.m4a", {})
	# An .mp4 with sound only is a song; one with pictures is a video.
	m4a(music / "voice.MP4", {"\xa9nam": "Voice Memo", "\xa9alb": "Memos"})
	m4a(music / "clip.mp4", {"\xa9nam": "A Clip"}, handlers=(b"vide", b"soun"))
	# Not looked at: too deep, hidden, not an MP4, not a song's name.
	m4a(music / "a/b/c/d/deep.m4a", {"\xa9nam": "Too Deep"})
	m4a(music / ".hidden/secret.m4a", {"\xa9nam": "Hidden"})
	(music / "broken.m4a").write_bytes(b"not an mp4 at all")
	m4a(music / "notes.txt", {"\xa9nam": "Text"})
	# Beside the folder: one opened from Files, and a video.
	m4a(folder / "outside.m4a", {"\xa9nam": "Outside", "\xa9ART": "Ann", "\xa9alb": "Blue", "trkn": 3})
	m4a(folder / "film.m4a", {"\xa9nam": "Film"}, handlers=(b"vide",))


if __name__ == "__main__":
	main()
