#### comm three columns
printf 'a\nb\nd\ne\n' > x; printf 'b\nc\ne\nf\n' > y; comm x y | od -c

#### comm -1 -2 -3
printf 'a\nb\nd\n' > x; printf 'b\nc\nd\n' > y; comm -1 x y; echo --; comm -2 x y; echo --; comm -3 x y; echo --; comm -12 x y; echo --; comm -13 x y; echo --; comm -23 x y; echo --; comm -123 x y | wc -c

#### comm duplicate lines
printf 'a\na\nb\n' > x; printf 'a\nb\nb\n' > y; comm x y

#### comm empty files
: > x; printf 'a\n' > y; comm x y; comm y x; comm x x | wc -c

#### comm standard input
printf 'a\nc\n' > x; printf 'b\nc\n' | comm x -; printf 'b\nc\n' | comm - x

#### comm no final newline
printf 'a\nb' > x; printf 'b\nc' > y; comm x y | od -c

#### comm empty lines
printf '\na\n' > x; printf '\nb\n' > y; comm x y | od -c

#### comm missing file
printf 'a\n' > x; comm x nosuch 2>/dev/null; echo "st=$?"

#### comm wrong operand count
printf 'a\n' > x; comm x 2>/dev/null; echo "st=$?"; comm x x x 2>/dev/null; echo "st=$?"
