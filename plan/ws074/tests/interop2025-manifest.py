#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Join fixed Interop 2025 metadata labels to a fixed WPT MANIFEST.json.

Run with WPT's manifest virtualenv, which includes PyYAML. The input metadata
repository and WPT checkout stay outside the source tree. Unresolved IDs are
preserved; this tool never treats them as passed or excludes them silently.
"""

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import yaml


WPT_COMMIT = "2d66b9b7998bb58c336138c178323ddee857b586"
METADATA_COMMIT = "018c06b2c0fae50400c71585c73b7f238a2d029a"
LAYOUT_LABELS = (
    "interop-2021-flexbox", "interop-2023-flexbox", "interop-2021-grid",
    "interop-2023-grid", "interop-2022-subgrid",
)
POINTER_LABEL = "interop-2023-events"
FOCUS_NAMES = (
    "anchor-positioning", "backdrop-filter", "core-web-vitals", "details",
    "layout", "modules", "navigation", "pointer-and-mouse-events",
    "remove-mutation-events", "scope", "scrollend", "storageaccess",
    "textdecoration", "urlpattern", "view-transitions", "webassembly",
    "webcompat", "webrtc", "writingmodes",
)


def revision(directory):
    return subprocess.check_output(
        ["git", "-C", str(directory), "rev-parse", "HEAD"], text=True,
    ).strip()


def focus_for_label(label):
    if label in LAYOUT_LABELS:
        return "layout"
    if label == POINTER_LABEL:
        return "pointer-and-mouse-events"
    if label.startswith("interop-2025-"):
        name = label[len("interop-2025-"):]
        if name in FOCUS_NAMES:
            return name
    return None


def selected_ids(metadata_root):
    selected = defaultdict(set)
    for path in sorted(metadata_root.rglob("META.yml")):
        data = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
        for link in data.get("links", []):
            focus = focus_for_label(link.get("label", ""))
            if focus is None:
                continue
            for result in link.get("results", []):
                name = result.get("test")
                if not name or name == "*":
                    raise ValueError("unsupported metadata selector in %s: %r" % (path, name))
                identifier = "/" + (path.parent.relative_to(metadata_root) / name).as_posix()
                selected[identifier].add(focus)
    return selected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wpt", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    wpt = args.wpt.resolve()
    metadata = args.metadata.resolve()
    if revision(wpt) != WPT_COMMIT or revision(metadata) != METADATA_COMMIT:
        raise SystemExit("WPT or wpt-metadata revision does not match the fixed input")

    sys.path.insert(0, str(wpt))
    from tools.manifest import manifest as wpt_manifest

    manifest_path = wpt / "MANIFEST.json"
    if not manifest_path.is_file():
        raise SystemExit("generate WPT MANIFEST.json with ./wpt manifest --no-download")
    metadata_ids = selected_ids(metadata)
    wpt_data = wpt_manifest.load(str(wpt), str(manifest_path))
    if wpt_data is None:
        raise SystemExit("WPT manifest could not be loaded")
    found = {}
    for kind, _path, items in wpt_data:
        for item in items:
            identifier = item.id
            if identifier in metadata_ids:
                found[identifier] = {"type": kind, "source": item.path}

    selected = []
    missing = []
    for identifier, focus in sorted(metadata_ids.items()):
        row = {"id": identifier, "focus": sorted(focus)}
        if identifier in found:
            row.update(found[identifier])
            selected.append(row)
        else:
            missing.append(row)
    counts = {}
    for name in FOCUS_NAMES:
        counts[name] = {
            "selected": sum(name in row["focus"] for row in selected),
            "unresolved": sum(name in row["focus"] for row in missing),
        }
    report = {
        "wpt_commit": WPT_COMMIT,
        "metadata_commit": METADATA_COMMIT,
        "wpt_manifest_sha256": hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
        "focus_counts": counts,
        "selected_count": len(selected),
        "unresolved_count": len(missing),
        "selected": selected,
        "unresolved": missing,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("Interop 2025: %d selected, %d unresolved" % (len(selected), len(missing)))
    for name, values in counts.items():
        print("%s: %d selected, %d unresolved" % (name, values["selected"], values["unresolved"]))
    return 0 if not missing else 1


if __name__ == "__main__":
    sys.exit(main())
