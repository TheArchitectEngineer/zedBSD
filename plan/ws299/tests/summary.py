# ws299-p001: summarizes the NVMe measurement logs of nvme-bench-5330.sh (bench-zedbsd.sh, bench-debian.sh).
#   python3 -I plan/ws299/tests/summary.py LOGS NAME
# For each LOGS/NAME-MODE.txt it prints one row a timed step: MB/s (10^6 bytes a second), and for zedBSD the dd's
# system time against its real time, the buffer cache's size after the step and the step's changes of the cache and
# I/O counters (the event names come from include/uapi/io-stats.h).  The table also goes to LOGS/NAME-summary.md.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import glob
import os
import re
import sys

EVENTS = ["IO_DRIVER_READ", "IO_DRIVER_WRITE", "IO_BUF_RUN_READ", "IO_BUF_RUN_WRITE", "IO_BUF_SINGLE_GEOMETRY",
          "IO_BUF_SINGLE_MEMORY", "IO_BUF_SINGLE_BUSY", "IO_BUF_SINGLE_HIT"]
SHORT = {"IO_DRIVER_READ": "drv_rd", "IO_DRIVER_WRITE": "drv_wr", "IO_BUF_RUN_READ": "run_rd",
         "IO_BUF_RUN_WRITE": "run_wr", "IO_BUF_SINGLE_GEOMETRY": "1_geom", "IO_BUF_SINGLE_MEMORY": "1_mem",
         "IO_BUF_SINGLE_BUSY": "1_busy", "IO_BUF_SINGLE_HIT": "1_hit"}


def event_numbers(header):
    """Reads the enum io_stat_event numbering from the UAPI header."""
    numbers = {}
    inside = False
    index = 0
    with open(header) as stream:
        for line in stream:
            if line.startswith("enum io_stat_event"):
                inside = True
                continue
            if not inside:
                continue
            match = re.match(r"\s*(IO_[A-Z0-9_]+)", line)
            if match:
                numbers[match.group(1)] = index
                index += 1
            if line.startswith("}"):
                break
    return numbers


def parse_snap(lines):
    """Turns one snap block into counters."""
    snap = {"io": {}, "sys_ticks": 0}
    for line in lines:
        match = re.match(r"vfs\.bufcache\.current_bytes: (\d+)", line)
        if match:
            snap["current"] = int(match.group(1))
        match = re.match(r"vfs\.bufcache\.max_bytes: (\d+)", line)
        if match:
            snap["max"] = int(match.group(1))
        if line.startswith("vfs.bufcache.stats:"):
            for key, value in re.findall(r"(\w+)=(\d+)", line):
                snap[key] = int(value)
        if line.startswith("vfs.cache_memory.stats:") and "managed=" in line:
            for key, value in re.findall(r"(\w+)=(\d+)", line):
                snap["cm_" + key] = int(value)
        match = re.match(r"vfs\.io\.stats: event=(\d+) calls=(\d+) bytes=(\d+)", line)
        if match:
            snap["io"][int(match.group(1))] = (int(match.group(2)), int(match.group(3)))
        match = re.match(r"hw\.cputimes: cpu=\d+ user=\d+ system=(\d+)", line)
        if match:
            snap["sys_ticks"] += int(match.group(1))
        match = re.match(r"hw\.cputimes: hz=(\d+)", line)
        if match:
            snap["hz"] = int(match.group(1))
    return snap


