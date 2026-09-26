# ws045: GNU extensions of the other base utilities, compared with GNU
# coreutils and findutils without POSIXLY_CORRECT.

#### sort -V (emacs configure: the newest version)
printf '1.10\n1.9\n1.2.3\n1.2\n' | sort -V

#### sort -V with names
printf 'lib-2.10.so\nlib-2.9.so\nlib-2.1.so\n' | sort -V | tail -n 1

#### sort -h
printf '1K\n2M\n512\n3G\n' | sort -h

#### sort --reverse --numeric-sort
printf '3\n10\n2\n' | sort --reverse --numeric-sort

#### head -n -2 (all but the last two)
seq 5 | head -n -2

#### head -c -2
printf 'abcdef' | head -c -2; echo

#### head --lines
seq 5 | head --lines=2

#### tail --bytes
printf 'abcdef' | tail --bytes=3; echo

#### tail --lines
seq 5 | tail --lines=2

#### tail -n +3
seq 5 | tail -n +3

#### cmp --ignore-initial (binutils configure)
printf 'xabc' > t1
printf 'yyabc' > t2
cmp --ignore-initial=1:2 t1 t2; echo $?

#### cmp --ignore-initial=N
printf 'xxabc' > t1
printf 'yyabc' > t2
cmp --ignore-initial=2 t1 t2; echo $?

#### cmp -i
printf 'xxabc' > t1
printf 'yyabc' > t2
cmp -i 2 t1 t2; echo $?

#### cmp --silent
printf 'a' > t1
printf 'b' > t2
cmp --silent t1 t2; echo $?

#### touch --reference (binutils opcodes configure)
touch -t 202001020304 a
touch b
touch --reference=a b
stat -c %Y b

#### touch -d
touch -d '2020-01-02 03:04:05' a
stat -c %Y a

#### touch -d with a date only and @seconds
touch -d 2021-03-04 a
touch -d @1700000000 b
stat -c %Y a b

#### touch -d relative to now is later than an old file
touch -d '2000-01-01' old
touch -d '1 day ago' new
[ new -nt old ] && echo newer

#### touch --no-create and --time=mtime
touch --no-create missing; ls missing 2>/dev/null; echo $?
touch -d @1000 f
touch --time=mtime -d @2000 f
stat -c '%X %Y' f

#### date -d @epoch (vim configure)
date -u -d @1700000000 '+%Y-%m-%d %H:%M:%S'

#### date -u -d with a date
date -u -d '2024-02-29 12:00:00' '+%s'

#### date --date
date -u --date='2020-01-01' '+%Y %j'

#### date -r file
touch -t 202001020304.05 f
date -u -r f '+%Y%m%d%H%M%S'

#### date -r seconds is not a file (BSD) falls back
date -u -r 0 '+%Y' 2>/dev/null || echo fail

#### date -d YYYYMMDD (ncurses make-tar.sh)
date -u +'%a %b %d %Y' -d 20260101

#### date -d relative items
date -u -d '2024-01-31 00:00:00 +1 day' +%F
date -u -d '2024-03-01 2 days ago' +%F
date -u -d '2024-01-01T10:00:00Z' +%s
date -u -d '2024-01-01 10:00 +0200' +%H:%M

#### date -I and -R
date -u -d @0 -I
date -u -d @0 -Iseconds
date -u -d @0 -R

#### date formats through strftime
date -u -d @1700000000 '+%a %A %b %B %e %j %y %H%%'

#### find -maxdepth
mkdir -p a/b/c
touch a/x a/b/y a/b/c/z
find a -maxdepth 1 | sort

#### find -mindepth
mkdir -p a/b/c
touch a/x a/b/y a/b/c/z
find a -mindepth 2 | sort

#### find -iname
touch A.YML b.yml c.txt
find . -iname '*.yml' | sort

#### find -print0
mkdir d
touch 'd/a b' d/c
find d -type f -print0 | sort -z | od -c

#### find -delete
mkdir -p d/e
touch d/e/f d/g
find d -name f -delete
find d | sort

#### find -empty
mkdir -p d/e
touch d/f
find d -empty | sort

#### find -readable and -executable
touch f; chmod 755 f; touch g
find . -type f -executable | sort

#### find -regex
touch a.c b.h c.o
find . -regex '.*\.[ch]' | sort

