#!/bin/sh
# on the RTX machine; marks line counts of the logs between scenes
export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0
echo 'while :; do echo hello zedbsd spam line 0123456789 abcdefghij; done' > /tmp/spam.sh
m() { echo "MARK $1 session=$(wc -l < /run/user/1000/session.log) kernel=$(wc -l < /var/log/kernel.log)"; }
m static-begin; sleep 20; m static-end
m files-begin; timeout 20 /bin/files >/tmp/files.out 2>&1; m files-end
sleep 8
m term-begin
timeout 26 /bin/terminal --command="sh /tmp/spam.sh" >/tmp/term.out 2>&1 &
sleep 3
timeout 20 /bin/files >/tmp/files2.out 2>&1
m term-end
sleep 8
m end
