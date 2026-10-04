#!/usr/bin/env python3
"""ws045-p001: counts the GNU extensions of the base utilities that real
build scripts use.

It reads the script-like files (configure, *.sh, Makefile*, *.mk, *.am,
*.in, *.ac, *.m4, *.pl, *.awk, *.sed) of unpacked source trees, finds the
invocations of the base utilities, and reports each option (and a few
features inside sed, grep and awk programs) with the number of packages
and files that use it.  The count is heuristic: it reads command lines
textually and does not run anything.

  python3 plan/tools/gnu-utils/survey.py TREE_DIR [--util sed] [--examples N]

TREE_DIR holds one directory per package (for example the files of every
tarball in build/distfiles unpacked there).
"""

import argparse
import collections
import os
import re
import sys
from pathlib import Path

UTILITIES = [
	"sed", "grep", "egrep", "fgrep", "awk", "gawk", "xargs", "find",
	"sort", "head", "tail", "cp", "mv", "rm", "ln", "install", "date",
	"stat", "readlink", "realpath", "mktemp", "cut", "tr", "uniq", "wc",
	"touch", "mkdir", "basename", "dirname", "tac", "seq", "expr", "od",
	"ls", "du", "env", "timeout", "chmod", "tee", "diff", "cmp", "paste",
	"split", "nl", "fold", "comm", "join", "truncate", "sha256sum",
	"md5sum", "base64", "tsort", "cat",
]

SUFFIXES = (".sh", ".mk", ".am", ".in", ".ac", ".m4", ".pl", ".awk",
	    ".sed", ".bash")
NAMES = ("configure", "Makefile", "GNUmakefile", "makefile", "ltmain.sh",
	 "install-sh", "depcomp", "mkinstalldirs", "config.guess")

TEST_DIRECTORIES = {"tests", "test", "t", "testsuite", "testing",
		    "gnulib-tests"}

# Words that make the following name a subcommand, not the utility.
NOT_COMMANDS = re.compile(r"(?:git|svn|hg|cvs|apt|dnf|pip)\s+$")

# An invocation: the name after a command separator, then its options.
INVOCATION = re.compile(
	r"(?:^|[\s;|&(`{]|\$\()(?:\$\{?(?:[A-Z_]*)\}?/)?(%s)"
	r"((?:[ \t]+(?:-[-A-Za-z0-9=_.,+]+|'[^']*'|\"[^\"]*\"|[^\s;|&)`]+))*)"
	% "|".join(re.escape(u) for u in UTILITIES))

# Features inside sed programs (text in quotes after sed).
SED_FEATURES = {
	r"\+ (BRE)": re.compile(r"(?<!\\)\\\+"),
	r"\? (BRE)": re.compile(r"(?<!\\)\\\?"),
	r"\| (BRE)": re.compile(r"(?<!\\)\\\|"),
	r"\w \W": re.compile(r"(?<!\\)\\[wW]"),
	r"\s \S": re.compile(r"(?<!\\)\\[sS]"),
	r"\b \B": re.compile(r"(?<!\\)\\[bB]"),
	r"\< \>": re.compile(r"(?<!\\)\\[<>]"),
	r"\` \'": re.compile(r"(?<!\\)\\`"),
	r"\U \L \u \l \E": re.compile(r"(?<!\\)\\[ULulE]"),
	r"\n in replacement or [^\n]": re.compile(r"\[\^?[^]]*\\n"),
	r"\t": re.compile(r"(?<!\\)\\t"),
	r"0,/re/": re.compile(r"(?:^|['\s;])0,/"),
	r"first~step": re.compile(r"(?:^|['\s;])\d+~\d+"),
	r"addr,+N": re.compile(r"/,\+\d"),
	r"s///I or /re/I": re.compile(r"/[gpI0-9]*I[gp0-9]*(?:[\s;'}]|$)"),
	r"s///M": re.compile(r"/[gpI0-9]*M(?:[\s;'}]|$)"),
	r"s///e": re.compile(r"s(.)[^\1]*\1[^\1]*\1[gp0-9]*e(?:[\s;'}]|$)"),
	r"one-liner { cmd }": re.compile(r"\{[^}\n]*;\s*\}"),
	r"cmd after } or a/i/c one-liner": re.compile(r"(?:^|;|\s)[aic][ \t]+[^\\\s]"),
	r"Q/q exit code": re.compile(r"(?:^|;|\s)[qQ]\s*\d"),
	r"T command": re.compile(r"(?:^|;|\s)T(?:\s|;|$)"),
	r"F / z / W / R / e / v": re.compile(r"(?:^|;)\s*[FzWRev](?:\s|;|$)"),
}

GREP_FEATURES = {
	r"\+ (BRE)": re.compile(r"(?<!\\)\\\+"),
	r"\? (BRE)": re.compile(r"(?<!\\)\\\?"),
	r"\| (BRE)": re.compile(r"(?<!\\)\\\|"),
	r"\w \s \b \< \>": re.compile(r"(?<!\\)\\[wWsSbB<>]"),
}

