#### xargs echo by default
printf 'a b\nc\n' | xargs

#### xargs runs echo once for empty input
printf '' | xargs echo nonempty; echo "st=$?"

#### xargs -r runs nothing for empty input
printf '' | xargs -r echo r; echo "st=$?"

#### xargs -r with input
printf 'a\n' | xargs -r echo r

#### xargs -r with only blanks
printf '  \n\n' | xargs -r echo r; echo "st=$?"

#### xargs quoting
printf 'a\\ b "c d" '"'"'e f'"'"' g\\\\h\n' | xargs -n 1 echo

#### xargs empty quoted argument
printf 'a "" b\n' | xargs -n 1 echo x

#### xargs quotes join with text
printf 'x"a b"y z\n' | xargs -n 1 echo

#### xargs unmatched single quote
printf "a'b\n" | xargs echo; echo "st=$?"

#### xargs unmatched double quote
printf 'a"b\nc"\n' | xargs echo; echo "st=$?"

#### xargs backslash newline
printf 'a\\\nb c\n' | xargs -n 1 echo

#### xargs -n 2
printf '1 2 3 4 5\n' | xargs -n 2 echo

#### xargs -n 1 with initial arguments
printf 'a b\n' | xargs -n 1 echo x y

#### xargs -L 1
printf 'a b\nc\nd\n\ne\n' | xargs -L 1 echo

#### xargs -L trailing blank continues the line
printf 'a b \nc\nd\n\ne\n' | xargs -L 1 echo

#### xargs -L 2
printf 'a\nb c\nd\ne f g\nh\n' | xargs -L 2 echo

#### xargs -L blank then newlines continues to the next non-empty line
printf 'a \n\n\nb\nc\n' | xargs -L 1 echo

#### xargs last of -L and -n wins
printf 'a b\nc d\n' | xargs -L 1 -n 1 echo 2>/dev/null

#### xargs last of -n and -L wins
printf 'a b\nc d\n' | xargs -n 1 -L 1 echo 2>/dev/null

#### xargs -I
printf '  a b  \n\n c\n' | xargs -I{} echo "[{}]"

#### xargs -I replaces every occurrence
printf 'x\n' | xargs -I % echo a%b %% %

#### xargs -I in several arguments
printf 'p\nq\n' | xargs -I @ echo @1 @2 @3 @4 @5 @6

#### xargs -I keeps quotes meaning
printf '"a  b" c\n' | xargs -I % echo "<%>"

#### xargs -I with -0
printf 'a b\0c\0' | xargs -0 -I % echo "<%>"

#### xargs -E
printf 'a _ b\n' | xargs -E _ echo

#### xargs -E after quote removal
printf 'a "_" b\n' | xargs -E _ echo

#### xargs -E part of a word does not end
printf 'a x_ b\n' | xargs -E _ echo

#### xargs -E empty string disables
printf 'a _ b\n' | xargs -E '' echo

#### xargs -E with -L
printf 'a b\nc STOP\nd\n' | xargs -E STOP -L 1 echo

#### xargs -0
printf 'a\0b c\0\0d' | xargs -0 -n 1 echo

#### xargs -0 keeps quotes and newlines
printf 'a"b\nc\0'"'"'x\0' | xargs -0 -n 1 echo

#### xargs -0 with -L
printf 'a\0b\0c\0' | xargs -0 -L 2 echo

#### xargs -s splits command lines
printf '1 2 3 4 5 6 7 8 9\n' | xargs -s 12 echo

#### xargs -n with -s
printf '1 2 3 4 5\n' | xargs -n 2 -s 12 echo

#### xargs -s too small for one argument
printf '12345 6\n' | xargs -s 8 echo; echo "st=$?"

#### xargs -x with -n that fits
printf 'a b c\n' | xargs -n 2 -x -s 9 echo; echo "st=$?"

#### xargs -x with -n that does not fit
printf 'aaa bbb c\n' | xargs -n 2 -x -s 10 echo; echo "st=$?"

#### xargs -t writes the command to stderr
printf 'a b\n' | xargs -t echo x 2>&1

#### xargs -t with -n
printf 'a b\n' | xargs -t -n 1 echo 2>&1

#### xargs utility status 1 to 125 gives 123
printf 'x\n' | xargs sh -c 'exit 3'; echo "st=$?"

#### xargs goes on after a failing run
printf '1 2 3\n' | xargs -n 1 sh -c 'echo $0; test $0 != 2'; echo "st=$?"

#### xargs utility status 255 stops
printf '1 2 3\n' | xargs -n 1 sh -c 'echo $0; exit 255' 2>/dev/null; echo "st=$?"

#### xargs utility killed by a signal
printf '1 2\n' | xargs -n 1 sh -c 'echo $0; kill -9 $$' 2>/dev/null; echo "st=$?"

#### xargs utility not found
printf 'x\n' | xargs nosuchcmd 2>/dev/null; echo "st=$?"

#### xargs utility not executable
printf 'x\n' > f; chmod 644 f; printf 'x\n' | xargs ./f 2>/dev/null; echo "st=$?"

#### xargs runs a script without #!
printf 'echo script "$@"\n' > s; chmod 755 s; printf 'a b\n' | xargs ./s

#### xargs utility stdin is not the input
printf 'a\nb\n' | xargs -n 1 sh -c 'cat; echo "arg $0"'

#### xargs invalid -n
printf 'a\n' | xargs -n 0 echo 2>/dev/null; echo "st=$?"

#### xargs invalid -L
printf 'a\n' | xargs -L x echo 2>/dev/null; echo "st=$?"

#### xargs unknown option
printf 'a\n' | xargs -Z echo 2>/dev/null; echo "st=$?"

#### xargs many arguments are split within the limit
i=0; while [ $i -lt 5000 ]; do echo "argument-$i"; i=$((i + 1)); done | xargs sh -c 'echo $#' sh | awk '{ n += $1 } END { print n }'

#### xargs tab separates
printf 'a\tb\n' | xargs -n 1 echo

#### xargs input without final newline
printf 'a b' | xargs echo

#### xargs initial arguments with spaces
printf 'x\n' | xargs printf '[%s]\n' 'a b'
