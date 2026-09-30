#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Capture public pages and their drawing resources for offline comparisons.

The defaults start at Mozilla's Japanese home page and follow three prominent
same-origin links found there.  Additional targets can be selected with
repeated --page TAG=URL arguments.  Source pages and localized, script-free
variants are written below build/.  Repeated --active-page TAG also writes a
localized active variant for selected pages; no downloaded content enters the
source tree.
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


def local_reference(value):
    """Tell whether a rewritten URL already names one capture asset."""
    if LOCAL_ASSET.fullmatch(value):
        return True
    return value.startswith("files/") and LOCAL_ASSET.fullmatch(value[6:]) is not None


def decode_html(path):
    """Decode one source page using its BOM or early HTML charset label."""
    data = path.read_bytes()
    if data.startswith(b"\xef\xbb\xbf"):
        return data.decode("utf-8-sig", errors="replace"), "utf-8"
    head = data[:8192].decode("ascii", errors="ignore")
    match = re.search(r"charset\s*=\s*[\"']?([A-Za-z0-9._-]+)", head, re.I)
    encoding = match.group(1).lower() if match else "utf-8"
    aliases = {"shift_jis": "cp932", "shift-jis": "cp932", "sjis": "cp932"}
    encoding = aliases.get(encoding, encoding)
    try:
        return data.decode(encoding, errors="replace"), encoding
    except LookupError:
        return data.decode("utf-8", errors="replace"), "utf-8"


