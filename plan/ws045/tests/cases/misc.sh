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
ls -l --time-style=+%Y%m%d%H%M b 2>/dev/null | cut -d' ' -f6 || true
[ a -nt b ] || [ b -nt a ] || echo same

#### touch -d
touch -d '2020-01-02 03:04:05' a
ls -l --time-style=+%Y%m%d%H%M%S a 2>/dev/null | cut -d' ' -f6

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

#### xargs -0
printf 'a b\0c\0' | xargs -0 printf '[%s]\n'

#### xargs -r with no input
printf '' | xargs -r echo run; echo done

#### xargs --null and --no-run-if-empty
printf 'x\0' | xargs --null --no-run-if-empty echo

#### xargs -I
printf 'a\nb\n' | xargs -I{} echo 'x{}y'

#### xargs -d
printf 'a:b:c' | xargs -d: echo

#### xargs -P1 accepted
printf 'a\n' | xargs -P1 echo

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

#### mktemp -d
d=$(mktemp -d ./confXXXXXX) && test -d "$d" && echo ok

#### mktemp file
f=$(mktemp ./tmp.XXXXXX) && test -f "$f" && echo ok

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

#### dirname several
dirname /a/b /c/d

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