#### find -path with -prune and -o
mkdir -p a/.git b
touch a/.git/x a/y b/z
find . -path '*/.git' -prune -o -type f -print | sort

#### find -newer
touch -t 202001010000 old
touch new
find . -newer old | sort

#### find -printf
mkdir d; touch d/f
find d -type f -printf '%f %p\n'

#### readlink -f
mkdir -p d/e
ln -s d/e l
readlink -f l | sed "s|$PWD|.|"

#### readlink -f of a missing last part
readlink -f nothere | sed "s|$PWD|.|"

#### readlink -e missing
readlink -e nothere; echo $?

#### realpath
mkdir d
realpath d/../d | sed "s|$PWD|.|"

#### stat -c
printf 'abc' > f
stat -c '%s %n' f

#### stat --format
printf 'abcd' > f
stat --format='%s' f

#### cp -a keeps the mode and times
touch -t 202001020304 f
chmod 640 f
cp -a f g
ls -l g | cut -c1-10; [ f -nt g ] || [ g -nt f ] || echo same

#### cp -r
mkdir -p d/e; touch d/e/f
cp -r d x
find x | sort

#### cp --preserve and -v
touch f
cp -v f g

#### cp -T and -t
mkdir d; touch f
cp -t d f
ls d

#### mv -n does not overwrite
echo a > a; echo b > b
mv -n a b; cat b

#### mv -v
touch a
mv -v a b

#### rm -v
touch a
rm -v a

#### rm --force --recursive
mkdir -p d/e; touch d/e/f
rm --force --recursive d; ls

#### mkdir --parents and -v
mkdir -pv a/b

#### ln -sfn
mkdir d e
ln -s d l
ln -sfn e l
readlink l

#### ln -r relative
mkdir -p a/b
touch a/f
ln -sr a/f a/b/l
readlink a/b/l

#### wc -L
printf 'ab\nabcd\n' | wc -L

#### cut --complement
echo 'a:b:c' | cut -d: -f2 --complement

#### cut --output-delimiter
echo 'a:b:c' | cut -d: -f1,3 --output-delimiter=,

#### uniq -w and -i
printf 'aX\nAy\nb\n' | uniq -i -w1

#### tr --delete
echo hello | tr --delete l

#### od -An -tx1
printf 'ab' | od -An -tx1

#### echo -e with /bin/echo semantics
env echo -e 'a\tb'

#### echo -n
env echo -n x; env echo y

#### seq -w and -s
seq -w 8 10; seq -s, 3

#### basename -s
basename -s .c /x/y.c

#### basename -a
basename -a /x/a /y/b

#### env -u
X=1 env -u X sh -c 'echo "${X-unset}"'

#### tac
printf 'a\nb\n' | tac

#### tee -a
echo a > f; echo b | tee -a f >/dev/null; cat f

#### timeout
timeout 5 true; echo $?

#### truncate -s
printf 'abcdef' > f; truncate -s 3 f; cat f; echo

#### split -d and -a
seq 4 | split -l 2 -d -a 1 - p
ls p*

#### nl -ba
printf 'a\n\nb\n' | nl -ba

#### expr length and match
expr length abc; expr match abc 'a\(.\)'; expr substr abcde 2 3; expr index abc c

#### expr + token
expr + length

#### find -maxdepth 0
mkdir -p a/b
find a -maxdepth 0

#### find -mindepth 1 -maxdepth 2 -type f
mkdir -p a/b/c
touch a/x a/b/y a/b/c/z
find a -mindepth 1 -maxdepth 2 -type f | sort

#### find -ipath and -wholename
mkdir -p A/B
touch A/B/f
find . -ipath './a/b/*' | sort
find . -wholename './A/B/f'

#### find -regex with -regextype posix-extended
touch a1.c b22.c c.h
find . -regextype posix-extended -regex '\./[a-z][0-9]+\.c' | sort

#### find -iregex
touch A.C b.c
find . -iregex '.*\.c' | sort

#### find -empty on files and directories
mkdir -p e f
touch f/g empty
printf x > full
find . -empty | sort

#### find -delete with -depth order
mkdir -p d/e/f
touch d/e/f/g
find d -delete
ls d 2>/dev/null; echo $?