def declare_utf8(text):
    """Make a decoded fixed page state its actual UTF-8 encoding."""
    text, count = re.subn(
        r"(?i)(charset\s*=\s*)[A-Za-z0-9._-]+",
        r"\1utf-8", text, count=1,
    )
    if count == 0:
        text = re.sub(r"(?i)<head([^>]*)>", r'<head\1><meta charset="utf-8">',
                      text, count=1)
    return text


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

    def fetch(self, url, path, use_cookies=False, keep_error=False):
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
        if result.returncode != 0:
            temporary.unlink(missing_ok=True)
            detail = result.stderr.strip() or "curl failed"
            return status, final_url, detail
        if status != "200":
            if keep_error and temporary.is_file():
                temporary.replace(path)
            else:
                temporary.unlink(missing_ok=True)
            return status, final_url, "HTTP " + status
        temporary.replace(path)
        return status, final_url, ""

    def page(self, tag, url):
        """Read a cached source page or fetch it once with metadata."""
        path = self.out / (tag + ".html")
        metadata_path = self.out / (tag + "-fetch.json")
        fetched = False
        if self.refetch or not path.is_file() or not metadata_path.is_file():
            status, final_url, error = self.fetch(
                url, path, use_cookies=True, keep_error=True
            )
            metadata = {
                "http_status": int(status) if status.isdigit() else 0,
                "requested_url": url,
                "final_url": final_url,
                "fetched_at_utc": datetime.datetime.now(
                    datetime.timezone.utc
                ).isoformat(),
            }
            if error:
                metadata["capture_error"] = error
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

    def flatten_frames(self, text, page_url):
        """Turn one simple frameset into a fixed flex document."""
        frameset = re.search(
            r"<frameset\b([^>]*)>(.*?)</frameset\s*>", text, re.I | re.S
        )
        if frameset is None:
            return text, False
        frame_tags = re.findall(r"<frame\b[^>]*>", frameset.group(2), re.I)
        if not frame_tags:
            return text, False

        attrs = frameset.group(1)
        axis = "cols"
        sizes = None
        for candidate in ("cols", "rows"):
            match = re.search(
                r"\b%s\s*=\s*([\"']?)([^\"' >]+)\1" % candidate,
                attrs, re.I,
            )
            if match is not None:
                axis = candidate
                sizes = match.group(2).split(",")
                break
        if sizes is None or len(sizes) != len(frame_tags):
            sizes = ["1"] * len(frame_tags)

        weights = []
        for size in sizes:
            match = re.search(r"\d+(?:\.\d+)?", size)
            weights.append(float(match.group(0)) if match else 1.0)
        total = sum(weights) or float(len(weights))

        bodies = []
        for index, tag in enumerate(frame_tags):
            source = re.search(r"\bsrc\s*=\s*([\"'])(.*?)\1", tag, re.I | re.S)
            if source is None:
                continue
            frame_url = absolute(source.group(2), page_url)
            name = self.mirror(frame_url)
            if name is None:
                continue
            frame_path = self.files / name
            frame_text, unused_encoding = decode_html(frame_path)
            frame_text = declare_utf8(frame_text)
            body = re.search(
                r"<body\b([^>]*)>(.*?)</body\s*>", frame_text, re.I | re.S
            )
            body_attrs = body.group(1) if body is not None else ""
            body_text = body.group(2) if body is not None else frame_text
            body_text = self.localize_page(
                without_active_content(body_text), frame_url
            )
            styles = ["overflow:hidden"]
            color = re.search(
                r"\bbgcolor\s*=\s*([\"']?)([^\"' >]+)\1",
                body_attrs, re.I,
            )
            if color is not None:
                styles.append("background-color:" + color.group(2))
            background = re.search(
                r"\bbackground\s*=\s*([\"']?)([^\"' >]+)\1",
                body_attrs, re.I,
            )
            if background is not None:
                image = self.mirror(absolute(background.group(2), frame_url))
                if image is not None:
                    styles.append('background-image:url("files/%s")' % image)
            basis = 100.0 * weights[index] / total
            styles.append("flex:0 0 %.6g%%" % basis)
            bodies.append(
                '<div class="zed-frame" style="%s">%s</div>'
                % (";".join(styles), body_text)
            )
        if not bodies:
            return text, False

        title = re.search(r"<title\b[^>]*>(.*?)</title\s*>", text, re.I | re.S)
        title_text = title.group(1) if title is not None else ""
        direction = "column" if axis == "rows" else "row"
        fixed = (
            '<!doctype html><html><head><meta charset="utf-8"><title>%s</title>'
            '<style>html,body{margin:0;min-height:100%%}'
            '.zed-frames{display:flex;flex-direction:%s;width:100%%;min-height:900px}'
            '</style></head><body><div class="zed-frames">%s</div></body></html>'
            % (title_text, direction, "".join(bodies))
        )
        return fixed, True

    def localize_css(self, text, css_url, prefix=""):
        """Replace imports and url() values with mirrored relative resources."""
        imports = []

        def replace_import(match):
            raw = match.group(2).strip()
            if local_reference(raw):
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
            cleaned = unescape(raw).strip().lstrip("\\\"'")
            if (
                cleaned.startswith(("data:", "blob:", "#", "%23"))
                or local_reference(cleaned)
            ):
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
                if local_reference(href.group(2)):
                    return tag
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
            if source is None:
                return tag
            raw = source.group(2).strip()
            if (
                raw.startswith(("data:", "blob:", "#", "%23"))
                or local_reference(raw)
            ):
                return tag
            name = self.mirror(absolute(raw, page_url))
            if name is None:
                return tag
            replacement = "src=%sfiles/%s%s" % (
                source.group(1),
                name,
                source.group(1),
            )
            return tag.replace(source.group(0), replacement)

        def replace_script(match):
            tag = match.group(0)
            source = re.search(r"""\bsrc=(["'])(.*?)\1""", tag, re.I | re.S)
            if source is None:
                return tag
            raw = source.group(2).strip()
            if raw.startswith(("data:", "blob:", "#", "%23")) or local_reference(raw):
                return tag
            name = self.mirror(absolute(raw, page_url))
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
        text = re.sub(r"<script\b[^>]*>", replace_script, text, flags=re.I)
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
    parser.add_argument("--active-page", action="append", default=[], metavar="TAG")
    parser.add_argument("--user-agent", default=CHROME_USER_AGENT)
    parser.add_argument("--refetch", action="store_true")
    args = parser.parse_args()

    try:
        pages = parse_pages(args.page)
        page_tags = {tag for tag, unused_url in pages}
        active_pages = set(args.active_page)
        unknown_active = active_pages - page_tags
        if unknown_active:
            raise ValueError(
                "--active-page does not name a captured page: "
                + ", ".join(sorted(unknown_active))
            )
        out = Path(args.out).expanduser().resolve()
        out.mkdir(parents=True, exist_ok=True)
        capture = Capture(out, args.user_agent, args.refetch)
        records = []
        entry_links = set()
        entry_final = None
        for index, (tag, url) in enumerate(pages):
            source, metadata, fetched = capture.page(tag, url)
            final_url = metadata["final_url"]
            text = ""
            encoding = None
            if source.is_file():
                text, encoding = decode_html(source)
            if index == 0:
                entry_final = final_url
                entry_links = set(normalized_links(text, final_url))

            requested = urldefrag(url)[0]
            record = {
                "tag": tag,
                **metadata,
                "discovered_from_entry": (
                    index == 0 or requested in entry_links
                ),
                "challenge_signals": challenge_signals(text),
                "source_encoding": encoding,
                "outgoing_links": normalized_links(text, final_url),
            }
            if source.is_file():
                record["source"] = file_record(source, out)

            if metadata["http_status"] == 200:
                fixed, flattened = capture.flatten_frames(text, final_url)
                if tag in active_pages:
                    active_fixed = declare_utf8(
                        capture.localize_page(fixed, final_url)
                    )
                    active_path = out / (tag + "-local.html")
                    active_path.write_text(active_fixed, encoding="utf-8")
                    record["active_fixed"] = file_record(active_path, out)
                    fixed = without_active_content(active_fixed)
                else:
                    fixed = declare_utf8(capture.localize_page(
                        without_active_content(fixed), final_url
                    ))
                fixed_path = out / (tag + "-local-noscript.html")
                fixed_path.write_text(fixed, encoding="utf-8")
                record["fixed"] = file_record(fixed_path, out)
                record["frames_flattened"] = flattened
            records.append(record)
            print(
                "%s: HTTP %s, %d bytes, %d assets%s%s"
                % (
                    tag,
                    metadata["http_status"],
                    len(source.read_bytes()) if source.is_file() else 0,
                    len(capture.assets),
                    " (fetched)" if fetched else " (cached)",
                    " [excluded]" if metadata["http_status"] != 200 else "",
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
            "schema": 3,
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