AWK_FEATURES = {
	"gensub": re.compile(r"\bgensub\s*\("),
	"strftime/systime": re.compile(r"\b(?:strftime|systime)\s*\("),
	"tolower/toupper": re.compile(r"\bto(?:lower|upper)\s*\("),
	"length(arr) / length without ()": re.compile(r"\blength\b(?!\s*\()"),
	"**": re.compile(r"\*\*"),
	"func": re.compile(r"\bfunc\s+\w+\s*\("),
	"IGNORECASE": re.compile(r"\bIGNORECASE\b"),
	"RT / FPAT / FIELDWIDTHS": re.compile(r"\b(?:RT|FPAT|FIELDWIDTHS)\b"),
	"asort/asorti": re.compile(r"\basorti?\s*\("),
	"PROCINFO": re.compile(r"\bPROCINFO\b"),
	"BEGINFILE/ENDFILE": re.compile(r"\b(?:BEGINFILE|ENDFILE)\b"),
	"nextfile": re.compile(r"\bnextfile\b"),
	"delete arr (whole)": re.compile(r"\bdelete\s+\w+\s*(?:[;}\n]|$)"),
	"fflush": re.compile(r"\bfflush\s*\("),
	"@include / @load": re.compile(r"@(?:include|load)\b"),
	"\\x escape": re.compile(r"\\x[0-9a-fA-F]"),
	"RS regex (multi-char)": re.compile(r"\bRS\s*=\s*\"[^\"]{2,}\""),
	"and/or/xor/lshift/rshift": re.compile(r"\b(?:and|or|xor|lshift|rshift|compl)\s*\("),
	"match(s, re, arr)": re.compile(r"\bmatch\s*\([^,()]+,[^,()]+,"),
	"split(s, a, re, seps)": re.compile(r"\bsplit\s*\([^()]*,[^()]*,[^()]*,"),
	"patsplit": re.compile(r"\bpatsplit\s*\("),
	"switch": re.compile(r"\bswitch\s*\("),
	"|& coprocess": re.compile(r"\|&"),
}


def script_files(tree: Path):
	"""Yields the script-like files below a package tree."""
	for directory, subdirectories, files in os.walk(tree):
		# Test suites exercise their own programs' options, not the
		# build's needs, so they are left out.
		subdirectories[:] = [name for name in subdirectories
				     if name not in TEST_DIRECTORIES]
		for name in files:
			if name.endswith(SUFFIXES) or name.startswith(NAMES):
				yield Path(directory) / name


def options_of(text: str) -> list[str]:
	"""Returns the option words of one invocation's argument text."""
	words = re.findall(r"'[^']*'|\"[^\"]*\"|\S+", text)
	options = []
	for word in words:
		if word == "--":
			break
		if word.startswith("--"):
			options.append(word.split("=", 1)[0])
			continue
		if re.fullmatch(r"-[A-Za-z0-9]+", word):
			options.append(word)
			continue
		if word.startswith("-"):
			continue
		# find's primaries follow the paths; keep reading them.
		continue
	return options


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("tree")
	parser.add_argument("--util")
	parser.add_argument("--examples", type=int, default=0)
	arguments = parser.parse_args()

	usage = collections.defaultdict(lambda: collections.defaultdict(set))
	files = collections.defaultdict(lambda: collections.defaultdict(int))
	examples = collections.defaultdict(list)
	for package in sorted(Path(arguments.tree).iterdir()):
		if not package.is_dir():
			continue
		for path in script_files(package):
			try:
				text = path.read_text(errors="replace")
			except OSError:
				continue
			for line in text.split("\n"):
				if len(line) > 2000:
					continue
				for match in INVOCATION.finditer(line):
					utility = match.group(1)
					if NOT_COMMANDS.search(line[:match.start(1)]):
						continue
					rest = match.group(2)
					keys = []
					for option in options_of(rest):
						if utility == "find":
							continue
						keys.append(option)
					if utility == "find":
						for primary in re.findall(r"(?<![\w-])-(?:maxdepth|mindepth|print0|iname|iregex|regex|delete|readable|executable|empty|wholename|ipath|samefile|printf|fprint|quit|lname|ilname|newermt|xtype|regextype)\b", rest):
							keys.append(primary)
					if utility == "sed":
						for name, pattern in SED_FEATURES.items():
							if pattern.search(rest):
								keys.append("[prog] " + name)
					if utility in ("grep", "egrep"):
						for name, pattern in GREP_FEATURES.items():
							if pattern.search(rest):
								keys.append("[re] " + name)
					if utility in ("awk", "gawk"):
						for name, pattern in AWK_FEATURES.items():
							if pattern.search(rest):
								keys.append("[prog] " + name)
					for key in keys:
						usage[utility][key].add(package.name)
						files[utility][key] += 1
						if len(examples[(utility, key)]) < arguments.examples:
							examples[(utility, key)].append("%s: %s" % (path, line.strip()[:200]))
	for utility in sorted(usage):
		if arguments.util and utility != arguments.util:
			continue
		print("== %s" % utility)
		ranked = sorted(usage[utility].items(), key=lambda item: (-len(item[1]), item[0]))
		for key, packages in ranked:
			print("  %-34s %3d pkgs %5d lines  %s" % (key, len(packages), files[utility][key], " ".join(sorted(packages))[:150]))
			for example in examples[(utility, key)]:
				print("      " + example)
	return 0


if __name__ == "__main__":
	sys.exit(main())
