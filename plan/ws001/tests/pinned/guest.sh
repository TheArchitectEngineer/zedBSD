#### df -P heading and fields
# Guest only: df reads the zedBSD mount table (/dev/system).
df -P | sed 1q; df -P / | awk 'NR == 2 { print NF, $6 }'; df -P -k / | sed 1q | awk '{ print $2 }'
## expect
Filesystem 512-blocks Used Available Capacity Mounted on
6 /
1024-blocks

#### df figures add up and the capacity is rounded up
df -P / | awk 'NR == 2 { t = $2; u = $3; a = $4; p = $5; sub("%", "", p); c = 0; if (u + a > 0) c = int((u * 100 + u + a - 1) / (u + a)); print (u <= t), (a <= t), (p == c) }'
## expect
1 1 1

#### df -k halves the 512-byte total
a=$(df -P / | awk 'NR == 2 { print $2 }'); b=$(df -P -k / | awk 'NR == 2 { print $2 }'); test $((a / 2)) -eq "$b" && echo half
## expect
half

#### df names the file system of a file operand
: > f; df -P f | awk 'NR == 2 { print $6 }'; df f . / | awk 'NR > 1 { print $6 }' | sort -u
## expect
/
/

#### df without operands lists every file system once
df -P | awk 'NR > 1 && $6 == "/" { n++ } END { print n }'; df -P | awk 'NR > 1 { print $6 }' | sort | uniq -d | wc -l
## expect
1
0

#### df on the device of the root file system
n=$(df -P / | awk 'NR == 2 { print $1 }'); case $n in /dev/*) df -P "$n" | awk 'NR == 2 { print $6 }';; *) echo /;; esac
## expect
/

#### df missing operand
df -P nothere > out 2> err; echo "st=$?"; wc -l < out; test -s err && echo diagnosed
## expect
st=1
1
diagnosed

#### df -P and -t together
df -P -t > /dev/null 2>&1; echo "st=$?"
## expect
st=1

#### du counts what UFS allocates in 512-byte units
# Guest only: UFS allocates whole 8 KiB blocks, 16 units; an empty file
# has none.  Directories come after their entries.
mkdir -p d/a; printf 'x%.0s' $(seq 1 3000) > d/a/f; du -a d/a/f; du -k d/a/f; : > d/e; du -a d/e; du -a d | cut -f2
## expect
16	d/a/f
8	d/a/f
0	d/e
d/a/f
d/a
d/e
d

#### du totals include the directories
mkdir -p d/a; printf 'x%.0s' $(seq 1 3000) > d/a/f; printf 'x%.0s' $(seq 1 1000) > d/g; du -a d | awk -F '\t' '{ s[$2] = $1 } END { print s["d/a/f"], s["d/g"], (s["d/a"] > s["d/a/f"]), (s["d"] > s["d/a"] + s["d/g"]) }'
## expect
16 16 1 1

#### du counts hard links and repeated operands once
mkdir d; printf 'x%.0s' $(seq 1 3000) > d/f; ln d/f d/g; du -a d | grep -c 'd/[fg]'; du d d | wc -l; du -s d/f d/g | wc -l
## expect
1
1
1

#### du -s, -H and -L
mkdir -p r/s; printf 'x%.0s' $(seq 1 3000) > r/s/f; ln -s r top; du -s top | cut -f2; du -H -s top | awk '{ print ($1 >= 6) }'; du -s top | awk '{ print ($1 < 6) }'; mkdir d; ln -s ../r d/l; du -L -a d | cut -f2
## expect
top
1
1
d/l/s/f
d/l/s
d/l
d

#### du -x stays on the file system of the operand
# /dev/shm is a tmpfs mounted on devfs.
mkdir -p d/e; : > d/e/f; du -x -a d | cut -f2; : > /dev/shm/ws001-du; du -x -a /dev 2> /dev/null | grep -c ws001-du; du -a /dev 2> /dev/null | grep -c ws001-du; rm -f /dev/shm/ws001-du
## expect
d/e/f
d/e
d
0
1

