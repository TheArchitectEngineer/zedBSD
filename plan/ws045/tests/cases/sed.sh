# ws045: GNU extensions of sed, compared with GNU sed without POSIXLY_CORRECT.

#### BRE \+ (emacs configure: the gcc version)
echo 'gcc version 13.2.0 (Debian)' | sed -n 's/^gcc version \([0123456789]\+.[0123456789]\+\).*/\1/p'

#### BRE \? (bash aclocal: emacs lisp dir)
printf '/usr/lib/emacs/site-lisp\n/usr/lib/xemacs/site-lisp\n/usr/lib/foo\n' | sed -n -e '/.*\/lib\/\(x\?emacs\/site-lisp\)$/{s,,${libdir}/\1,;p;q;}'

#### BRE \? after a group
printf 'color\ncolour\ncolouur\n' | sed -n '/^colou\?r$/p'

#### BRE \| alternation
printf 'CPU=x\nMIPS_ENDIAN=el\nLIBCABI=gnu\nOTHER=1\n' | sed -n '/^CPU\|^MIPS_ENDIAN\|^LIBCABI/p'

#### BRE \| inside a group
printf 'foo.c\nbar.h\nbaz.o\n' | sed -n 's/\.\(c\|h\)$/ source/p'

#### \t in the regex and replacement (binutils opcodes configure)
printf '\t$(AM_V_CCLD) foo\nother\n' | sed -e 's/^\t\(\$(AM_V_CCLD)\)/\t+ \1/' | od -c

#### \t inside a bracket (emacs configure)
printf 'programs: =/usr/bin\nprograms:\t/opt\n' | sed -n 's/^programs:[\t ]*=\?\(.*\)/\1/p'

#### \n inside a bracket
printf 'a\nb\n' | sed 'N; s/[\n]/+/'

#### \w and \W
echo 'foo_bar-baz 12' | sed 's/\w\+/X/g; s/\W/./'

#### \s and \S
printf 'cacheversion  = 9\n' | sed -E 's/cacheversion\s*=\s*([0-9]*)/\1/'

#### \S+
echo 'one   two three' | sed -E 's/\S+/<&>/2'

#### \< and \> (gdb configure)
echo '$datadir/x $datadirs' | sed -e 's/[$]datadir\>/D/g'

#### \b word boundary
echo 'cat concat cat.' | sed 's/\bcat\b/dog/g'

#### \B
echo 'cat concat' | sed 's/\Bcat/CAT/g'

#### \` and \' buffer anchors
printf 'a\nb\n' | sed 'N; s/^/>/mg; s/\`/[/; s/'"\\\\'"'/]/'

#### -E with \w (ncurses configure)
echo '-lfoo -Lbar' | sed -E "s#-l(\w*)#\1.dll.lib#g" | sed -E "s#-L(\w*)#-LIBPATH:\1#g"

#### -r is -E
echo 'aaa bbb' | sed -r 's/(a+) (b+)/\2 \1/'

#### --regexp-extended
echo 'aaa bbb' | sed --regexp-extended 's/(a+) (b+)/\2 \1/'

#### -i edits in place
printf 'CACHE_VERSION=8\nother\n' > f
sed -i -e '/^CACHE_VERSION=/s/[0-9]\+/9/' f
cat f

#### -i after the file operand (fontconfig new-version.sh)
printf 'cacheversion = 8\n' > m
sed -i m -e "/^cacheversion/s/[0-9]\+/9/"
cat m

#### -i with a suffix keeps a backup
printf 'x\n' > f
sed -i.bak 's/x/y/' f
cat f f.bak

#### --in-place=SUFFIX
printf 'x\n' > f
sed --in-place=.orig 's/x/y/' f
cat f f.orig

#### --in-place without a suffix
printf 'x\n' > f
sed --in-place 's/x/z/' f
cat f; ls

#### -i on several files numbers each from 1 and $ is each last
printf 'a\nb\n' > f1
printf 'c\nd\n' > f2
sed -i '1s/^/[/; $s/$/]/' f1 f2
cat f1 f2

