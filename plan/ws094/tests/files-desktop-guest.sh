#!/bin/sh
# ws094-p003: Files' desktop mode (files --desktop) on zdesktop's desktop surface, on the Venus guest.  The running
# guest gets this worktree's compositor, files and libraries (BIN); HOME is /tmp/dhome with a Desktop folder of a
# folder, a text, a picture, a PDF and a script.  zdesktop --glass at 1280x800 starts /bin/files --desktop itself
# (--desktop-client), with the token in its environment.
#   show      the desktop: the role taken, configured 1280x766, the five items laid out from the top-right corner down
#             (ZFILES DESKTOP place/ready), a picture (desktop.png)
#   watch     a file added to ~/Desktop appears within a few seconds (items=6, added.png), and goes when it is removed
#   input     click, arrow, Enter (the started program has no token), a double click on a folder (a new window), a
#             rubber band (selected.png, folder.png, band.png)
#   window    a Files window opens over the icons (window.png)
#   saved     (ws094-p004) the layout file placed before the desktop starts puts notes.txt at column 2 row 3 (its saved
#             place), the other items in the free cells (saved.png)
# The steps read zdesktop's log (Files, started by zdesktop, writes there too) through SSH, and the pictures; nothing
# reads the console.
#   GUEST_RUNTIME=$PWD/build/ws094-run BIN=build/ws094-amd64 plan/ws094/tests/files-desktop-guest.sh OUTDIR STEP...
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws094-run}"
bin=${BIN:-build/ws094-amd64}
out=$1
shift
mkdir -p "$out"
status=0
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 120 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null || { echo "put $1: FAILED"; status=1; }; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; sleep 0.7; }
shot() {
	python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null
	echo "shot $1"
}
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[f]iles" | awk "{print \$1}"); do kill $p; done; sleep 1'

# Fails the run unless a log has a line matching a pattern (within a few seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt 10 ]; do
		found=$(guest "grep -acE '$2' $1" | tail -1)
		[ "${found:-0}" -gt 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then
		echo "log: $2 ok"
	else
		echo "log: $2 MISSING"
		status=1
	fi
}

