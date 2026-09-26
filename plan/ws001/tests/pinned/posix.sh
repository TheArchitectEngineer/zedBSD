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
