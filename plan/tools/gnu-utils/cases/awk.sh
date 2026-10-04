# ws045: the gawk extensions scripts use, compared with gawk without
# POSIXLY_CORRECT.

#### gensub (gdb update-linux-from-src.sh)
echo 'x removed sys_read' | awk '{print $2, gensub("^sys_", "", 1, $3)}'

#### gensub global and with groups
echo 'a1b22' | awk '{print gensub(/([a-z])([0-9]+)/, "<\\2\\1>", "g")}'

#### gensub on $0 by default and the nth match
echo 'aaa' | awk '{print gensub(/a/, "b", 2)}'

#### tolower and toupper
echo 'Banner x' | awk 'tolower($1)=="banner"{print toupper($2)}'

#### length without parentheses
echo 'abc' | awk '{for (i=length; i>0; i--) printf "%s", substr($0, i, 1); print ""}'

#### length of an array
echo | awk '{a[1]; a[2]; print length(a)}'

#### ** and **=
awk 'BEGIN { x = 2 ** 10; y = 3; y **= 2; print x, y }'

#### func keyword
awk 'func f(a) { return a * 2 } BEGIN { print f(4) }'

#### \x escape
awk 'BEGIN { printf "%s\n", "\x41\x42" }'

#### fflush with no argument
awk 'BEGIN { print "a"; fflush(); print "b" }'

#### delete a whole array
awk 'BEGIN { a[1]; a[2]; delete a; n = 0; for (k in a) n++; print n }'

#### nextfile
printf 'a\nb\n' > f1
printf 'c\nd\n' > f2
awk '{ print; nextfile }' f1 f2

#### systime and strftime
awk 'BEGIN { t = systime(); if (t > 1600000000) print "ok"; print strftime("%Y", 0, 1) }'

#### strftime with a format only
TZ=UTC awk 'BEGIN { s = strftime("%H", 3600, 1); print s }'

#### RS as a regex and RT
printf 'a1b22c' | awk 'BEGIN { RS = "[0-9]+" } { print $0 "|" RT }'

#### RS as a multi-character string
printf 'a--b--c' | awk 'BEGIN { RS = "--" } { print NR ": " $0 }'

#### RS empty paragraph mode (POSIX) still works
printf 'a\nb\n\nc\n' | awk 'BEGIN { RS = "" } { print NR ": " $1 }'

#### FS single space and tab
printf 'a\tb c\n' | awk -F'\t' '{ print $2 }'

#### match with an array
echo 'foo=123' | awk '{ if (match($0, /([a-z]+)=([0-9]+)/, m)) print m[1], m[2] }'

#### asort and asorti
awk 'BEGIN { a["x"] = 3; a["y"] = 1; a["z"] = 2; n = asort(a); for (i = 1; i <= n; i++) printf "%s ", a[i]; print ""; b["q"]; b["b"]; n = asorti(b, c); print c[1], c[2] }'

#### and or xor lshift rshift compl
awk 'BEGIN { print and(12, 10), or(12, 10), xor(12, 10), lshift(1, 4), rshift(16, 2) }'

#### printf %c with a number
awk 'BEGIN { printf "%c%c\n", 65, "BCD" }'

#### substr with a large length
awk 'BEGIN { print substr("hello", 2, 100) }'

#### -v with escapes
awk -v 's=a\tb' 'BEGIN { print s }'

#### --version exits 0
awk --version >/dev/null; echo $?

#### -- ends options and ARGV
awk -- 'BEGIN { print ARGV[1] }' x

#### ENVIRON
X=val awk 'BEGIN { print ENVIRON["X"] }'

#### printf %i
awk 'BEGIN { printf "%i\n", 3.9 }'

#### regex dynamic with a string
echo 'a.c' | awk '{ print ($0 ~ "a\\.c") }'

#### getline from a command
awk 'BEGIN { "echo hi" | getline x; print x }'