def zedbsd_rows(lines, numbers):
    """Pairs each timed step with the snap after it."""
    rows = []
    previous = None
    step = None
    snap_lines = None
    for line in lines:
        if line.startswith("snap "):
            snap_lines = []
            continue
        if line.startswith("endsnap "):
            snap = parse_snap(snap_lines)
            snap_lines = None
            if step is not None and previous is not None:
                step["after"] = snap
                step["before"] = previous
                rows.append(step)
                step = None
            previous = snap
            continue
        if snap_lines is not None:
            snap_lines.append(line)
            continue
        match = re.match(r"step (\S+) bytes=(\d+)", line)
        if match:
            step = {"name": match.group(1), "bytes": int(match.group(2))}
            continue
        match = re.match(r"(real|user|sys) ([\d.]+)", line)
        if match and step is not None:
            step[match.group(1)] = float(match.group(2))
    table = []
    header = "| step | MB/s | real s | sys s | cache MiB | misses | evict | cap_fail | " + \
        " | ".join(SHORT[name] for name in EVENTS) + " | cpu sys s | cm_refusals | cm_reclaimed MiB |"
    table.append(header)
    table.append("|" + " --- |" * (header.count("|") - 1))
    for row in rows:
        before = row["before"]
        after = row["after"]
        real = row.get("real", 0.0)
        rate = "-"
        if real > 0 and row["bytes"] > 0:
            rate = "%.1f" % (row["bytes"] / real / 1e6)
        cells = [row["name"], rate, "%.2f" % real, "%.2f" % row.get("sys", 0.0),
                 "%.0f" % (after.get("current", 0) / 1048576.0)]
        for key in ("misses", "evictions", "capacity_failures"):
            cells.append(str(after.get(key, 0) - before.get(key, 0)))
        for name in EVENTS:
            number = numbers[name]
            calls_after = after["io"].get(number, (0, 0))[0]
            calls_before = before["io"].get(number, (0, 0))[0]
            cells.append(str(calls_after - calls_before))
        hz = after.get("hz", 100) or 100
        cells.append("%.2f" % ((after["sys_ticks"] - before["sys_ticks"]) / float(hz)))
        cells.append(str(after.get("cm_refusals", 0) - before.get("cm_refusals", 0)))
        cells.append("%.0f" % ((after.get("cm_reclaimed", 0) - before.get("cm_reclaimed", 0)) / 1048576.0))
        table.append("| " + " | ".join(cells) + " |")
    return table


def debian_rows(lines):
    """Pairs each step with its dd or fio result."""
    table = ["| step | result |", "| --- | --- |"]
    step = None
    for line in lines:
        match = re.match(r"step (\S+)", line)
        if match:
            step = match.group(1)
            continue
        if step is None:
            continue
        match = re.search(r"copied, ([\d.]+) s, ([\d.]+ [kMG]?B/s)", line)
        if match:
            table.append("| %s | %s s, %s |" % (step, match.group(1), match.group(2)))
            step = None
            continue
        match = re.match(r"(read|write) bw_kib=(\d+) iops=(\d+) lat_us=([\d.]+)", line)
        if match:
            table.append("| %s | %s %.1f MB/s, %s IOPS, %s us |" % (
                step, match.group(1), int(match.group(2)) * 1024 / 1e6, match.group(3), match.group(4)))
            step = None
    return table


def main():
    logs, name = sys.argv[1], sys.argv[2]
    numbers = event_numbers(os.path.join(os.path.dirname(__file__), "..", "..", "..", "include", "uapi", "io-stats.h"))
    out = []
    for path in sorted(glob.glob(os.path.join(logs, name + "-*.txt"))):
        if path.endswith("-dmesg.txt"):
            continue
        with open(path, errors="replace") as stream:
            lines = [line.rstrip("\n") for line in stream]
        out.append("### " + os.path.basename(path))
        out.append("")
        heads = [line for line in lines if line.startswith(("Linux", "zedBSD", "hmb ", "hw.ncpu", "swapoff",
                                                             "budget", "bench done", "bench-debian"))]
        out.extend("    " + line for line in heads[:8])
        out.append("")
        if any(line.startswith("snap ") for line in lines):
            out.extend(zedbsd_rows(lines, numbers))
        else:
            out.extend(debian_rows(lines))
        out.append("")
    text = "\n".join(out)
    print(text)
    with open(os.path.join(logs, name + "-summary.md"), "w") as stream:
        stream.write(text + "\n")


main()
