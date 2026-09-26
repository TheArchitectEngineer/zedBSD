#### sleep integer and fraction
sleep 0; echo "st=$?"; sleep 0.2; echo "st=$?"; sleep 1 & wait; echo "st=$?"

#### sleep takes about the time
s=$(awk 'BEGIN { srand(); print srand() }'); sleep 2; e=$(awk 'BEGIN { srand(); print srand() }'); echo $((e - s >= 1 && e - s <= 4))

#### sleep invalid operands
sleep 2>/dev/null; echo "st=$?"; sleep x 2>/dev/null; echo "st=$?"; sleep -1 2>/dev/null; echo "st=$?"

#### sleep SIGALRM ends it normally
sleep 5 & p=$!; sleep 0.3; kill -ALRM $p; wait $p; echo "st=$?"

#### uname default and options
test "$(uname)" = "$(uname -s)" && echo same; uname -a | awk '{ print (NF >= 5) }'; test "$(uname -snrvm)" = "$(uname -a | cut -d' ' -f1-$(uname -snrvm | wc -w))" && echo order

#### uname single fields
for o in s n r m; do test -n "$(uname -$o)" && echo "$o ok"; done; test "$(uname -n)" = "$(hostname 2>/dev/null || uname -n)" && echo node

#### uname invalid
uname -z 2>/dev/null; echo "st=$?"; uname x 2>/dev/null; echo "st=$?"

#### kill a process by number and name
sleep 30 & p=$!; env kill $p; wait $p; echo "st=$?"; sleep 30 & p=$!; env kill -s KILL $p; wait $p; echo "st=$?"; sleep 30 & p=$!; env kill -9 $p; wait $p; echo "st=$?"

#### kill signal names without SIG and in any case
sleep 30 & p=$!; env kill -s term $p; wait $p; echo "st=$?"; sleep 30 & p=$!; env kill -HUP $p; wait $p; echo "st=$?"; sleep 30 & p=$!; env kill -s SIGINT $p 2>/dev/null; r=$?; kill $p 2>/dev/null; wait $p 2>/dev/null; echo "sig-prefix=$r"

#### kill 0 signal checks existence
sleep 30 & p=$!; env kill -0 $p; echo "st=$?"; env kill -s 0 $p; echo "st=$?"; kill $p; wait $p 2>/dev/null; env kill -0 $p 2>/dev/null; echo "gone=$?"

#### kill -l with an exit status
env kill -l 15; env kill -l 1 2>/dev/null | head -1

#### kill -l lists names
env kill -l | tr ' \t' '\n\n' | grep -c -x -e HUP -e INT -e KILL -e TERM -e STOP -e CONT -e USR1

#### kill errors
env kill 2>/dev/null; echo "st=$?"; env kill -s NOSUCH 1 2>/dev/null; echo "st=$?"; env kill 999999 2>/dev/null; echo "st=$?"; env kill abc 2>/dev/null; echo "st=$?"

#### kill a missing process group
env kill -0 -- -999999 2>/dev/null; echo "st=$?"; env kill -s TERM -- -999999 2>/dev/null; echo "st=$?"

#### pathchk ordinary paths
pathchk a/b/c; echo "st=$?"; pathchk "$(awk 'BEGIN { for (i = 0; i < 300; i++) printf "a" }')" 2>/dev/null; echo "long=$?"

#### pathchk -p portability
pathchk -p abcdefghijklmn; echo "st=$?"; pathchk -p abcdefghijklmno 2>/dev/null; echo "long=$?"; pathchk -p 'a b' 2>/dev/null; echo "char=$?"; pathchk -p '' 2>/dev/null; echo "empty=$?"

#### pathchk -P
pathchk -P a/-b 2>/dev/null; echo "dash=$?"; pathchk -P '' 2>/dev/null; echo "empty=$?"; pathchk -P a/b; echo "st=$?"

#### pathchk unsearchable directory
test "$(id -u)" -eq 0 && echo "st=1" || { mkdir d; chmod 0 d; pathchk d/x 2>/dev/null; echo "st=$?"; chmod 755 d; }

#### pathchk path through a file
touch f; pathchk f/x 2>/dev/null; echo "st=$?"

#### pathchk several operands
pathchk -p ok 'no good' ok2 2>/dev/null; echo "st=$?"

#### strings default
printf 'ab\0abcd\0abcdef\nxyz\001hello world\n\377\376' > f; strings f

#### strings -n
printf 'ab\0abcd\0abcdef\nxyz\001hello world\n' > f; strings -n 6 f; strings -n 2 f

#### strings -t
printf 'xx\0abcd\0\001\002efghij\n' > f; strings -t d f; strings -t o f; strings -t x f

#### strings standard input and -a
printf 'zz\0longer text\0' | strings -a; printf 'zz\0longer text\0' | strings

#### strings invalid
printf 'abcd' | strings -n 0 2>/dev/null; echo "st=$?"; printf 'abcd' | strings -t q 2>/dev/null; echo "st=$?"; strings nosuch 2>/dev/null; echo "st=$?"

#### link and unlink
touch a; link a b; echo "st=$?"; ls -i a b | awk '{ print $1 }' | uniq | wc -l; unlink a; echo "st=$?"; ls

#### link errors
touch a b; link a b 2>/dev/null; echo "st=$?"; link nosuch c 2>/dev/null; echo "st=$?"; link a 2>/dev/null; echo "st=$?"; link a b c 2>/dev/null; echo "st=$?"; mkdir d; link d e 2>/dev/null; echo "dir=$?"

#### unlink errors
unlink nosuch 2>/dev/null; echo "st=$?"; unlink 2>/dev/null; echo "st=$?"; touch a b; unlink a b 2>/dev/null; echo "st=$?"; ls; mkdir d; unlink d 2>/dev/null; echo "dir=$?"; ls

#### unlink a symbolic link
touch t; ln -s t l; unlink l; ls

#### tty without a terminal
tty < /dev/null; echo "st=$?"; tty -s < /dev/null; echo "st=$?"

#### tty invalid
tty x < /dev/null 2>/dev/null; echo "st=$?"; tty -z < /dev/null 2>/dev/null; echo "st=$?"

#### logname operand
logname x 2>/dev/null; echo "st=$?"
