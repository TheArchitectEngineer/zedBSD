# ws045: GNU extensions of grep, compared with GNU grep without
# POSIXLY_CORRECT.

#### BRE \| (config.guess)
printf 'CPU=x\nMIPS_ENDIAN=el\nLIBCABI=gnu\nOTHER=1\n' | grep '^CPU\|^MIPS_ENDIAN\|^LIBCABI'

#### BRE \+ and \?
printf 'ab\nabbb\nac\n' | grep 'ab\+$'
printf 'color\ncolour\n' | grep -c 'colou\?r'

#### \< \> (cairo check-preprocessor-syntax.sh)
printf '#include <x>\n# includes\n#define include_x\n' | grep '#.*\<include\>'

#### \w \s \b
printf 'foo bar\nfoo_bar\n' | grep 'foo\sbar'
printf 'a-b\nab\n' | grep '\bb'
printf 'x1\n--\n' | grep -c '\w'

#### -w word match (ncurses configure)
printf 'exec gcc\nexecute\nre-exec\n' | grep -w exec

#### -w with a match that is not a word first
printf 'foobar foo\n' | grep -w foo

#### -w with -F
printf 'foo bar\nfoobar\n' | grep -Fw foo

#### --word-regexp
printf 'is this\nisthis\n' | grep --word-regexp is

#### -o only the matches (gdb Makefile)
printf 'const char version[] = "13.2";\n' | grep -o '".*"'

#### -o with several matches on a line
printf 'a1b22c333\n' | grep -o '[0-9]\+'

#### -o -E (curl cmakeopts)
printf 'option(FOO_BAR "x")\n' | grep -o -E '[A-Z0-9_]+'

#### -o with -n and -b
printf 'xx ab ab\n' | grep -on ab

#### -o with -i
printf 'AbC abc\n' | grep -oi abc

#### egrep -o
printf 'x=1 y=22\n' | egrep -o '[0-9]+'

#### -A1 (oils: set | grep -A1)
printf 'a\nb\nc\nd\ne\n' | grep -A1 b

#### -B and -C with separators
printf '1\n2\n3\n4\n5\n6\n7\n8\n9\n' | grep -B1 -e 3 -e 8
echo ---
printf '1\n2\n3\n4\n5\n6\n7\n8\n9\n' | grep -C1 5

#### -A with overlapping contexts
printf '1\n2\n3\n4\n5\n' | grep -A2 -e 1 -e 2

#### --context=N and -NUM
printf '1\n2\n3\n4\n5\n' | grep --context=1 3
printf '1\n2\n3\n4\n5\n' | grep -1 4

#### -A with -n uses - for context lines
printf 'a\nb\nc\n' | grep -n -A1 a

#### context with several files
printf 'a\nb\n' > f1
printf 'c\na\n' > f2
grep -B1 a f1 f2

#### --group-separator
printf '1\n2\n3\n4\n5\n' | grep --group-separator=XX -A0 -e 1 -e 4

#### --no-group-separator
printf '1\n2\n3\n4\n5\n' | grep --no-group-separator -A0 -e 1 -e 4

#### -h and -H
printf 'a\n' > f1
printf 'a\n' > f2
grep -h a f1 f2
grep -H a f1

#### --with-filename and --no-filename
printf 'a\n' > f1
grep --with-filename a f1
grep --no-filename a f1 f1

#### -H with standard input
printf 'a\n' | grep -H a

#### --label
printf 'a\n' | grep -H --label=in a

#### -r recursive (libstdc++ po Makefile)
mkdir -p d/e
printf 'x __N("a")\n' > d/f
printf '__N("b")\n' > d/e/g
printf 'none\n' > d/h
grep -r -l '__N(".*")' d | sort

#### -r with no file operand searches .
mkdir -p d
printf 'hit\n' > d/f
grep -r hit | sort

#### -rl (qt start_analysis.sh)
mkdir -p d/e
printf 'MAGIC\n' > d/e/f
grep -rl MAGIC d