#### find -printf directives
mkdir -p d/e
printf abc > d/e/f
chmod 640 d/e/f
find d -type f -printf '%p|%f|%h|%P|%s|%m|%M|%d|%y\n'

#### find -printf escapes and %%
touch f
find f -printf '%f\t100%%\n'

#### find -quit
mkdir d
touch d/a d/b
find d -type f -print -quit | wc -l

#### find -false and -or
touch a b
find . -name a -false -or -name b | sort

#### find -and and -not
touch a b c
find . -type f -and -not -name a | sort

#### find -mmin
touch -d '10 minutes ago' old
touch new
find . -type f -mmin -5 | sort
find . -type f -mmin +5 | sort

#### find -executable -readable -writable
touch f; chmod 755 f; touch g; chmod 444 g
find . -type f -executable | sort
find . -type f -readable | sort

#### find with -print0 and xargs-free sort
mkdir d
touch 'd/a b'
find d -type f -print0 | od -c

#### readlink -e an existing chain
mkdir -p d
touch d/f
ln -s d/f l1
ln -s l1 l2
readlink -e l2 | sed "s|$PWD|.|"

#### readlink -f a missing file in an existing directory
mkdir d
readlink -f d/missing | sed "s|$PWD|.|"

#### readlink -f a missing directory fails
readlink -f nodir/missing; echo $?

#### readlink -m anything
readlink -m nodir/../x/./y | sed "s|$PWD|.|"

#### readlink -n and several operands
mkdir d
readlink -f -n d | sed "s|$PWD|.|"; echo "|"
readlink -f d d | sed "s|$PWD|.|"

#### readlink -z
mkdir d
readlink -fz d | tr '\0' '|' | sed "s|$PWD|.|"

#### readlink of a relative link
ln -s target l
readlink l

#### readlink -f through .. after a link
mkdir -p a/b c
ln -s ../c a/b/lc
readlink -f a/b/lc/.. | sed "s|$PWD|.|"

#### stat -c directives
printf 'abcd' > f
chmod 640 f
stat -c '%n %s %a %A %F %h %f' f

#### stat -c on a directory and a link
mkdir d
ln -s d l
stat -c '%n %F %N' d l

#### stat --printf with escapes
printf 'ab' > f
stat --printf='%s\t%n\n' f

#### stat -L follows
printf 'abc' > f
ln -s f l
stat -L -c %s l

#### stat of an empty file type
: > e
stat -c %F e

#### stat -c %Y and %X
touch -d @1234567890 f
stat -c '%X %Y' f

#### head -n -0 and head -c -0
seq 3 | head -n -0
printf abc | head -c -0; echo

#### head with a unit
awk 'BEGIN { for (i = 0; i < 2000; i++) print "x" }' | head -c 1K | wc -c
awk 'BEGIN { for (i = 0; i < 2000; i++) print "y" }' | head -n 1b | wc -l

#### head -q and -v
printf 'a\n' > f1
printf 'b\n' > f2
head -q -n1 f1 f2
head -v -n1 f1

#### head -z
printf 'a\0b\0c\0' | head -z -n 2 | od -c

#### head -n -N on a file without a final newline
printf 'a\nb\nc' | head -n -1

#### head options after the file
printf 'a\nb\n' > f
head f -n 1

#### tail -n with a unit and -q -v
seq 5 > f
tail -q -n 1 f f
tail -v -n 1 f

#### tail -z
printf 'a\0b\0c\0' | tail -z -n 1 | od -c

#### tail -c +N and --bytes
printf 'abcdef' | tail -c +3; echo
printf 'abcdef' | tail --bytes=+3; echo

#### tail -n +N with -z
printf 'a\0b\0c\0' | tail -z -n +2 | od -c

#### cmp -n
printf 'abcX' > t1
printf 'abcY' > t2
cmp -n 3 t1 t2; echo $?
cmp --bytes=4 t1 t2 >/dev/null; echo $?

#### cmp -i with a difference after the skip
printf 'xxabc' > t1
printf 'yyabd' > t2
cmp -s -i 2 t1 t2; echo $?

#### cmp --verbose
printf 'ab' > t1
printf 'ac' > t2
cmp --verbose t1 t2

#### expr keywords with operators
expr length abcd + 1
expr substr hello 2 10
expr substr hello 0 2; echo $?
expr index hello lo
expr match abc 'ab'
expr + match

