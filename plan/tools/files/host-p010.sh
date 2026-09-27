#!/bin/sh
# ws071-p010: dragging items within files on the host (files-render, host-render.c).
# Builds nothing: run host-build.sh first.  Each case runs files-render on a fresh sample home, started in
# Projects/zedBSD (docs, src, Makefile, README.md), and checks the lines it prints and the files:
#  1. README.md dragged onto the folder docs: moved (move.png shows the drag over docs).
#  2. Ctrl: Makefile dragged onto src is copied (copy.png: the plus badge).
#  3. Ctrl+Shift: README.md dragged onto src makes a link there.
#  4. Two selected items (Makefile, then Ctrl+README.md); a press on a selected one keeps both, and the drag
#     onto Documents in the sidebar moves both (multi.png: the count badge).
#  5. README.md dragged onto the Trash in the sidebar goes to the trash.
#  6. README.md dragged onto the first tag in the sidebar gets the tag.
#  7. README.md dragged onto the other tab (Documents, opened by a middle click) moves there.
#  8. Esc gives up a drag: nothing moves.
#  9. Without a drag, a click on one of two selected items selects it alone; a drag of a folder onto the
#     empty part of the folder shown drops nothing.
# 10. The folder docs dragged onto the Favorites' title is added to the sidebar (favorite.png).
# 11. A favorite dragged onto another moves there in the list (reorder.png); a click on a favorite still goes.
# (A press soon after a click on the same item would be a double click: the cases wait first.)
#
#   plan/tools/files/host-p010.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws071-p010-host}
mkdir -p "$out"
home=$(pwd)/build/ws071-host/home
project=$home/Projects/zedBSD
status=0

