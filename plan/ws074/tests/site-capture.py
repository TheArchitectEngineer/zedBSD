#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Capture public pages and their drawing resources for offline comparisons.

The defaults start at Mozilla's Japanese home page and follow three prominent
same-origin links found there.  Additional targets can be selected with
repeated --page TAG=URL arguments.  Source pages and localized, script-free
variants are written below build/; no downloaded content enters the source
tree.
"""

import argparse
import datetime
import hashlib
from html import unescape
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from urllib.parse import urldefrag, urljoin, urlparse

from comparison_config import CHROME_USER_AGENT


ROOT = Path(__file__).resolve().parents[3]
DEFAULT_PAGES = (
    ("mozilla-top", "https://www.mozilla.org/ja/"),
    ("mozilla-products", "https://www.mozilla.org/ja/products/"),
    ("mozilla-about", "https://www.mozilla.org/ja/about/"),
    ("mozilla-manifesto", "https://www.mozilla.org/ja/about/manifesto/"),
)
CHALLENGE_PATTERNS = (
    ("cloudflare-challenge", re.compile(r"(?:cf-chl-|/cdn-cgi/challenge-platform/)", re.I)),
    ("captcha", re.compile(r"""(?:id|class)=["'][^"']*captcha""", re.I)),
    ("human-check", re.compile(r"verify you are human", re.I)),
    ("access-denied", re.compile(r"<title[^>]*>\s*access denied\s*</title>", re.I)),
    ("just-a-moment", re.compile(r"<title[^>]*>\s*just a moment", re.I)),
)
CSS_URL = re.compile(r"""url\(\s*(["']?)([^"')]+)\1\s*\)""", re.I)
CSS_IMPORT = re.compile(
    r"""@import\s+(?:url\(\s*)?(["'])(.*?)\1\s*\)?([^;]*);""",
    re.I,
)
LOCAL_ASSET = re.compile(r"^[0-9a-f]{20}(?:\.[a-z0-9]{1,8})?$", re.I)


class PageLinks(HTMLParser):
    """Collect links from one source page without executing it."""

    def __init__(self):
        super().__init__()
        self.links = []

    def handle_starttag(self, tag, attrs):
        values = dict(attrs)
        if tag.lower() == "a" and values.get("href"):
            self.links.append(values["href"])


def file_record(path, root):
    """Return a file's stable identity relative to the capture root."""
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return {
        "path": str(path.relative_to(root)),
        "bytes": path.stat().st_size,
        "sha256": digest.hexdigest(),
    }


def local_name(url):
    """Name one mirrored URL by a hash while retaining a useful extension."""
    clean = urldefrag(url)[0]
    extension = os.path.splitext(urlparse(clean).path)[1]
    if not re.fullmatch(r"\.[A-Za-z0-9]{1,8}", extension):
        extension = ""
    return hashlib.sha256(clean.encode()).hexdigest()[:20] + extension.lower()


def absolute(value, base):
    """Resolve an HTML or CSS URL against its resource URL."""
    return urljoin(base, unescape(value.strip()))


def challenge_signals(text):
    """Return conservative challenge-page signatures found in HTML."""
    return [name for name, pattern in CHALLENGE_PATTERNS if pattern.search(text)]