#### -R follows symbolic links
mkdir -p d real
printf 'hit\n' > real/f
ln -s ../real d/link
grep -R hit d | sort
echo ---
grep -r hit d | sort

#### --include and --exclude
mkdir -p d
printf 'hit\n' > d/a.c
printf 'hit\n' > d/b.h
printf 'hit\n' > d/c.o
grep -r --include='*.c' --include='*.h' hit d | sort
echo ---
grep -r --exclude='*.o' hit d | sort

#### --exclude-dir
mkdir -p d/.git d/src
printf 'hit\n' > d/.git/x
printf 'hit\n' > d/src/y
grep -r --exclude-dir=.git hit d

#### -L files without a match
printf 'a\n' > f1
printf 'b\n' > f2
grep -L a f1 f2; echo $?

#### --files-with-matches and --files-without-match
printf 'a\n' > f1
printf 'b\n' > f2
grep --files-with-matches a f1 f2
grep --files-without-match a f1 f2

#### -m max count
printf 'a\na\na\n' | grep -m2 a

#### --max-count with -c
printf 'a\na\na\n' | grep --max-count=1 -c a

#### -m stops reading
printf 'a\nb\na\n' | grep -m1 -A1 a

#### long options: --quiet --count --ignore-case --invert-match --line-number
printf 'A\nb\n' | grep --count --ignore-case a
printf 'A\nb\n' | grep --invert-match --line-number A
printf 'a\n' | grep --quiet a; echo $?
printf 'a\n' | grep --silent a; echo $?

#### --regexp and --file
printf 'b\n' > pats
printf 'a\nb\nc\n' | grep --regexp=a --file=pats

#### --regexp with separate argument
printf 'a\nb\n' | grep --regexp a

#### --extended-regexp --fixed-strings --basic-regexp
printf 'a+\naa\n' | grep --extended-regexp 'a+$'
printf 'a+\naa\n' | grep --fixed-strings 'a+'
printf 'a+\naa\n' | grep --basic-regexp 'a+'

#### -G basic
printf 'a+\naa\n' | grep -G 'a\+'

#### --line-regexp
printf 'ab\nabc\n' | grep --line-regexp ab

#### --no-messages
grep --no-messages a missing; echo $?

#### --color=never and --colour=auto
printf 'a\n' | grep --color=never a
printf 'a\n' | grep --colour=auto a
printf 'a\n' | grep --color a

#### -Z after names
printf 'a\n' > f1
grep -lZ a f1 | od -c

#### --null
printf 'a\n' > f1
grep --null -l a f1 | od -c

#### -z NUL-separated input
printf 'a\0b\0' | grep -z a | od -c

#### -a binary as text
printf 'a\0b\n' | grep -a a | od -c

#### binary file matches message
printf 'a\0b\n' > bin
grep a bin; echo $?

#### -I skips binary files
printf 'a\0b\n' > bin
grep -I a bin; echo $?

#### -b byte offset
printf 'ab\ncd\n' | grep -b c

#### -s with -r on unreadable
grep -rs x nonexist; echo $?

#### --version exits 0
grep --version >/dev/null; echo $?

#### --help exits 0
grep --help >/dev/null; echo $?

#### -e with -w -o -i combined
printf 'Foo foobar FOO\n' | grep -owi foo

#### -c with -v and several files
printf 'a\nb\n' > f1
printf 'a\n' > f2
grep -cv a f1 f2

#### -x -F several patterns
printf 'ab\nabc\n' | grep -xF -e ab -e zz

#### ERE backreference
printf 'abab\nabcd\n' | grep -E '(ab)\1'

#### --only-matching empty match is skipped
printf 'abc\n' | grep -o 'x*'; echo $?

#### -n with -r
mkdir d
printf 'x\nhit\n' > d/f
grep -rn hit d

#### fgrep and egrep
printf 'a.c\nabc\n' | fgrep a.c
printf 'ab\nabb\n' | egrep -c 'b+'

#### -P is refused or works
printf 'a1\n' | grep -P '\d' >/dev/null 2>&1; echo $?

#### -y is -i
printf 'A\n' | grep -y a
