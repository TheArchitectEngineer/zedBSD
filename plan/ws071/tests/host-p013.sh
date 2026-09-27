#!/bin/sh
# ws071-p013: the tabs of zdesktop-files on the host (files-render, host-render.c).
# Builds nothing: run host-build.sh first.  Each case runs files-render on a fresh sample home,
# started in Documents, and checks the lines it prints:
#  1. Ctrl+T twice: three tabs, the new one shown each time, all in Documents (two.png, three.png).
#  2. Ctrl+PageUp, Ctrl+Tab (around the end), Ctrl+Shift+Tab, Ctrl+PageDown move between the tabs.
#  3. Ctrl+W closes the shown tab; the last tab's Close Tab asks to close the window (request 4).
#  4. A middle click on a place of the sidebar (Downloads) opens it in a new tab.
#  5. A click on a tab shows it; a click on its close button closes it (closed.png).
#  6. The menus' state counts the tabs (the tab items need two).
#
#   plan/ws071/tests/host-p013.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws071-p013-host}
mkdir -p "$out"
home=$(pwd)/build/ws071-host/home
docs=$home/Documents
status=0

# Runs files-render on a fresh home, started in Documents, with the actions given; the output goes to $out/NAME.txt.
run() {
	name=$1
	shift
	sh plan/ws071/tests/host-run.sh --fresh "--start=$docs" "$@" > "$out/$name.txt" 2>&1
}

# Fails the run unless the output of a case has a line matching a pattern.
expect() {
	if grep -qE "$2" "$out/$1.txt"; then
		echo "$1: $2 ok"
	else
		echo "$1: $2 MISSING"
		status=1
	fi
}

# 1. New tabs.
run new key=20:2 draw="$out/two.ppm" key=20:2 tabs draw="$out/three.ppm"
expect new "TABS new index=1 count=2"
expect new "TABS new index=2 count=3"
expect new "^tabs count=3 shown=2 0=$docs 1=$docs 2=$docs\$"

# 2. Moving between them (Ctrl+PageUp, Ctrl+Tab twice, Ctrl+Shift+Tab, Ctrl+PageDown).
run step key=20:2 key=20:2 key=104:2 tabs key=15:2 tabs key=15:2 tabs key=15:3 tabs key=109:2 tabs
expect step "^tabs count=3 shown=1 "
expect step "^tabs count=3 shown=2 "
expect step "^tabs count=3 shown=0 "
expect step "TABS select index=2 count=3"
expect step "TABS select index=0 count=3"

# 3. Closing: Ctrl+W, then the last tab's Close Tab (action 42) asks to close the window.
run close key=20:2 key=17:2 tabs action=42
expect close "TABS close index=1 count=1 shown=0"
expect close "^tabs count=1 shown=0 0=$docs\$"
expect close "^request 4\$"

# 4. A middle click on Downloads in the sidebar.
run middle middle=100,150 tabs
expect middle "^tabs count=2 shown=1 0=$docs 1=$home/Downloads\$"

# 5. The row of tabs at the top of the content panel: a click on the first tab, then on its close button.
run bar key=20:2 click=350,27 tabs click=655,27 tabs draw="$out/closed.ppm"
expect bar "^tabs count=2 shown=0 "
expect bar "TABS close index=0 count=1 shown=0"

# 6. The menus' state.
run state state key=20:2 state
expect state "tabs=1"
expect state "tabs=2"

exit $status