#### -i keeps the mode of the file
printf 'x\n' > f
chmod 640 f
sed -i 's/x/y/' f
ls -l f | cut -c1-10

#### -i with w /dev/stdout
printf 'a\nb\n' > f
sed -i -n '/a/w /dev/stdout' f
echo ---; cat f

#### -i on a missing file continues with the next
printf 'a\n' > f
sed -i 's/a/b/' missing f 2>/dev/null
echo $?; cat f

#### -s treats files separately
printf 'a\nb\n' > f1
printf 'c\nd\n' > f2
sed -s -n '$p' f1 f2

#### --separate with line numbers
printf 'a\nb\n' > f1
printf 'c\nd\n' > f2
sed --separate -n '1p' f1 f2

#### -z NUL-separated lines
printf 'a\0b\0' | sed -z 's/^/x/' | od -c

#### --null-data with $
printf 'one\0two\0' | sed --null-data -n '$p' | od -c

#### -n --quiet --silent
printf 'a\nb\n' | sed --quiet 1p
printf 'a\nb\n' | sed --silent 2p

#### --expression and --file
printf 's/a/b/\n' > script
printf 'a\nc\n' | sed --expression='s/c/d/' --file=script

#### --expression with a separate argument
printf 'a\n' | sed --expression 's/a/b/'

#### -u and -l are accepted
printf 'a\n' | sed -u 's/a/b/'

#### --posix is accepted
printf 'a\n' | sed --posix 's/a/b/'

#### --debug not needed; --version prints something
sed --version >/dev/null; echo $?

#### --help exits 0
sed --help >/dev/null; echo $?

#### \U and \L in the replacement
echo 'hello world' | sed 's/\(hello\) \(world\)/\U\1\E \u\2/'

#### \L and \l
echo 'HELLO WORLD' | sed -E 's/(\w+) (\w+)/\L\1 \l\2/'

#### \u then \L
echo 'jOHN smITH' | sed -E 's/(\w+)/\L\u&/g'

#### \U stays on to the end of the replacement
echo 'abc def' | sed 's/.*/\U&-x/'

#### \n in the replacement
echo 'a,b' | sed 's/,/\n/'

#### newline escape in the replacement (backslash-newline)
echo 'a,b' | sed 's/,/\
/'

#### 0,/re/ ends on the first line
printf 'x\ny\nx\n' | sed '0,/x/s//X/'

#### 1,/re/ differs from 0,/re/
printf 'x\ny\nx\n' | sed '1,/x/s/x/X/'

#### first~step
seq 10 | sed -n '1~3p'

#### 0~4
seq 10 | sed -n '0~4p'

#### addr,+N
seq 10 | sed -n '/4/,+2p'

#### addr,~N
seq 10 | sed -n '5,~4p'

#### $! and ranges with !
seq 5 | sed -n '2,4!p'

#### I address flag
printf 'Foo\nbar\n' | sed -n '/foo/Ip'

#### s///I
echo 'FOO foo' | sed 's/foo/x/Ig'

#### s///M with ^ and $
printf 'a\nb\n' | sed 'N; s/^/>/Mg'

#### s///e not supported check (number and g)
echo 'aaaa' | sed 's/a/b/2g'

#### a one-liner
printf 'a\nb\n' | sed '1a hello'

#### a one-liner keeps leading blanks after a backslash
printf 'a\n' | sed '1a\  two'

#### i one-liner and c one-liner
printf 'a\nb\n' | sed -e '1i top' -e '2c changed'

#### a with -e continuation
printf 'a\n' | sed -e '1a\' -e 'next'

#### a in a block with }
printf 'a\nb\n' | sed '/a/{a x
}'

#### a, i, c text with escapes
printf 'a\n' | sed 'a\tx\ty'

#### { cmd } on one line
printf 'library_names='"'"'libfoo.so.1 libfoo.so'"'"'\n' | sed -ne "/^library_names=/{s/.*='//;s/'\$//;s/ .*//;p;}"

