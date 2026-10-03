#### pr default page
printf 'one\ntwo\nthree\n' > f; touch -t 200001020304.05 f; pr f | sed '/Page [0-9]*$/s/  */ /g' | od -c | sed 's/  */ /g'; pr f | wc -l

#### pr -l short pages
printf 'one\ntwo\nthree\n' > f; touch -t 200001020304.05 f; pr -l 12 f | sed '/Page [0-9]*$/s/  */ /g'

#### pr -l 10 has no header
printf 'one\ntwo\nthree\n' > f; pr -l 10 f

#### pr -t
printf 'one\ntwo\nthree\n' > f; pr -t f; pr -t -l 2 f

#### pr -h
printf 'one\ntwo\nthree\n' > f; touch -t 200001020304.05 f; pr -l 12 -h 'A title' f | sed '/Page [0-9]*$/s/  */ /g'

#### pr standard input
printf 'x\n' | pr -l 12 | sed '/Page [0-9]*$/s/  */ /g; s/^[A-Z][a-z][a-z] [ 0-9][0-9] [0-9][0-9]:[0-9][0-9] [0-9]*/DATE/'

#### pr two files
printf 'one\ntwo\nthree\n' > f; printf 'x\n' > g; touch -t 200001020304.05 f g; pr -l 12 f g | sed '/Page [0-9]*$/s/  */ /g'

#### pr +page
awk 'BEGIN { for (i = 1; i <= 10; i++) print i }' > n; touch -t 200001020304.05 n; pr +2 -l 12 n | sed '/Page [0-9]*$/s/  */ /g'

#### pr -F
printf 'one\ntwo\nthree\n' > f; touch -t 200001020304.05 f; pr -F -l 12 f | sed '/Page [0-9]*$/s/  */ /g' | od -c | sed 's/  */ /g'

#### pr -d
printf 'one\ntwo\nthree\n' > f; pr -d -t f; awk 'BEGIN { for (i = 1; i <= 3; i++) print i }' > g; touch -t 200001020304.05 g; pr -d -l 14 g | sed '/Page [0-9]*$/s/  */ /g'

#### pr -o
printf 'one\ntwo\n' | pr -o 3 -t

#### pr -n
printf 'one\ntwo\nthree\n' | pr -n -t; printf 'one\ntwo\n' | pr -n:3 -t; printf 'a\nb\n' | pr -nx2 -t

#### pr -e
printf 'a\tb\n' | pr -e4 -t; printf 'a\tb\n' | pr -e -t; printf 'axb\n' | pr -ex2 -t

#### pr -i
printf 'a         b\n' | pr -i4 -t | od -c | sed 's/  */ /g'; printf 'a         b\n' | pr -i -t | od -c | sed 's/  */ /g'

#### pr two columns
awk 'BEGIN { for (i = 1; i <= 10; i++) print i }' | pr -2 -t | expand

#### pr three columns balanced
awk 'BEGIN { for (i = 1; i <= 10; i++) print i }' | pr -3 -t -w 20 | expand; awk 'BEGIN { for (i = 1; i <= 7; i++) print i }' | pr -3 -t | expand

#### pr columns across
awk 'BEGIN { for (i = 1; i <= 10; i++) print i }' | pr -2 -a -t | expand; awk 'BEGIN { for (i = 1; i <= 7; i++) print i }' | pr -3 -a -t | expand

#### pr columns with -s
awk 'BEGIN { for (i = 1; i <= 10; i++) print i }' | pr -2 -s -t | od -c | sed 's/  */ /g'; awk 'BEGIN { for (i = 1; i <= 4; i++) print i }' | pr -2 -s: -t

#### pr columns cut long lines
printf '%s\n' aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa b | pr -2 -t | expand; printf 'abcdefgh\nijk\n' | pr -2 -w 10 -t | expand

#### pr columns with numbers
awk 'BEGIN { for (i = 1; i <= 10; i++) print i }' | pr -2 -n -t | expand

#### pr columns on pages
awk 'BEGIN { for (i = 1; i <= 12; i++) print i }' > n; touch -t 200001020304.05 n; pr -2 -l 14 n | expand | sed '/Page [0-9]*$/s/  */ /g'

#### pr columns with -o
awk 'BEGIN { for (i = 1; i <= 4; i++) print i }' | pr -o 2 -2 -t | expand

#### pr columns with a tab
printf 'a\tb\nc\n' | pr -2 -t | expand

#### pr -m
printf 'one\ntwo\nthree\n' > f; awk 'BEGIN { for (i = 1; i <= 5; i++) print i }' > n; pr -m -t f n | expand; pr -m -n -t n n | expand

#### pr -w in one column does nothing
printf '%0100d\n' 1 | pr -t -w 20

#### pr empty input prints nothing
: | pr -l 12 | wc -c; : > e; pr e | wc -c

#### pr missing file
pr nosuch 2>/dev/null; echo "st=$?"; pr -r nosuch; echo "st=$?"; printf 'x\n' > f; pr -t nosuch f 2>/dev/null; echo "st=$?"

#### pr invalid options
printf 'x\n' | pr -l x 2>/dev/null; echo "st=$?"; printf 'x\n' | pr -w 0 2>/dev/null; echo "st=$?"; printf 'x\n' | pr -q 2>/dev/null; echo "st=$?"