# Runs files-render on a fresh home, started in Projects/zedBSD, with the actions given; the output goes to $out/NAME.txt.
run() {
	name=$1
	shift
	sh plan/tools/files/host-run.sh --fresh "--start=$project" "$@" > "$out/$name.txt" 2>&1
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

# Fails the run when the output of a case has a line matching a pattern.
refuse() {
	if grep -qE "$2" "$out/$1.txt"; then
		echo "$1: $2 FOUND"
		status=1
	else
		echo "$1: no $2 ok"
	fi
}

# Fails the run unless a shell test holds (after a case).
holds() {
	if eval "$2"; then
		echo "$1: $2 ok"
	else
		echo "$1: $2 MISSING"
		status=1
	fi
}

# The items' places (icon view, one tab): docs, src, Makefile, README.md; the sidebar's Documents, Trash and first tag.
docs=335,130
src=447,130
makefile=559,130
readme=671,130

# 1. A move onto a folder.
run move press=$readme drag=640,132 drag=340,135 draw="$out/move.ppm" release=340,135 wait=400 wait=400
expect move "DRAG start items=1$"
expect move "DRAG target kind=folder path=$project/docs$"
expect move "DRAG drop operation=move items=1 destination=$project/docs$"
expect move "TASK done id=[0-9]+ kind=move state=done files=1 "
holds move "[ -f '$project/docs/README.md' ] && [ ! -e '$project/README.md' ]"

# 2. Ctrl copies.
run copy mods=2 press=$makefile drag=530,132 drag=450,135 draw="$out/copy.ppm" release=450,135 wait=400 wait=400
expect copy "DRAG drop operation=copy items=1 destination=$project/src$"
expect copy "TASK done id=[0-9]+ kind=copy state=done files=1 "
holds copy "[ -f '$project/src/Makefile' ] && [ -f '$project/Makefile' ]"

# 3. Ctrl+Shift links.
run link mods=3 press=$readme drag=640,132 drag=450,135 release=450,135 wait=400 wait=400
expect link "DRAG drop operation=link items=1 destination=$project/src$"
holds link "[ -L '$project/src/README.md' ] && [ -f '$project/README.md' ]"

# 4. Two items onto Documents in the sidebar.
run multi click=$makefile click=$readme:2 mods=0 wait=500 press=$readme drag=640,132 drag=118,125 draw="$out/multi.ppm" release=118,125 wait=400 wait=400
expect multi "DRAG start items=2$"
expect multi "DRAG target kind=folder path=$home/Documents$"
expect multi "DRAG drop operation=move items=2 destination=$home/Documents$"
holds multi "[ -f '$home/Documents/README.md' ] && [ -f '$home/Documents/Makefile' ] && [ ! -e '$project/Makefile' ]"

# 5. The Trash.
run trash press=$readme drag=640,132 drag=118,343 release=118,343 wait=400 wait=400
expect trash "DRAG target kind=trash$"
expect trash "DRAG drop operation=trash items=1$"
expect trash "TASK done id=[0-9]+ kind=trash state=done "
holds trash "[ ! -e '$project/README.md' ]"

# 6. A tag.
run tag press=$readme drag=640,132 drag=118,441 release=118,441
expect tag "DRAG target kind=tag tag=[A-Za-z]+$"
expect tag "DRAG drop operation=tag items=1 tag=[A-Za-z]+$"
expect tag "TAG tag=[A-Za-z]+ on=1 items=1"

# 7. The other tab (Documents); with two tabs the items are 30 pixels lower.
run tab middle=118,125 click=350,27 press=671,160 drag=640,162 drag=885,27 release=885,27 wait=400 wait=400
expect tab "DRAG target kind=folder path=$home/Documents$"
expect tab "DRAG drop operation=move items=1 destination=$home/Documents$"
holds tab "[ -f '$home/Documents/README.md' ]"

# 8. Esc.
run cancel press=$readme drag=640,132 drag=340,135 key=1 release=340,135 wait=400
expect cancel "DRAG cancel$"
refuse cancel "DRAG drop"
holds cancel "[ -f '$project/README.md' ] && [ ! -e '$project/docs/README.md' ]"

# 9. A click without a drag on one of two selected items; a folder dropped on its own folder's ground.
run plain click=$makefile click=$readme:2 items mods=0 wait=500 click=$makefile items press=$docs drag=360,140 drag=400,400 release=400,400
expect plain "^item 2 Makefile selected=1$"
expect plain "^item 3 README.md selected=0$"
expect plain "DRAG target kind=none$"
expect plain "DRAG drop operation=none$"

# 10. The folder docs dragged onto the Favorites' title: added to the sidebar (its list file).
sidebar=$home/.config/files/sidebar
run favorite press=$docs drag=360,132 drag=118,35 draw="$out/favorite.ppm" release=118,35
expect favorite "DRAG target kind=favorites$"
expect favorite "FAVORITE add path=$project/docs$"
expect favorite "DRAG drop operation=favorites added=1$"
holds favorite "tail -1 '$sidebar' | grep -qx '$project/docs'"

# 11. Pictures (the fifth place) dragged onto Desktop (the second): first in the list; a click on the fourth
#     place, Documents now, still goes there (at the release).
run reorder press=118,185 drag=118,170 drag=118,95 draw="$out/reorder.ppm" release=118,95 click=118,155
expect reorder "DRAG start place=4 path=$home/Pictures$"
expect reorder "DRAG target kind=place place=1$"
expect reorder "DRAG drop operation=reorder place=4 to=1 error=0$"
expect reorder "LOCATION kind=folder path=$home/Documents "
refuse reorder "LOCATION kind=folder path=$home/Pictures "
holds reorder "head -1 '$sidebar' | grep -qx '$home/Pictures'"
holds reorder "sed -n 2p '$sidebar' | grep -qx '$home/Desktop'"

# The pictures as PNG next to the text.
ls "$out"/*.png >/dev/null 2>&1 || status=1
[ $status = 0 ] && echo "host-p010: PASS" || echo "host-p010: FAIL"
exit $status
