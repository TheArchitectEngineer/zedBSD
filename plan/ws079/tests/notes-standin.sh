#!/bin/sh
# ws079-p010: a stand-in for Notes on the test guest, installed as /bin/notes
# on the guest's copy of the disk only while Notes itself is not built.
#
# It is a wltest window whose app_id is "notes", in Notes' paper colour, and
# fullscreen from the start unless --windowed is given (the compositor starts
# it as "/bin/notes --fullscreen").  /tmp/notes-args, when present, adds
# wltest options.  It reports to /tmp/notes.log and adds its process ID to
# /tmp/notes.pids (the guest's ps shows no arguments to tell it by).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
window=
for argument in "$@"; do
	[ "$argument" = "--windowed" ] && window="--windowed --size=640x400"
done
extra=
[ -f /tmp/notes-args ] && extra=$(cat /tmp/notes-args)
echo $$ >> /tmp/notes.pids
exec /bin/wltest --app-id=notes --token=notes --frames=3600 --delay-ms=200 --color=fdf6e3 $window $extra >> /tmp/notes.log 2>&1 </dev/null