#### expr keyword in parentheses
expr \( length abc \) \* 2

#### echo -E and -n combined
env echo -nE 'a\tb'; echo
env echo -en 'a\tb'; echo

#### echo -- is an operand
env echo -- -n

#### sort -V and head together (emacs configure)
printf 'gcc-9\ngcc-12\ngcc-10\n' | sort -V | tail -n 1

#### rm -d an empty directory, and a full one
mkdir e f
touch f/x
rm -d e; echo $?
rm -d f 2>/dev/null; echo $?
ls

#### rm -rv a tree
mkdir -p d/e
touch d/e/f
rm -rv d | sort

#### rm --interactive=never
touch a
rm --interactive=never a; ls

#### mv -t and -v
mkdir d
touch a b
mv -v -t d a b
ls d

#### mv -T onto a file name
mkdir d
touch f
mv -T f g; ls

#### mv -u keeps a newer file
echo old > a
touch -d '2000-01-01' a
echo new > b
mv -u a b; cat b

#### mv --update=none
echo a > a; echo b > b
mv --update=none a b; cat b; ls

#### mv options after the operands
touch a
mv a b -v

#### mkdir -m with -p gives the mode to the last only
mkdir -p -m 700 x/y
ls -ld x/y | cut -c1-10

#### mkdir -m exactly
umask 077
mkdir -m 755 d
ls -ld d | cut -c1-10

#### mkdir --parents on an existing tree is quiet
mkdir -p a/b
mkdir --parents --verbose a/b/c

#### ln -t, -T and -v
mkdir d
touch f
ln -sv -t d ../f
readlink d/f
ln -sT f g; readlink g

#### ln with one operand
mkdir d
touch d/f
ln -s d/f
readlink f

#### ln -sr into a deeper directory
mkdir -p a/b/c x
touch x/f
ln -sr x/f a/b/c/l
readlink a/b/c/l

#### ln -sfn replaces a link to a directory
mkdir d e
ln -s d l
ln -sfn e l
readlink l

#### basename -z and --suffix
basename -z -s .h /a/b.h | od -c
basename --suffix=.c x.c y.c

#### cut --complement on bytes and -z
echo 'abcdef' | cut -b 2-3 --complement
printf 'a:b\0c:d\0' | cut -z -d: -f2 | od -c

#### cut --output-delimiter on byte ranges
echo 'abcdef' | cut -b 1-2,4-5 --output-delimiter=:

#### wc -L with a tab and several files
printf 'a\tb\n' > f1
printf 'abcdefghijk\n' > f2
wc -L f1 f2

#### wc -lL
printf 'ab\nabc\n' | wc -lL

#### uniq -c -w
printf 'ab1\nab2\ncd\n' | uniq -c -w 2

#### uniq -z
printf 'a\0a\0b\0' | uniq -z | od -c

#### tr -t
echo abcd | tr -t abcd xy

#### tr --squeeze-repeats
echo 'aaabbb' | tr --squeeze-repeats ab

#### env -S splits the string
env -S 'sh -c "echo one two"'

#### env -S with more options inside
X=1 env -S '-u X env' | grep -c '^X='; echo $?

#### env -0
env -i -0 A=1 B=2 | od -c

#### env -C
mkdir d
env -C d sh -c 'basename "$PWD"'

#### env stops at the utility
env sh -c 'echo "$1"' -i x

#### split --additional-suffix and -a 3
seq 3 | split -l 1 -a 3 --additional-suffix=.txt
ls x*

#### split --numeric-suffixes=5
seq 4 | split -l 2 --numeric-suffixes=5 - p
ls p*

#### split -b with a unit
awk 'BEGIN { for (i = 0; i < 1500; i++) print "y" }' | split -b 1K
ls x* | wc -l

#### split --verbose
seq 2 | split -l 1 --verbose

#### tee --append
echo a > f
echo b | tee --append f >/dev/null
cat f

#### cp -t and -v
mkdir d
touch a b
cp -v -t d a b
ls d

#### cp -rv
mkdir -p s/e
touch s/e/f
cp -rv s t | sort

#### cp --preserve=mode
touch f
chmod 604 f
cp --preserve=mode f g
ls -l g | cut -c1-10

#### cp -T
mkdir d
touch f
cp -T f d/g; ls d
