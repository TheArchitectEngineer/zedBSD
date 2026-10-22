#!/usr/bin/env python3
"""Fetch files of one pinned upstream revision into a verified cache.

  fetch-pinned-files.py --base-url URL [--mirror TEMPLATE] --manifest FILE CACHE-DIRECTORY
  fetch-pinned-files.py --verify --manifest FILE DIRECTORY

Each manifest line (after # comments) is PATH SIZE SHA-256; the file is
fetched from URL/PATH (or, when that fails, from the mirror TEMPLATE with
{path} replaced by PATH) and kept at CACHE-DIRECTORY/PATH.  An existing cache
is verified and never updated: a mismatch is an error.  A missing cache is
filled in a sibling directory and published with one rename only after
every file passes its size and SHA-256 check.  --verify only checks DIRECTORY.
"""
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

from __future__ import annotations

import argparse
import hashlib
import os
import sys
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


def fetch(url: str, attempts: int, fallback: bool) -> bytes | None:
    # A busy server (429, 5xx) or a dropped connection is retried with a growing pause;
    # with a fallback to try, any other refusal also gives up here.
    pause = 2.0
    request = urllib.request.Request(url, headers={"User-Agent": "zedBSD-fetch-pinned-files"})
    token = os.environ.get("GITHUB_TOKEN")
    if token and url.startswith("https://raw.githubusercontent.com/"):
        request.add_header("Authorization", f"token {token}")
    for attempt in range(attempts):
        try:
            with urllib.request.urlopen(request, timeout=60) as reply:
                return reply.read()
        except urllib.error.HTTPError as error:
            if error.code != 429 and error.code < 500:
                if fallback:
                    return None
                raise
            wait = error.headers.get("Retry-After") if error.headers else None
            delay = float(wait) if wait and wait.isdigit() else pause
        except (urllib.error.URLError, TimeoutError):
            delay = pause
        if attempt == attempts - 1:
            break
        time.sleep(min(delay, 120.0))
        pause = min(pause * 2.0, 60.0)
    return None


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url")
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--mirror")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("cache", type=Path)
    args = parser.parse_args()
    entries = read_manifest(args.manifest)
    if args.verify:
        verify(args.cache, entries)
        return
    if args.base_url is None:
        parser.error("--base-url is required unless --verify is given")

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
            try:
                data = fetch(f"{args.base_url}/{name}", 4 if args.mirror else 8, args.mirror is not None)
                if data is None and args.mirror:
                    print(f"could not fetch {name} from {args.base_url}; fetching it from the mirror",
                          file=sys.stderr)
                    data = fetch(args.mirror.replace("{path}", name), 8, False)
            except urllib.error.HTTPError as error:
                raise SystemExit(f"could not fetch {name}: {error}")
            if data is None:
                raise SystemExit(f"could not fetch {name}")
            target.write_bytes(data)
        verify(temporary, entries)
        os.replace(temporary, args.cache)


if __name__ == "__main__":
    main()