for step in "$@"; do
	case "$step" in
	install)
		python3 plan/tools/imageview/make-images.py build/ws094-images >/dev/null
		put "$bin/bin/wayland" /bin/wayland
		put "$bin/bin/files" /bin/files
		for library in $(cd "$bin/dynamic" && ls *.so | grep -vE '^(libc|ld)\.so$'); do
			put "$bin/dynamic/$library" "/lib/$library"
		done
		guest 'chmod 755 /bin/wayland /bin/files; rm -rf /tmp/dhome; mkdir -p /tmp/dhome/Desktop/Projects; printf "Meeting notes\n" > /tmp/dhome/Desktop/notes.txt; printf "#!/bin/sh\necho hi\n" > /tmp/dhome/Desktop/script.sh; chmod 755 /tmp/dhome/Desktop/script.sh' >/dev/null
		put build/ws094-images/01-splash.png /tmp/dhome/Desktop/photo.png
		python3 plan/ws081/tests/make-touch-pdf.py build/ws094-images/report.pdf >/dev/null
		put build/ws094-images/report.pdf /tmp/dhome/Desktop/report.pdf
		;;
	show)
		guest "$stop_all" >/dev/null
		guest "export XDG_RUNTIME_DIR=/tmp HOME=/tmp/dhome; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass \$picture --desktop-client='/bin/files --desktop' > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -aq 'ZFILES READY' /tmp/zdesktop.log && [ \$i -lt 60 ]; do sleep 0.5; i=\$((i+1)); done; sleep 2; echo started" >/dev/null
		expect_log /tmp/zdesktop.log 'ZWL DESKTOP start pid=[0-9]+ command=/bin/files --desktop'
		expect_log /tmp/zdesktop.log 'ZWL DESKTOP role client=[0-9]+ surface=[0-9]+ x=0 y=34 width=1280 height=766'
		expect_log /tmp/zdesktop.log 'ZFILES DESKTOP configure x=0 y=34 width=1280 height=766'
		expect_log /tmp/zdesktop.log 'ZFILES DESKTOP ready items=5 cells=5 width=1280 height=766'
		expect_log /tmp/zdesktop.log 'ZFILES DESKTOP place name=[^ ]+ column=0 row=0 x=1168 y=16'
		expect_log /tmp/zdesktop.log 'ZFILES DESKTOP place name=[^ ]+ column=0 row=4 x=1168 y=432'
		pointer move 640 600 sleep 300
		sleep 2
		shot desktop.png
		guest "grep -a 'ZFILES DESKTOP place' /tmp/zdesktop.log" > "$out/places.txt"
		;;
	watch)
		guest 'printf "added\n" > /tmp/dhome/Desktop/added.txt' >/dev/null
		expect_log /tmp/zdesktop.log 'ZFILES DESKTOP ready items=6 cells=6'
		sleep 1
		shot added.png
		guest 'rm -f /tmp/dhome/Desktop/added.txt' >/dev/null
		sleep 3
		found=$(guest "grep -ac 'ZFILES DESKTOP ready items=5' /tmp/zdesktop.log" | tail -1)
		[ "${found:-0}" -ge 2 ] 2>/dev/null && echo "removed: ok" || { echo "removed: MISSING"; status=1; }
		;;
	input)
		# Icons are placed from the top-right corner: 1 Projects (1216,90), 2 notes.txt (1216,194), 3 photo.png (1216,298).
		# ~/.config/keiland/open-with sends plain text to "Env" (env > ~/env.txt): the program a double click starts
		# must not have the desktop's token.
		guest 'mkdir -p /tmp/dhome/.config/keiland; printf "text/plain\tEnv\tenv > /tmp/dhome/env.txt # %%f\n" > /tmp/dhome/.config/keiland/open-with; rm -f /tmp/dhome/env.txt' >/dev/null
		pointer move 1216 298 sleep 300 down sleep 60 up sleep 700
		expect_log /tmp/zdesktop.log 'ZFILES DESKTOP select name=photo.png selected=1'
		shot selected.png
		keys '<up>'
		expect_log /tmp/zdesktop.log 'ZFILES DESKTOP select name=notes.txt selected=1 via=arrow'
		keys '<ret>'
		expect_log /tmp/zdesktop.log 'ZFILES DESKTOP open via=enter'
		expect_log /tmp/zdesktop.log 'ZFILES OPEN path=/tmp/dhome/Desktop/notes.txt app=Env error=0'
		sleep 2
		token=$(guest 'grep -c KEILAND_DESKTOP_TOKEN /tmp/dhome/env.txt; grep -c "^HOME=" /tmp/dhome/env.txt' | tail -2 | tr '\n' ' ')
		[ "$token" = "0 1 " ] && echo "token: not inherited ok" || { echo "token: ($token) MISSING"; status=1; }
		# A double click on the folder opens a new Files window on it.
		pointer move 1216 90 sleep 300 down sleep 50 up sleep 80 down sleep 50 up sleep 3000
		expect_log /tmp/zdesktop.log 'ZFILES DESKTOP open name=Projects via=double-click'
		expect_log /tmp/zdesktop.log 'ZFILES DESKTOP open-folder path=/tmp/dhome/Desktop/Projects error=0'
		expect_log /tmp/zdesktop.log 'ZWL MAP client=[0-9]+ '
		shot folder.png
		# The folder's window (on top, with the keyboard) closes by Ctrl+W.
		keys '<ctrl-w>'
		sleep 1
		# A rubber band from empty desktop over the first column selects its items.
		# The picture is taken after one more small move and a rest, so that the band's last frame is shown (the
		# first p004 picture was one frame behind the pointer).
		pointer move 1100 60 sleep 300 down sleep 100 move 1150 200 sleep 100 move 1260 330 sleep 400 move 1261 331 sleep 1200
		shot band.png
		pointer up sleep 500
		expect_log /tmp/zdesktop.log 'ZFILES DESKTOP band start x=1100 y=26'
		expect_log /tmp/zdesktop.log 'ZFILES DESKTOP band end first=0'
		keys '<esc>'
		;;
	window)
		guest "export XDG_RUNTIME_DIR=/tmp HOME=/tmp/dhome; /bin/files --token=w --timeout-s=800 --width=800 --height=560 /tmp/dhome/Desktop > /tmp/f.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
		expect_log /tmp/zdesktop.log 'ZWL MAP client=[0-9]+ '
		pointer move 640 760 sleep 300
		shot window.png
		;;
	saved)
		# notes.txt kept at column 2, row 3 before Files starts; the compositor started again with Files.
		guest "$stop_all" >/dev/null
		guest 'mkdir -p /tmp/dhome/.config/keiland; printf "notes.txt\t2\t3\n" > /tmp/dhome/.config/keiland/desktop-layout' >/dev/null
		guest "export XDG_RUNTIME_DIR=/tmp HOME=/tmp/dhome; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass \$picture --desktop-client='/bin/files --desktop' > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -aq 'ZFILES READY' /tmp/zdesktop.log && [ \$i -lt 60 ]; do sleep 0.5; i=\$((i+1)); done; sleep 2; echo started" >/dev/null
		expect_log /tmp/zdesktop.log 'ZFILES DESKTOP place name=notes.txt column=2 row=3 '
		expect_log /tmp/zdesktop.log 'ZFILES DESKTOP place name=Projects column=0 row=0 '
		pointer move 640 600 sleep 300
		sleep 2
		shot saved.png
		guest "grep -a 'ZFILES DESKTOP place' /tmp/zdesktop.log" > "$out/saved-places.txt"
		guest 'rm -f /tmp/dhome/.config/keiland/desktop-layout' >/dev/null
		;;
	stop)
		guest "$stop_all" >/dev/null
		;;
	*)
		echo "unknown step $step"
		status=1
		;;
	esac
done
errors=$(guest "grep -ac ERROR /tmp/zdesktop.log" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
failed=$(guest "grep -ac 'ZFILES FAILED' /tmp/zdesktop.log" | tail -1)
[ "${failed:-1}" = 0 ] && echo "files: no FAILED" || { echo "files: FAILED lines"; status=1; }
guest 'grep -a "DESKTOP" /tmp/zdesktop.log' > "$out/desktop-log.txt"
[ $status = 0 ] && echo "files-desktop-guest: PASS" || echo "files-desktop-guest: FAIL"
exit $status