class Capture:
    """Fetch pages once and mirror every resource used by their fixed variants."""

    def __init__(self, out, agent, refetch):
        self.out = out
        self.files = out / "files"
        self.agent = agent
        self.refetch = refetch
        self.jar = out / "cookies.txt"
        self.assets = {}
        self.failures = []
        self.failed_urls = set()
        self.processing_css = set()
        self.localized_css = set()
        self.seen_urls = set()
        self.files.mkdir(parents=True, exist_ok=True)
        manifest_path = out / "capture-manifest.json"
        if not refetch and manifest_path.is_file():
            previous = json.loads(manifest_path.read_text(encoding="utf-8"))
            for asset in previous.get("assets", []):
                self.assets[Path(asset["path"]).name] = asset["source_url"]

    def fetch(self, url, path, use_cookies=False):
        """Fetch one URL and return its HTTP status and final URL."""
        temporary = Path(str(path) + ".tmp")
        command = [
            "curl",
            "-sS",
            "-L",
            "--max-time",
            "90",
            "-o",
            str(temporary),
            "-w",
            "%{http_code}\n%{url_effective}",
            "-A",
            self.agent,
            "-H",
            "Accept-Encoding: identity",
            "-H",
            "Accept-Language: ja,en-US;q=0.8,en;q=0.6",
        ]
        if use_cookies:
            command += ["-c", str(self.jar), "-b", str(self.jar)]
        result = subprocess.run(
            command + [url],
            capture_output=True,
            text=True,
            timeout=120,
        )
        lines = result.stdout.strip().splitlines()
        status = lines[-2] if len(lines) >= 2 else "000"
        final_url = lines[-1] if lines else url
        if result.returncode != 0 or status != "200":
            temporary.unlink(missing_ok=True)
            detail = result.stderr.strip() or "HTTP " + status
            return status, final_url, detail
        temporary.replace(path)
        return status, final_url, ""

    def page(self, tag, url):
        """Read a cached source page or fetch it once with metadata."""
        path = self.out / (tag + ".html")
        metadata_path = self.out / (tag + "-fetch.json")
        fetched = False
        if self.refetch or not path.is_file() or not metadata_path.is_file():
            status, final_url, error = self.fetch(url, path, use_cookies=True)
            if error:
                raise RuntimeError("%s: %s" % (url, error))
            metadata = {
                "http_status": int(status),
                "requested_url": url,
                "final_url": final_url,
                "fetched_at_utc": datetime.datetime.now(
                    datetime.timezone.utc
                ).isoformat(),
            }
            metadata_path.write_text(
                json.dumps(metadata, ensure_ascii=False, indent=2, sort_keys=True)
                + "\n",
                encoding="utf-8",
            )
            fetched = True
        else:
            metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        return path, metadata, fetched

    def mirror(self, url):
        """Mirror one drawing resource and return its local name."""
        clean = urldefrag(url)[0]
        scheme = urlparse(clean).scheme.lower()
        if not clean or scheme not in ("http", "https"):
            return None
        name = local_name(clean)
        path = self.files / name
        if path.is_file() and (not self.refetch or clean in self.seen_urls):
            self.assets[name] = clean
            return name
        if clean in self.failed_urls:
            return None
        status, final_url, error = self.fetch(clean, path)
        if error:
            self.failed_urls.add(clean)
            self.failures.append(
                {
                    "url": clean,
                    "http_status": int(status) if status.isdigit() else 0,
                    "final_url": final_url,
                    "error": error,
                }
            )
            print("  asset failed %s %s" % (status, clean), file=sys.stderr)
            return None
        self.seen_urls.add(clean)
        self.assets[name] = clean
        return name

    def stylesheet(self, url):
        """Mirror and recursively localize one stylesheet."""
        clean = urldefrag(url)[0]
        name = self.mirror(clean)
        if name is None:
            return None
        if name in self.localized_css or name in self.processing_css:
            return name
        self.processing_css.add(name)
        path = self.files / name
        text = path.read_text(encoding="utf-8", errors="replace")
        path.write_text(self.localize_css(text, clean), encoding="utf-8")
        self.processing_css.remove(name)
        self.localized_css.add(name)
        return name

    def localize_css(self, text, css_url, prefix=""):
        """Replace imports and url() values with mirrored relative resources."""
        imports = []

        def replace_import(match):
            raw = match.group(2).strip()
            if LOCAL_ASSET.fullmatch(raw):
                return match.group(0)
            imported_url = absolute(raw, css_url)
            name = self.stylesheet(imported_url)
            if name is None:
                return match.group(0)
            token = "__ZEDBSD_CSS_IMPORT_%d__" % len(imports)
            imports.append(
                '@import url("%s%s")%s;' % (prefix, name, match.group(3))
            )
            return token

        text = CSS_IMPORT.sub(replace_import, text)

        def replace_url(match):
            raw = match.group(2).strip()
            if raw.startswith(("data:", "blob:", "#")) or LOCAL_ASSET.fullmatch(raw):
                return match.group(0)
            name = self.mirror(absolute(raw, css_url))
            if name is None:
                return match.group(0)
            return 'url("%s%s")' % (prefix, name)

        text = CSS_URL.sub(replace_url, text)
        for index, statement in enumerate(imports):
            text = text.replace(
                "__ZEDBSD_CSS_IMPORT_%d__" % index,
                statement,
            )
        return text

    def localize_page(self, text, page_url):
        """Replace stylesheets, images and inline CSS with local resources."""
        def replace_link(match):
            tag = match.group(0)
            lowered = tag.lower()
            href = re.search(r"""\bhref=(["'])(.*?)\1""", tag, re.I | re.S)
            if "stylesheet" in lowered and href is not None:
                name = self.stylesheet(absolute(href.group(2), page_url))
                if name is None:
                    return tag
                replacement = "href=%sfiles/%s%s" % (
                    href.group(1),
                    name,
                    href.group(1),
                )
                return tag.replace(href.group(0), replacement)
            relation = re.search(r"""\brel=(["'])(.*?)\1""", tag, re.I | re.S)
            if relation is not None:
                tokens = relation.group(2).lower().split()
                if any(
                    token in tokens
                    for token in (
                        "preload",
                        "prefetch",
                        "preconnect",
                        "dns-prefetch",
                        "icon",
                    )
                ):
                    return ""
            return tag

        def replace_image(match):
            tag = match.group(0)
            tag = re.sub(
                r"""\s(?:data-)?srcset=(["']).*?\1""",
                "",
                tag,
                flags=re.I | re.S,
            )
            source = re.search(r"""\bsrc=(["'])(.*?)\1""", tag, re.I | re.S)
            if source is None or source.group(2).startswith("data:"):
                return tag
            name = self.mirror(absolute(source.group(2), page_url))
            if name is None:
                return tag
            replacement = "src=%sfiles/%s%s" % (
                source.group(1),
                name,
                source.group(1),
            )
            return tag.replace(source.group(0), replacement)

        def replace_style_block(match):
            body = self.localize_css(
                match.group(2),
                page_url,
                "files/",
            )
            return match.group(1) + body + match.group(3)

        def replace_style_attribute(match):
            quote = match.group(1)
            body = self.localize_css(
                match.group(2),
                page_url,
                "files/",
            )
            return "style=" + quote + body + quote

        text = re.sub(r"<link\b[^>]*>", replace_link, text, flags=re.I)
        text = re.sub(r"<source\b[^>]*>", "", text, flags=re.I)
        text = re.sub(r"<img\b[^>]*>", replace_image, text, flags=re.I)
        text = re.sub(
            r"(<style\b[^>]*>)(.*?)(</style\s*>)",
            replace_style_block,
            text,
            flags=re.I | re.S,
        )
        return re.sub(
            r"""\bstyle=(["'])(.*?)\1""",
            replace_style_attribute,
            text,
            flags=re.I | re.S,
        )


