#!/usr/bin/env python3
# ws245: compares the regression evidence of two bare starts of the Latitude 5330 (design.md §6.3): the surveys of
# the first picture, their judgement and the digest of the display writes, read from two whole kernel logs taken by
# plan/ws245/tests/5330-survey.sh.
#   python3 -I plan/ws245/tests/5330-survey-diff.py <reference dmesg> <stage dmesg>
# Prints a table of every surveyed register whose value differs (the frame counter, the scanline, the CRC result
# and the live surface move between any two starts and are listed apart), the two judgements and the two digests,
# and ends with PASS when the stage's judgement is "lit" and no steady register differs, FAIL otherwise.
# The digest of the writes changes between two starts of the same kernel (a read-modify-write that keeps hardware
# status bits: VARYING below), so a digest that differs is looked at in the whole lists: when both logs have them
# (started with i915.debug=display), every write that differs is printed, the VARYING registers apart, and a write
# that is in both lists at another place (another thread's timing) is listed as moved without failing.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import collections
import difflib
import re
import sys

# Writes whose value carries hardware status read back, seen to differ between two starts of the same kernel
# (2026-10-16): the combo PHY A compensation control (ICL_PORT_COMP_DW0(PHY A), set by the PHY init's rmw).
VARYING = {"162100"}
MOVING = {"PIPE_FRMCNT", "PIPE_SCANLINE", "PIPE_CRC_RES_1", "PLANE_SURFLIVE", "PLANE_SURF", "PIPE_CRC_CTL"}
SURVEY = re.compile(r"i915: survey ([12]) ([A-Z0-9_]+)=(0x[0-9a-f]{8}|unread)")
HEADER = re.compile(r"i915: survey ([12]) pipe ([A-D]) \((.*)\), live surface (.*), underruns (\d+)")
VERDICT = re.compile(r"i915: survey: (lit|NOT lit): (.*)")
DIGEST = re.compile(r"i915: w: (.*)")
WHOLE = re.compile(r"i915: w ([0-9a-f].*)")


def read(path):
    values = {}
    headers = {}
    verdict = None
    digest = None
    writes = []
    with open(path, encoding="utf-8", errors="replace") as log:
        for line in log:
            match = SURVEY.search(line)
            if match:
                values[(match.group(1), match.group(2))] = match.group(3)
                continue
            match = HEADER.search(line)
            if match:
                headers[match.group(1)] = match.group(0)
                continue
            match = VERDICT.search(line)
            if match:
                verdict = (match.group(1), match.group(2))
                continue
            match = DIGEST.search(line)
            if match:
                digest = match.group(1)
                continue
            match = WHOLE.search(line)
            if match:
                writes.extend(match.group(1).split())
    return values, headers, verdict, digest, writes


def main():
    if len(sys.argv) != 3:
        print(__doc__ or "usage: 5330-survey-diff.py <reference dmesg> <stage dmesg>")
        return 2
    reference = read(sys.argv[1])
    stage = read(sys.argv[2])
    steady_differs = 0
    print("| survey | register | reference | stage | kind |")
    print("| --- | --- | --- | --- | --- |")
    for key in sorted(set(reference[0]) | set(stage[0])):
        before = reference[0].get(key, "-")
        after = stage[0].get(key, "-")
        if before == after:
            continue
        kind = "moves" if key[1] in MOVING else "steady"
        if kind == "steady":
            steady_differs += 1
        print("| %s | %s | %s | %s | %s |" % (key[0], key[1], before, after, kind))
    for name, data in (("reference", reference), ("stage", stage)):
        for round_number in sorted(data[1]):
            print("%s: %s" % (name, data[1][round_number]))
        print("%s judgement: %s" % (name, " ".join(data[2]) if data[2] else "(none)"))
        print("%s digest: %s" % (name, data[3] if data[3] else "(none)"))
    if reference[3] and stage[3]:
        same = reference[3].split(";")[-1] == stage[3].split(";")[-1]
        print("digest of the writes: %s" % ("same" if same else "DIFFERS"))
    if reference[4] and stage[4]:
        # The lists are aligned, not compared position by position: the display writes come from more than one
        # thread (the start, the DMC load, the hotplug work), so a change of timing in one of them moves its writes
        # against the others without changing any write (seen 2026-10-16 with the AUX channel rewritten: the panel
        # VDD and two hotplug writes moved, the unordered digest the same).  A write that only moved is listed apart
        # and does not fail the stage; a write one list has and the other has not does.
        aligned = difflib.SequenceMatcher(a=reference[4], b=stage[4], autojunk=False)
        removed = collections.Counter()
        added = collections.Counter()
        for operation, first, last, other_first, other_last in aligned.get_opcodes():
            if operation == "equal":
                continue
            removed.update(reference[4][first:last])
            added.update(stage[4][other_first:other_last])
        moved = removed & added
        varying = 0
        steady = 0
        for write, count in sorted((removed - moved).items()):
            if write.split("=")[0] in VARYING:
                varying += count
                continue
            steady += count
            print("write only in the reference: %s (%d)" % (write, count))
        for write, count in sorted((added - moved).items()):
            if write.split("=")[0] in VARYING:
                varying += count
                continue
            steady += count
            print("write only in the stage: %s (%d)" % (write, count))
        for write, count in sorted(moved.items()):
            print("write in another place: %s (%d)" % (write, count))
        print("whole lists: %d writes differ, %d moved, %d more in registers that vary between starts" %
              (steady, sum(moved.values()), varying))
        steady_differs += steady
    lit = stage[2] is not None and stage[2][0] == "lit"
    print("steady registers that differ: %d" % steady_differs)
    print("PASS" if lit and steady_differs == 0 else "FAIL")
    return 0 if lit and steady_differs == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
