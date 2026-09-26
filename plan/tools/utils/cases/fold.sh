#### fold default width
awk 'BEGIN { for (i = 0; i < 200; i++) printf "%d", i % 10; print "" }' | fold | awk '{ print length($0) }'

#### fold -w
printf 'abcdefghij\n' | fold -w 4

#### fold -s
printf 'ab cd ef gh\n' | fold -s -w 5 | od -c

#### fold -s without blanks
printf 'abcdefghij kl\n' | fold -s -w 4

#### fold tabs count to the next stop
printf 'a\tb\tc\n' | fold -w 10 | od -c

#### fold -b counts bytes
printf 'a\tb\tc\n' | fold -b -w 3 | od -c

#### fold backspace and carriage return
printf 'abc\bd\n' | fold -w 3 | od -c; printf 'abcd\refgh\n' | fold -w 5 | od -c

#### fold a tab wider than the line
printf '\tabc\n' | fold -w 4 | od -c

#### fold keeps short lines and empty lines
printf 'a\n\nbc\n' | fold -w 2

#### fold no final newline
printf 'abcdef' | fold -w 4 | od -c

#### fold several files
printf 'abcdef\n' > f; fold -w 3 f f

#### fold invalid width
printf 'x\n' | fold -w 0 2>/dev/null; echo "st=$?"; printf 'x\n' | fold -w x 2>/dev/null; echo "st=$?"

#### fold missing file
fold nosuch 2>/dev/null; echo "st=$?"
