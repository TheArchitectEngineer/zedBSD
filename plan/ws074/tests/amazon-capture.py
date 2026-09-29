#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Saves the demo's two amazon.co.jp pages once, with local copies of what they draw with.

  amazon-capture.py [--out DIR] [--refetch]

Fetches the top page and the search results for "kei" with browser's own User-Agent (one request
each, with a cookie jar), then makes the variants the comparisons use, all under DIR (default
build/ws074-amazon; nothing goes into the source tree):

  top.html, search.html                 the pages as they came
  top-noscript.html, search-noscript.html   the same without their <script> elements
  top-local.html, search-local.html     (and -local-noscript) the pages with every stylesheet, the
                                        images their CSS names and the <img> sources replaced by
                                        local copies in DIR/files/ (srcset dropped, so both browsers
                                        draw the same source), so that browser and Chromium draw
                                        from the same bytes without asking the site again

The page requests go to www.amazon.co.jp only when DIR has no copy yet (or with --refetch); the
stylesheets and images come from the CDN (m.media-amazon.com) once each.
"""

import argparse
import datetime
import hashlib
import html
import json
import os
import re
import subprocess
import sys
import time

AGENT = "browser/0.1 (Kei)"
PAGES = [("top", "https://www.amazon.co.jp/"), ("search", "https://www.amazon.co.jp/s?k=kei")]


def fetch(url, path, jar=None):
    """Fetches one URL into a file with browser's User-Agent; returns the HTTP status."""
    command = ["curl", "-s", "-L", "--max-time", "60", "-o", path, "-w", "%{http_code}", "-A", AGENT,
               "-H", "Accept-Encoding: identity"]
    if jar is not None:
        command += ["-c", jar, "-b", jar]
    result = subprocess.run(command + [url], capture_output=True, text=True, timeout=90)
    return result.stdout.strip()


def local_name(url):
    """Names the local copy of a URL by its hash, keeping the extension."""
    base = url.split("?")[0]
    extension = os.path.splitext(base)[1]
    if not re.match(r"^\.[A-Za-z0-9]{1,5}$", extension):
        extension = ""
    return hashlib.sha1(url.encode()).hexdigest()[:16] + extension


def mirror(url, files):
    """Copies a URL into the files directory once; returns the local name, or None when it failed."""
    name = local_name(url)
    path = os.path.join(files, name)
    if not os.path.exists(path):
        status = fetch(url, path + ".tmp")
        if status != "200":
            if os.path.exists(path + ".tmp"):
                os.unlink(path + ".tmp")
            print("  failed %s %s" % (status, url), file=sys.stderr)
            return None
        os.rename(path + ".tmp", path)
    return name


def absolute(url, base):
    """Resolves a URL against a base the simple way the pages need (absolute, // and / paths)."""
    url = html.unescape(url.strip())
    if url.startswith("//"):
        return "https:" + url
    if url.startswith("/"):
        match = re.match(r"^(https?://[^/]+)", base)
        return match.group(1) + url
    if re.match(r"^[a-z]+:", url):
        return url
    return base.rsplit("/", 1)[0] + "/" + url


def localize_css(text, css_url, files, prefix=""):
    """Replaces the url() images of a stylesheet with local copies (prefix leads from the sheet to files/)."""
    def replace(match):
        raw = match.group(2)
        if raw.startswith("data:"):
            return match.group(0)
        name = mirror(absolute(raw, css_url), files)
        if name is None:
            return match.group(0)
        return "url(" + prefix + name + ")"
    return re.sub(r"url\(\s*(['\"]?)([^'\")]+)\1\s*\)", replace, text)


