#!/usr/bin/env python3
"""Fetch files of one pinned upstream revision into a verified cache.

  fetch-pinned-files.py --base-url URL --manifest FILE CACHE-DIRECTORY

Each manifest line (after # comments) is PATH SIZE SHA-256; the file is
fetched from URL/PATH and kept at CACHE-DIRECTORY/PATH.  An existing cache
is verified and never updated: a mismatch is an error.  A missing cache is
filled in a sibling directory and published with one rename only after
every file passes its size and SHA-256 check.
"""
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

from __future__ import annotations

import argparse
import hashlib
import os
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path


def read_manifest(path: Path) -> list[tuple[str, int, str]]:
    entries = []
    for line in path.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        name, size, digest = line.split()
        if name.startswith("/") or ".." in Path(name).parts:
            raise SystemExit(f"unsafe manifest path: {name}")
        entries.append((name, int(size), digest))
    return entries


def verify(directory: Path, entries: list[tuple[str, int, str]]) -> None:
    for name, size, digest in entries:
        path = directory / name
        if not path.is_file() or path.is_symlink():
            raise SystemExit(f"missing or unsafe cache file: {path}")
        data = path.read_bytes()
        if len(data) != size:
            raise SystemExit(f"size mismatch: {path}: expected {size}, got {len(data)}")
        if hashlib.sha256(data).hexdigest() != digest:
            raise SystemExit(f"SHA-256 mismatch: {path}")


def fetch(url: str) -> bytes:
    # A busy server (429, 5xx) or a dropped connection is retried with a growing pause.
    pause = 2.0
    for attempt in range(8):
        try:
            with urllib.request.urlopen(url, timeout=60) as reply:
                return reply.read()
        except urllib.error.HTTPError as error:
            if error.code != 429 and error.code < 500:
                raise
            wait = error.headers.get("Retry-After") if error.headers else None
            delay = float(wait) if wait and wait.isdigit() else pause
        except (urllib.error.URLError, TimeoutError):
            delay = pause
        if attempt == 7:
            break
        time.sleep(min(delay, 120.0))
        pause = min(pause * 2.0, 60.0)
    raise SystemExit(f"could not fetch {url}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("cache", type=Path)
    args = parser.parse_args()
    entries = read_manifest(args.manifest)

    # A cache once published is only checked.
    if args.cache.exists():
        verify(args.cache, entries)
        return

    args.cache.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".fetch.", dir=args.cache.parent) as scratch:
        temporary = Path(scratch) / "files"
        for name, _, _ in entries:
            target = temporary / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(fetch(f"{args.base_url}/{name}"))
        verify(temporary, entries)
        os.replace(temporary, args.cache)


if __name__ == "__main__":
    main()
