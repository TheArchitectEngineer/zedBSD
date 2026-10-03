#### expand default stops
printf 'a\tb\tc\n\tx\n12345678\ty\n' | expand | od -c

#### expand -t n
printf 'a\tb\tc\n' | expand -t 4 | od -c

#### expand -t list
printf 'a\tb\tc\n' | expand -t 4,8 | od -c

#### expand -t list with blanks, past the last stop
printf 'a\tb\tc\td\n' | expand -t '3 6' | od -c

#### expand backspace
printf 'ab\bc\td\n' | expand | od -c

#### expand several files and stdin
printf 'a\tb\n' > f; printf '\tc\n' | expand f - f | od -c

#### expand no final newline
printf 'a\tb' | expand | od -c

#### expand missing file continues
printf 'a\tb\n' > f; expand nosuch f 2>/dev/null | od -c; expand nosuch f > /dev/null 2>&1; echo "st=$?"

#### expand invalid lists
for t in 0 4,2 4,4 a 4x ''; do printf '\tx\n' | expand -t "$t" > /dev/null 2>&1; echo "$t st=$?"; done

#### unexpand leading blanks only
printf '        a       b\n' | unexpand | od -c

#### unexpand -a
printf '        a       b\n' | unexpand -a | od -c

#### unexpand -a runs and single spaces
printf '         a  b    c d\nx       y\nabcdefg a\n' | unexpand -a | od -c

#### unexpand -t
printf '    a\n' | unexpand -t 4 | od -c; printf '    a   b\n' | unexpand -t 4,8 | od -c; printf '            a\n' | unexpand -t 4,8 | od -c

#### unexpand mixed leading blanks
printf '  \t  a\n' | unexpand | od -c

#### unexpand keeps a line of blanks
printf '                \n' | unexpand | od -c; printf '   ' | unexpand | od -c

#### unexpand round trip
printf 'if (x) {\n\t\ty = 1;\n    }\n' > f; expand f | unexpand | cmp - f && echo same

#### unexpand missing file
unexpand nosuch 2>/dev/null; echo "st=$?"