def localize_page(text, page_url, files):
    """Replaces a page's stylesheets and <img> sources with local copies."""
    def sheet(match):
        tag = match.group(0)
        href = re.search(r"href=(['\"])(.*?)\1", tag, flags=re.I)
        if "stylesheet" not in tag or href is None:
            return tag
        url = absolute(href.group(2), page_url)
        name = local_name(url)
        path = os.path.join(files, name)
        if not os.path.exists(path):
            raw = mirror(url, files)
            if raw is None:
                return tag
            with open(path, encoding="utf-8", errors="replace") as source:
                css = source.read()
            with open(path, "w", encoding="utf-8") as target:
                target.write(localize_css(css, url, files))
        return tag.replace(href.group(0), 'href="files/%s"' % name)

    def image(match):
        tag = match.group(0)
        tag = re.sub(r"\s(data-)?srcset=(['\"]).*?\2", "", tag, flags=re.I)
        src = re.search(r"\ssrc=(['\"])(.*?)\1", tag, flags=re.I)
        if src is None or src.group(2).startswith("data:"):
            return tag
        name = mirror(absolute(src.group(2), page_url), files)
        if name is None:
            return tag
        return tag.replace(src.group(0), ' src="files/%s"' % name)

    def style_block(match):
        return match.group(1) + localize_css(match.group(2), page_url, files, "files/") + match.group(3)

    def style_attribute(match):
        quote = match.group(1)
        value = localize_css(match.group(2), page_url, files, "files/")
        return "style=" + quote + value + quote

    text = re.sub(r"<link[^>]*>", sheet, text)
    # Chromium otherwise selects a remote <picture><source srcset> while browser,
    # which does not implement picture source selection yet, uses the localized
    # fallback <img>.  Force both engines to consume exactly the same bytes.
    text = re.sub(r"<source\b[^>]*>", "", text, flags=re.I)
    text = re.sub(r"<img[^>]*>", image, text)
    text = re.sub(r"(<style[^>]*>)(.*?)(</style>)", style_block, text, flags=re.S)
    text = re.sub(r"\bstyle=(['\"])(.*?)\1", style_attribute, text, flags=re.S | re.I)
    return text


def without_scripts(text):
    """Drops the <script> elements of a page."""
    text = re.sub(r"<script\b.*?</script>", "", text, flags=re.S | re.I)
    # browser has no iframe layout.  Removing them keeps Chromium from loading
    # live advertisements that browser cannot render and makes the capture local.
    return re.sub(r"<iframe\b.*?</iframe>", "", text, flags=re.S | re.I)


def file_record(path, root):
    """Returns the stable identity of a capture input or generated file."""
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return {
        "path": os.path.relpath(path, root),
        "bytes": os.path.getsize(path),
        "sha256": digest.hexdigest(),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "../../../build/ws074-amazon"))
    parser.add_argument("--refetch", action="store_true")
    args = parser.parse_args()
    out = os.path.abspath(args.out)
    files = os.path.join(out, "files")
    os.makedirs(files, exist_ok=True)
    jar = os.path.join(out, "cookies.txt")
    page_records = []
    for tag, url in PAGES:
        path = os.path.join(out, tag + ".html")
        if args.refetch or not os.path.exists(path):
            status = fetch(url, path, jar)
            print("%s %s %s" % (tag, status, url))
            if status != "200":
                raise SystemExit("amazon-capture: %s returned HTTP %s" % (url, status))
            time.sleep(2)
        with open(path, encoding="utf-8", errors="replace") as source:
            text = source.read()
        local = localize_page(text, url, files)
        variants = {
            tag + "-noscript.html": without_scripts(text),
            tag + "-local.html": local,
            tag + "-local-noscript.html": without_scripts(local),
        }
        for name, body in variants.items():
            with open(os.path.join(out, name), "w", encoding="utf-8") as target:
                target.write(body)
        page_records.append({
            "tag": tag,
            "url": url,
            "fetched_at_utc": datetime.datetime.fromtimestamp(
                os.path.getmtime(path), datetime.timezone.utc
            ).isoformat(),
            "source": file_record(path, out),
            "variants": [file_record(os.path.join(out, name), out) for name in variants],
        })
        print("%s: %d bytes, %d stylesheets, %d files" % (tag, len(text), len(re.findall(r"<link[^>]*stylesheet", text)),
                                                         len(os.listdir(files))))
    assets = [file_record(os.path.join(files, name), out) for name in sorted(os.listdir(files))]
    manifest = {
        "schema": 1,
        "user_agent": AGENT,
        "pages": page_records,
        "assets": assets,
    }
    with open(os.path.join(out, "capture-manifest.json"), "w", encoding="utf-8") as target:
        json.dump(manifest, target, ensure_ascii=False, indent=2, sort_keys=True)
        target.write("\n")


if __name__ == "__main__":
    main()
