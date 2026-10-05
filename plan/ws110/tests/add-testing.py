#!/usr/bin/env python3
"""ws110-p002: puts --testing on every finite compositor start of the tests.

Since WS110 the compositor started without a role is a user's desktop with
no deadline, and --timeout and --max-frames need --testing (role.c).  This
finds the starts that give a deadline or a frame limit without --testing
and adds it, in the forms the tree has:

  - a command line: /bin/wayland, /opt/keiland/bin/wayland or $wayland
    followed on the same line by --timeout= or --max-frames=: --testing
    goes right after the program;
  - a Python argv list: str(prefix/'bin/wayland'), followed by the options:
    '--testing', goes right after it;
  - a zdesktop service file (plan/ws031/tests/*/zdesktop): an arguments=
    line with --timeout=: --testing goes first.

It reads only the files git tracks, and leaves the records of the past
(plan/history, plan/uat, evidence, JSON checkpoints) and documents (.md,
.txt, .log) as they are.  It prints the count of such starts before and
after, and the files it changed.

    add-testing.py [--dry-run]

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import re
import subprocess
import sys

SKIP = re.compile(r"^(plan/history/|plan/uat/|docs/)|/evidence/|\.(json|md|txt|log|png|ppm)$")
PROGRAM = r"(?:/opt/keiland/bin/wayland|/bin/wayland|\$\{?wayland\}?)"
SHELL = re.compile(r"(" + PROGRAM + r")([ \t]+)(?=[^\n]*?--(?:timeout|max-frames)=)")
PYTHON = re.compile(r"(str\(prefix\s*/\s*'bin/wayland'\),)(?=[^\n]*'--(?:timeout|max-frames)=)")
SERVICE = re.compile(r"^(arguments=)(?=[^\n]*--(?:timeout|max-frames)=)", re.M)
LIMIT = re.compile(r"--(?:timeout|max-frames)=")


def finite_starts(text):
    """Counts the lines that start the compositor with a limit and no --testing."""
    count = 0
    for line in text.splitlines():
        if "--testing" in line or not LIMIT.search(line):
            continue
        if SHELL.search(line) or PYTHON.search(line) or SERVICE.search(line):
            count += 1
    return count


def add_testing(text):
    """Returns the text with --testing added to each finite start."""
    lines = text.split("\n")
    for index, line in enumerate(lines):
        if "--testing" in line or not LIMIT.search(line):
            continue
        line = SHELL.sub(r"\1\2--testing ", line, count=1)
        line = PYTHON.sub(r"\1'--testing',", line, count=1)
        line = SERVICE.sub(r"\1--testing ", line, count=1)
        lines[index] = line
    return "\n".join(lines)


def main():
    dry_run = "--dry-run" in sys.argv[1:]
    files = subprocess.run(["git", "ls-files"], capture_output=True, text=True,
                           check=True).stdout.split("\n")
    before = 0
    after = 0
    changed = []
    for path in files:
        if not path or SKIP.search(path) or path.startswith("plan/ws110/tests/add-testing"):
            continue
        try:
            with open(path, encoding="utf-8") as stream:
                text = stream.read()
        except (UnicodeDecodeError, FileNotFoundError, IsADirectoryError):
            continue
        count = finite_starts(text)
        if count == 0:
            continue
        before += count
        new_text = add_testing(text)
        after += finite_starts(new_text)
        if new_text != text:
            changed.append((path, count))
            if not dry_run:
                with open(path, "w", encoding="utf-8") as stream:
                    stream.write(new_text)
    for path, count in changed:
        print(f"{path}: {count}")
    print(f"add-testing: files={len(changed)} starts before={before} after={after}"
          f"{' (dry run)' if dry_run else ''}")


if __name__ == "__main__":
    main()
