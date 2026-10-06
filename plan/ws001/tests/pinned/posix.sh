#### kill -l names the signal of an exit status above 128
# procps kill does not; POSIX XCU kill -l asks for it.
env kill -l 137; env kill -l 143; env kill -l 9
## expect
KILL
TERM
KILL

#### mkdir -m +t sets the mode exactly
# GNU leaves the umask applied when the sticky bit is asked for.
umask 022; mkdir -m +t d; ls -ld d | cut -c1-10
## expect
drwxrwxrwt

#### pr header is the POSIX format
# GNU centres the name; POSIX writes "%s %s Page %d" with single spaces.
printf 'one\n' > f; touch -t 200001020304.05 f; pr -l 12 f | sed -n 3p; pr -l 12 f | sed -n 1,5p | awk 'length($0) == 0 { n++ } END { print n }'
## expect
Jan  2 03:04 2000 f Page 1
4

#### pr -d keeps the page length
# GNU makes a page one line short when the text rows are odd.
printf '1\n2\n' > f; touch -t 200001020304.05 f; pr -d -l 13 f | wc -l
## expect
26

#### chgrp -R -H changes links met inside the tree themselves
# GNU changes what they point to, outside the tree.
g2=$(id -G | awk '{ print $2 }'); test -n "$g2" || g2=24; mkdir real; touch real/f out; ln -s ../out real/l; ln -s real top; chgrp -R -H "$g2" top; test "$(stat -c %g out)" = "$(id -g)" && echo out-kept; test "$(stat -c %g real/l)" = "$g2" && echo link-changed
## expect
out-kept
link-changed

#### cksum -a sha256 prints the digest, two spaces and the name
# A zedBSD extension the installer reads (userland/retro/zedinst/files.noct、2026-10-07 に削除).
printf abc > f; cksum -a sha256 -- f; printf 'x' > 'a\b'; cksum -a sha256 'a\b' | cut -c1-2
## expect
ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  f
\2

#### patch -N ignores an applied hunk
# POSIX: -N ignores differences already applied; GNU rejects them to w.rej
# and exits with 1.  Exit status 1 means lines were written to a reject file.
printf 'a\nb\nc\n' > x; printf 'a\nB\nc\n' > y; diff -u x y > p; cp y w; patch -N w p 2> /dev/null; echo "st=$?"; cat w; ls
## expect
st=0
a
B
c
p
w
x
y

#### patch writes a rejected normal hunk in the context format
# POSIX: rejected normal hunks are written as copied context differences;
# GNU writes "*** 2" and "--- 2 -----" and names /dev/null.
printf 'a\nb\nc\n' > x; printf 'a\nB\nc\n' > y; diff x y > p; printf 'a\nX\nc\n' > w; patch w p 2> /dev/null; echo "st=$?"; cat w.rej; printf 'a\nc\n' > y; diff x y > p; printf 'a\nX\nc\n' > w; patch -r r w p 2> /dev/null; cat r
## expect
st=1
*** w
--- w
***************
*** 2 ****
! b
--- 2 ----
! B
*** w
--- w
***************
*** 2 ****
- b
--- 1 ----

#### patch writes nothing on standard output
# POSIX: standard output is not used; GNU writes "patching file".
printf 'a\n' > x; printf 'b\n' > y; diff x y > p; patch x p 2> /dev/null | wc -c
## expect
0

#### du -L reports a directory loop
# POSIX: du detects infinite loops and writes a diagnostic; GNU skips the
# directory met again silently and exits with 0.
mkdir -p d/e; ln -s .. d/e/up; du -L d 2> err | cut -f2; grep -c loop err
## expect
d/e
d
1

#### who diagnoses a database file that cannot be read
# GNU who writes nothing and exits with 0.
who nothere 2> err; echo "st=$?"; test -s err && echo diagnosed
## expect
st=1
diagnosed
