#### who reads a file of records
# Host only: the records are glibc's struct utmpx (384 bytes), which both
# who read from the file operand.  Lines that do not exist show state ?.
python3 - <<'EOF'
import struct
def r(kind, pid, line, ident, user, host, seconds, term=0, code=0):
	return struct.pack("<hxxi32s4s32s256shhiii4i20x", kind, pid, line.encode(), ident.encode(), user.encode(), host.encode(), term, code, 0, seconds, 0, 0, 0, 0, 0)
b = 1790000000
with open("u", "wb") as f:
	f.write(r(2, 0, "~", "~~", "reboot", "6.1", b))
	f.write(r(5, 101, "", "si", "", "", b + 10))
	f.write(r(6, 200, "tty90", "90", "LOGIN", "", b + 20))
	f.write(r(7, 300, "tty91", "91", "alice", "", b + 30))
	f.write(r(7, 301, "pts/90", "s/90", "bob", "host.example", b + 86400))
	f.write(r(7, 302, "pts/91", "s/91", "", "", b + 50))
	f.write(r(8, 303, "pts/92", "s/92", "", "", b + 100))
	f.write(r(3, 0, "|", "", "", "", b + 200))
	f.write(r(4, 0, "}", "", "", "", b + 190))
	f.write(b"partial")
EOF
for o in "" -H -s -T -b -l -p -t -u -q "-q -H" "-T -H" "-s -u" "-b -t" "-u -T -H"; do echo "== $o"; who $o u; echo "st=$?"; done

#### who -m and am i without a terminal
python3 - <<'EOF'
import struct
with open("u", "wb") as f:
	f.write(struct.pack("<hxxi32s4s32s256shhiii4i20x", 7, 300, b"pts/90", b"s/90", b"alice", b"", 0, 0, 0, 1790000000, 0, 0, 0, 0, 0))
EOF
who -m u; echo "st=$?"; who -m -H u; echo "st=$?"

#### who terminal state and idle time from the line
# The line is an absolute path of 32 bytes at most, so it is made in /tmp.
t=$(mktemp -d /tmp/who.XXXXXX); export t
python3 - <<'EOF'
import os, struct, time
here = os.environ["t"]
now = int(time.time())
def r(pid, line, user):
	return struct.pack("<hxxi32s4s32s256shhiii4i20x", 7, pid, line.encode(), b"", user.encode(), b"", 0, 0, 0, now - 7200, 0, 0, 0, 0, 0)
open(here + "/t1", "w").close(); os.chmod(here + "/t1", 0o620)
open(here + "/t2", "w").close(); os.chmod(here + "/t2", 0o600)
os.utime(here + "/t2", (now - 3 * 3600 - 5 * 60 - 10, now - 3 * 3600 - 5 * 60 - 10))
with open("u", "wb") as f:
	f.write(r(10, here + "/t1", "a"))
	f.write(r(11, here + "/t2", "b"))
EOF
who -T -u u | sed "s|$t/||; s/[A-Z][a-z][a-z] [ 0-9][0-9] [0-9][0-9]:[0-9][0-9]/TIME/"; rm -r "$t"

#### who empty and missing files
: > e; who e; echo "st=$?"; who -q e; head -c 1000 /dev/zero > z; who -a z; echo "st=$?"

#### who usage
who -x > /dev/null 2>&1; echo "st=$?"; who a b c > /dev/null 2>&1; echo "st=$?"