#### } after a command without ;
printf 'a\nb\n' | sed -n '/a/{p}'

#### Q quits without printing
seq 5 | sed '3Q'

#### q with an exit status
seq 5 | sed '2q5'; echo $?

#### Q with an exit status
seq 5 | sed '2Q7'; echo $?

#### T branches when there was no substitution
printf 'ab\ncd\n' | sed 's/a/A/; T; s/$/!/'

#### T to a label
printf 'ab\ncd\n' | sed -e ':x' -e 's/a/A/; Tend' -e 's/$/!/' -e ':end'

#### F prints the file name
printf 'a\n' > f
sed -n F f
printf 'a\n' | sed F

#### z empties the pattern space
printf 'a\nb\n' | sed '1z'

#### W writes the first line
printf 'a\nb\n' | sed -n 'N; W out'
cat out

#### R reads a line of a file each time
printf '1\n2\n' > lines
printf 'a\nb\nc\n' | sed 'R lines'

#### e command runs the pattern space? (e with a command)
printf 'a\n' | sed '1e echo hi'

#### e with no argument runs the pattern space
printf 'echo run\n' | sed 'e'

#### s///e runs the result
printf 'x\n' | sed 's/x/echo made/e'

#### v is accepted
printf 'a\n' | sed 'v 4.2'

#### l with a line length
printf 'aaaaaaaaaaaaaaaaaaaa\n' | sed -n 'l 8'

#### -l sets the line length of l
printf 'aaaaaaaaaaaaaaaaaaaa\n' | sed -n -l 6 l

#### = on its own
printf 'a\nb\n' | sed -n '$='

#### comment after a command
printf 'a\n' | sed 's/a/b/ # comment'

#### y with \n
printf 'a b\n' | sed 'y/ /\n/'

#### empty regex reuses the last with other flags
echo 'aAa' | sed '/a/s//x/g'

#### w /dev/stderr
printf 'a\n' | sed 'w /dev/stderr' 2>/dev/null

#### special replacement: & escaped
echo 'x' | sed 's/x/[\&]/'

#### sed -n with #n first line and -s
printf 'a\n' | sed '#n
p'

#### -E with + and ? and |
printf 'ab\nabb\nac\n' | sed -E -n '/^ab+$|^ac?$/p'

#### ERE interval and backreference
echo 'abab' | sed -E 's/(ab)\1/X/'

#### -i on a file with no final newline
printf 'a' > f
sed -i 's/a/b/' f
od -c f

#### --line-length
printf 'aaaaaaaaaaaaaaaaaaaa\n' | sed -n --line-length=10 l

#### -E -i combined as -Ei
printf 'aa\n' > f
sed -Ei 's/a+/b/' f
cat f

#### -ni combination
printf 'a\nb\n' > f
sed -ni '2p' f
cat f

#### --follow-symlinks with -i
printf 'a\n' > target
ln -s target link
sed -i --follow-symlinks 's/a/b/' link
cat target; ls -l link | cut -c1

#### -i replaces a symlink without --follow-symlinks
printf 'a\n' > target
ln -s target link
sed -i 's/a/b/' link
cat target link; ls -l link | cut -c1

#### -s: $ and line numbers per file
printf 'a\nb\nc\n' > s1; printf 'd\ne\nf\n' > s2
sed -s -n '$=' s1 s2
sed -s -n '2,1p' s1 s2

#### -s: ranges end with the file
printf 'a\nb\nc\n' > s1; printf 'd\ne\nf\n' > s2
sed -s -n '/b/,/e/p' s1 s2

#### -s: the hold space goes on across files
printf 'a\nb\n' > s1; printf 'c\nd\n' > s2
sed -s -n '1h;2{x;p}' s1 s2

#### -s: N at the end of a file writes it and goes on
printf 'x\ny\n' > f1; printf 'z\n' > f2
sed -s 'N;N;s/\n/+/g' f1 f2

#### N at the end writes the pattern space (GNU)
printf 'x\ny\nz\n' | sed 'N;s/\n/+/'