#### close returns the status
awk 'BEGIN { print "x" | "cat"; r = close("cat"); print r }'

#### system return value
awk 'BEGIN { r = system("exit 3"); print r }'

#### sprintf with %*d
awk 'BEGIN { printf "%*d|\n", 5, 42 }'

#### index of a regex-free string and --posix accepted
awk --posix 'BEGIN { print index("abc", "c") }'

#### -e program text
awk -e 'BEGIN { print "e" }'

#### @include is not needed; -f twice
printf 'function g() { return 7 }\n' > lib.awk
printf 'BEGIN { print g() }\n' > main.awk
awk -f lib.awk -f main.awk

#### FS as a regex with --field-separator
echo 'a1b2c' | awk --field-separator='[0-9]' '{ print $3 }'

#### --assign
awk --assign x=5 'BEGIN { print x }'

#### gensub with & and \\0 and a number
echo 'abcabc' | awk '{ print gensub(/b/, "[&]", "g"), gensub(/(b)(c)/, "<\\2\\1\\0>", 2) }'

#### gensub with \\& and a target variable kept
awk 'BEGIN { s = "x.y"; t = gensub(/\./, "\\&", "g", s); print s, t }'

#### gensub of G and an empty match
echo 'abc' | awk '{ print gensub(/x*/, "-", "G") }'

#### strftime in UTC
awk 'BEGIN { print strftime("%Y-%m-%d %H:%M:%S", 86400 * 365, 1) }'

#### mktime
TZ=UTC awk 'BEGIN { print mktime("2024 02 29 12 00 00") }'

#### mktime of a bad text
awk 'BEGIN { print mktime("2024 02") }'

#### lshift rshift compl and xor of many
awk 'BEGIN { print lshift(3, 2), rshift(255, 4), compl(0), xor(1, 2, 4), and(7, 6, 5), or(1, 8, 16) }'

#### asort with a destination keeps the source
awk 'BEGIN { a["x"] = "pear"; a["y"] = 10; a["z"] = "apple"; a["w"] = 2; n = asort(a, b); for (i = 1; i <= n; i++) printf "%s ", b[i]; print a["x"] }'

#### asorti sorts keys
awk 'BEGIN { a["b"]; a["a"]; a["c"]; n = asorti(a); for (i = 1; i <= n; i++) printf "%s ", a[i]; print n }'

#### match with an array: start and length
echo 'key=value' | awk '{ match($0, /([a-z]+)=([a-z]+)/, m); print m[0], m[1], m[2], m[2, "start"], m[2, "length"] }'

#### match with an array and no match empties it
awk 'BEGIN { m["old"] = 1; r = match("abc", /z/, m); n = 0; for (k in m) n++; print r, n }'

#### RS regex ending the input
printf 'a12b3\n' | awk 'BEGIN { RS = "[0-9]+" } { printf "[%s|%s]", $0, RT } END { print "" }'

#### RS regex of newlines (paragraphs)
printf 'a\nb\n\n\nc\n' | awk 'BEGIN { RS = "\n\n+" } { print NR ": " $0 }'

#### RS single character sets RT
printf 'a;b' | awk 'BEGIN { RS = ";" } { printf "%s<%s>", $0, RT } END { print "" }'

#### ** is right associative and **= assigns
awk 'BEGIN { print 2 ** 3 ** 2; x = 3; x **= 3; print x }'

#### func with a regex using \x
echo 'aAb' | awk 'func f(s) { return gsub(/\x41/, "-", s) } { print f($0) }'

#### -e with -f
printf 'function g() { return 5 }\n' > lib.awk
awk -f lib.awk -e 'BEGIN { print g() }'

#### --field-separator and --assign together
echo 'a:b' | awk --field-separator=: --assign=x=1 '{ print $2, x }'

#### options end at the program
awk 'BEGIN { print ARGV[1] }' -v

#### system of a signal
awk 'BEGIN { print system("kill -9 $$") }'