def without_active_content(text):
    """Remove active and embedded content from a fixed comparison page."""
    text = re.sub(
        r"<script\b[^>]*>.*?</script\s*>",
        "",
        text,
        flags=re.I | re.S,
    )
    return re.sub(
        r"<iframe\b[^>]*>.*?</iframe\s*>",
        "",
        text,
        flags=re.I | re.S,
    )


def normalized_links(text, base):
    """Return de-duplicated http(s) links from a source page."""
    parser = PageLinks()
    parser.feed(text)
    result = []
    seen = set()
    for raw in parser.links:
        url = urldefrag(absolute(raw, base))[0]
        if urlparse(url).scheme not in ("http", "https") or url in seen:
            continue
        seen.add(url)
        result.append(url)
    return result


def parse_pages(values):
    """Parse repeated TAG=URL options or return the Mozilla path."""
    if not values:
        return list(DEFAULT_PAGES)
    pages = []
    tags = set()
    for value in values:
        if "=" not in value:
            raise ValueError("--page must be TAG=URL")
        tag, url = value.split("=", 1)
        if (
            not re.fullmatch(r"[a-z0-9][a-z0-9-]*", tag)
            or urlparse(url).scheme not in ("http", "https")
            or tag in tags
        ):
            raise ValueError("invalid or duplicate page: " + value)
        tags.add(tag)
        pages.append((tag, url))
    return pages


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--out",
        default=str(ROOT / "build/ws074-sites"),
    )
    parser.add_argument("--page", action="append", metavar="TAG=URL")
    parser.add_argument("--user-agent", default=CHROME_USER_AGENT)
    parser.add_argument("--refetch", action="store_true")
    args = parser.parse_args()

    try:
        pages = parse_pages(args.page)
        out = Path(args.out).expanduser().resolve()
        out.mkdir(parents=True, exist_ok=True)
        capture = Capture(out, args.user_agent, args.refetch)
        records = []
        entry_links = set()
        entry_final = None
        for index, (tag, url) in enumerate(pages):
            source, metadata, fetched = capture.page(tag, url)
            text = source.read_text(encoding="utf-8", errors="replace")
            final_url = metadata["final_url"]
            if index == 0:
                entry_final = final_url
                entry_links = set(normalized_links(text, final_url))
            fixed = without_active_content(
                capture.localize_page(text, final_url)
            )
            fixed_path = out / (tag + "-local-noscript.html")
            fixed_path.write_text(fixed, encoding="utf-8")
            requested = urldefrag(url)[0]
            records.append(
                {
                    "tag": tag,
                    **metadata,
                    "discovered_from_entry": (
                        index == 0 or requested in entry_links
                    ),
                    "challenge_signals": challenge_signals(text),
                    "source": file_record(source, out),
                    "fixed": file_record(fixed_path, out),
                    "outgoing_links": normalized_links(text, final_url),
                }
            )
            print(
                "%s: HTTP %s, %d bytes, %d assets%s"
                % (
                    tag,
                    metadata["http_status"],
                    len(text.encode()),
                    len(capture.assets),
                    " (fetched)" if fetched else " (cached)",
                )
            )
            if fetched and index + 1 < len(pages):
                time.sleep(1)

        assets = []
        for name in sorted(capture.assets):
            path = capture.files / name
            if path.is_file():
                record = file_record(path, out)
                record["source_url"] = capture.assets[name]
                assets.append(record)
        manifest = {
            "schema": 1,
            "entry_url": entry_final,
            "user_agent": args.user_agent,
            "pages": records,
            "assets": assets,
            "asset_failures": capture.failures,
        }
        manifest_path = out / "capture-manifest.json"
        manifest_path.write_text(
            json.dumps(
                manifest,
                ensure_ascii=False,
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
        print(manifest_path)
        return 0
    except (
        OSError,
        RuntimeError,
        ValueError,
        subprocess.TimeoutExpired,
    ) as error:
        print("site-capture: %s" % error, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
