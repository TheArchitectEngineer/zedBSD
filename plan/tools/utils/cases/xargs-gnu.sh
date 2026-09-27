#### xargs --null and -0
printf 'a b\0c\0\0d' | xargs -0 printf '[%s]\n'; printf 'x\0y\0' | xargs --null -n 1 echo

#### xargs -0 has no end string
printf 'a\0END\0b\0' | xargs -0 -E END echo

#### xargs -d with a character and escapes
printf 'a,b c,"d"' | xargs -d , printf '[%s]\n'; printf 'a\nb c\n' | xargs -d '\n' printf '[%s]\n'; printf 'a\tb' | xargs -d '\t' printf '[%s]\n'; printf 'a:b' | xargs -d '\x3a' printf '[%s]\n'; printf 'a:b' | xargs -d '\072' printf '[%s]\n'; printf 'a\0b' | xargs -d '\0' printf '[%s]\n'

#### xargs --delimiter and a bad one
printf 'p;q' | xargs --delimiter=';' echo; xargs -d ab echo < /dev/null > /dev/null 2>&1; echo "st=$?"

#### xargs -r and --no-run-if-empty
printf '' | xargs -r echo ran; echo "st=$?"; printf '  \n' | xargs --no-run-if-empty echo ran; printf '' | xargs echo ran

#### xargs -P runs several at once
printf '1 2 3 4 5 6\n' | xargs -P 3 -n 1 sh -c 'echo "$1"' sh | sort; printf '1 2\n' | xargs -P 0 -n 1 echo | sort; printf 'a\n' | xargs --max-procs=2 echo

#### xargs -P and failures
printf '1 2 3\n' | xargs -P 2 -n 1 sh -c 'exit $(( $1 % 2 ))' sh; echo "st=$?"; printf '1 2 3\n' | xargs -P 2 -n 1 sh -c 'test $1 = 2 && exit 255; exit 0' sh 2> /dev/null; echo "st=$?"

#### xargs -a reads a file
printf 'f1 f2\nf3\n' > list; xargs -a list echo; xargs --arg-file=list -n 1 echo; echo 'from stdin' | xargs -a list sh -c 'cat; echo "$@"' sh

#### xargs old forms -e -i -l
printf 'a b\nSTOP\nc\n' | xargs -eSTOP echo; printf 'a\nb\n' | xargs -i echo '<{}>'; printf 'a\nb\n' | xargs -iX echo '<X>'; printf 'a b\nc\nd e\n' | xargs -l echo; printf 'a\nb\nc\n' | xargs -l2 echo; printf 'a\nb\n' | xargs -e echo

#### xargs long options
printf 'a b c d\n' | xargs --max-args=2 echo; printf 'a\nb\n' | xargs --replace echo '<{}>'; printf 'a\nb\nc\n' | xargs --max-lines=2 echo; printf 'a b\n' | xargs --verbose echo 2>&1; printf 'aa bb cc\n' | xargs --max-chars=11 echo; printf 'x\n' | xargs --eof=x echo none

#### xargs options end at the utility
printf 'a\n' | xargs echo -n; echo; printf 'a\n' | xargs -n 1 echo -r