#### N at the end with POSIXLY_CORRECT does not
printf 'x\ny\nz\n' | POSIXLY_CORRECT=1 sed 'N;s/\n/+/'

#### N at the end with --posix does not
printf 'x\ny\nz\n' | sed --posix 'N;s/\n/+/'

#### -s: n at the end of a file goes on with the next
printf 'x\n' > h1; printf 'y\nz\n' > h2
sed -s 'n;s/^/+/' h1 h2

#### -s: q ends everything
printf 'x\ny\n' > f1; printf 'z\n' > f2
sed -s 2q f1 f2

#### -s: $ skips an empty file
printf 'x\ny\n' > f1; : > f3; printf 'z\n' > f2
sed -s -n '$p' f1 f3 f2

#### -s: a missing final newline is written when more follows
printf 'a\nb' > u1; printf 'c\n' > u2
sed -s p u1 u2 | od -c

#### -i with q truncates the file and leaves the rest
printf 'a\nb\nc\n' > t1; printf 'd\n' > t2
sed -i 2q t1 t2
cat t1; echo --; cat t2

#### -i with 1d on each file
printf 'x\ny\n' > g1; printf 'z\n' > g2
sed -i 1d g1 g2
cat g1 g2

#### -i with r
printf 'a\nb\n' > t1; printf 'R\n' > r
sed -i '1r r' t1
cat t1

#### -i: the backup with * in the suffix
printf 'a\n' > t1
sed -i'old_*.b' 's/a/x/' t1
cat t1 old_t1.b

#### -i: the backup in another directory
mkdir bk
printf 'a\n' > t1
sed -i'bk/*.old' 's/a/x/' t1
cat t1 bk/t1.old

#### -ie makes the suffix e
printf 'a\n' > t1
sed -ie 's/a/x/' t1
cat t1 t1e

#### -i with no file
printf 'x\n' | sed -i 's/x/y/'; echo $?

#### -i on a directory
mkdir dd
sed -i 's/x/y/' dd; echo $?

#### -i on a file in another directory
mkdir sub
printf 'a\n' > sub/f
sed -i 's/a/b/' sub/f
cat sub/f; ls sub

#### -n -i $=
printf 'x\ny\n' > f
sed -n -i '$=' f
cat f

#### -i -s with N at the end of a short file
printf 'x\ny\n' > f
sed -i -s 'N;N;s/\n/+/' f
cat f

#### -z: = and G and P and D
printf 'a\0b\0' | sed -z '=' | od -c
printf 'a\0b\0' | sed -z '1h;2G' | od -c
printf 'a\0b\0c\0' | sed -z -n 'N;N;P' | od -c
printf 'a\0b\0c\0' | sed -z 'N;D' | od -c

#### -z: a file of lines is one record
printf 'a\nb\n' | sed -z 's/a/b/' | od -c

#### -z: l
printf 'a\0b\0' | sed -z -n 'l' | od -c

#### -z: missing final NUL
printf 'a\0b' | sed -z 'p' | od -c

#### -l widths
printf 'abc\n' | sed -n -l 3 l
printf 'abcdef\n' | sed -n -l 1 l
printf 'abcdef\n' | sed -n 'l 0'
printf 'abcdef\n' | sed -n 'l 4'

#### --sandbox refuses w
printf 'x\n' | sed --sandbox 'w out'; echo $?

#### --sandbox refuses r
printf 'x\n' | sed --sandbox 'r out'; echo $?

#### --sandbox allows the rest
printf 'x\n' | sed --sandbox 's/x/y/'

#### -u and -z together with options after the script
printf 'a\0' | sed 's/a/b/' -u -z | od -c

#### -s with -n and F-less script on standard input
printf 'a\nb\n' | sed -s -n '$p'

#### unknown long option
sed --nothing p </dev/null; echo $?

#### ambiguous long option prefix
printf 'x\n' | sed --qui p

#### -- then a script that starts with -
printf 'x\n' | sed -- -n; echo $?

#### --expression twice
printf 'x\n' | sed -n --expression=p --expression p
