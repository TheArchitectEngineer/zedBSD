#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
#
# Summarizes the compositor's --log-frames lines (KWL LAT, KWL SCENE) per frame:
# input->draw, record, vkQueueSubmit, vkQueuePresentKHR, fence and input->shown,
# split by whether the frame reused the cursor-free scene (ws141-p013).
import re
import statistics
import sys

LAT = re.compile(r"KWL LAT (draw|acquired|recorded|queued|submit|shown) frame=(\d+)(.*?)at_us=(\d+)")
INPUT = re.compile(r"input_us=(\d+)")
SCENE = re.compile(r"KWL SCENE frame=(\d+) reuse=(\d)")


def summary(name, values):
    if not values:
        return "%-14s n=0" % name
    values = sorted(values)
    return "%-14s n=%3d min=%7.1f med=%7.1f p90=%7.1f max=%7.1f ms" % (
        name, len(values), values[0], statistics.median(values),
        values[min(len(values) - 1, int(len(values) * 0.9))], values[-1])


def main():
    frames = {}
    reuse = {}
    for line in open(sys.argv[1], errors="replace"):
        match = LAT.search(line)
        if match:
            stage, frame, middle, at = match.groups()
            entry = frames.setdefault(int(frame), {})
            entry[stage] = int(at)
            found = INPUT.search(middle)
            if stage == "draw" and found:
                entry["input"] = int(found.group(1))
            continue
        match = SCENE.search(line)
        if match:
            reuse[int(match.group(1))] = int(match.group(2))

    first = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    groups = {0: {}, 1: {}}
    for frame, entry in sorted(frames.items()):
        if frame < first or "shown" not in entry or "draw" not in entry:
            continue
        group = groups[reuse.get(frame, 0)]
        def add(name, start, end):
            if start in entry and end in entry and entry[start] > 0:
                group.setdefault(name, []).append((entry[end] - entry[start]) / 1000.0)
        add("input->draw", "input", "draw")
        add("acquire", "draw", "acquired")
        add("record", "acquired", "recorded")
        add("queue-submit", "recorded", "queued")
        add("present", "queued", "submit")
        add("fence", "submit", "shown")
        add("draw->shown", "draw", "shown")
        add("input->shown", "input", "shown")

    for key, label in ((0, "scene redrawn"), (1, "scene reused")):
        print("== %s ==" % label)
        for name in ("input->draw", "acquire", "record", "queue-submit", "present",
                     "fence", "draw->shown", "input->shown"):
            print(summary(name, groups[key].get(name, [])))


if __name__ == "__main__":
    main()
