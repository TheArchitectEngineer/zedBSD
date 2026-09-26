#### diff same files
printf 'a\nb\n' > x; cp x y; diff x y; echo "st=$?"

#### diff normal change
printf 'a\nb\nc\nd\ne\n' > x; printf 'a\nB\nc\nd\ne\nf\n' > y; diff x y; echo "st=$?"

#### diff normal delete and add
printf 'a\nb\nc\nd\n' > x; printf 'a\nd\ne\n' > y; diff x y

#### diff normal at the start and several lines
printf '1\n2\n3\nk\n' > x; printf 'n1\nn2\nk\n' > y; diff x y; printf 'k\n' > x; printf 'z\nk\n' > y; diff x y

#### diff empty files
: > x; printf 'a\nb\n' > y; diff x y; diff y x

#### diff missing newline
printf 'a\nb' > x; printf 'a\nc\n' > y; diff x y; diff -u x y | sed '1,2d'

#### diff -b
printf 'a  b\nc \nd\n' > x; printf 'a b\nc\n d\n' > y; diff -b x y; echo "st=$?"; diff x y > /dev/null; echo "st=$?"

#### diff -b only blank changes
printf 'a  b\t\nc\n' > x; printf 'a\tb\nc  \n' > y; diff -b x y; echo "st=$?"

#### diff -u
awk 'BEGIN { for (i = 1; i <= 20; i++) print i }' > x; sed 's/^5$/five/; s/^15$/fifteen/; /^10$/d' x > y; diff -u x y | sed '1,2s/[ 	].*//'; echo "st=$?"

#### diff -U 1 and -U 0
awk 'BEGIN { for (i = 1; i <= 20; i++) print i }' > x; sed 's/^5$/five/; s/^9$/nine/' x > y; diff -U 1 x y | sed '1,2s/[ 	].*//'; diff -U0 x y | sed '1,2s/[ 	].*//'

#### diff -u insert at start and delete at end
printf 'b\nc\nd\n' > x; printf 'a\nb\nc\n' > y; diff -u x y | sed '1,2s/[ 	].*//'

#### diff -u empty file
: > x; printf 'a\n' > y; diff -u x y | sed '1,2s/[ 	].*//'; diff -u y x | sed '1,2s/[ 	].*//'

#### diff -c
awk 'BEGIN { for (i = 1; i <= 20; i++) print i }' > x; sed 's/^5$/five/; /^15$/d; s/^8$/8\n8b/' x > y; diff -c x y | sed '1,2s/[ 	].*//'; echo "st=$?"

#### diff -C 1
awk 'BEGIN { for (i = 1; i <= 12; i++) print i }' > x; sed 's/^3$/three/; s/^10$/ten/' x > y; diff -C 1 x y | sed '1,2s/[ 	].*//'

#### diff -c insert only and delete only
printf 'a\nb\nc\n' > x; printf 'a\nb\nX\nc\n' > y; diff -c x y | sed '1,2s/[ 	].*//'; diff -c y x | sed '1,2s/[ 	].*//'

#### diff -e
printf 'a\nb\nc\nd\ne\n' > x; printf 'a\nB\nc\ne\nf\n' > y; diff -e x y; echo "st=$?"

#### diff -e applies with ed
awk 'BEGIN { for (i = 1; i <= 30; i++) print i }' > x; sed 's/^5$/five/; /^1[0-2]$/d; s/^20$/20\na\nb/' x > y; (diff -e x y; echo w) | ed -s x; cmp x y && echo same

#### diff -e with a lone dot
printf 'a\nb\n' > x; printf 'a\n.\nb\n' > y; (diff -e x y; echo w) | ed -s x; cmp x y && echo same

#### diff -f
printf 'a\nb\nc\nd\ne\n' > x; printf 'a\nB\nc\ne\nf\n' > y; diff -f x y

#### diff directory and file
mkdir d; printf 'a\n' > d/f; printf 'b\n' > f; diff d f; diff f d

#### diff directories
mkdir a b; printf '1\n' > a/same; printf '1\n' > b/same; printf 'x\n' > a/f; printf 'y\n' > b/f; printf 'o\n' > a/only; mkdir a/sub b/sub; diff a b; echo "st=$?"

#### diff -r
mkdir -p a/s b/s; printf 'x\n' > a/s/f; printf 'y\n' > b/s/f; printf 'n\n' > b/s/new; diff -r a b; echo "st=$?"

#### diff -r -u
mkdir -p a/s b/s; printf 'x\n' > a/s/f; printf 'y\n' > b/s/f; diff -r -u a b | sed '/^---\|^+++/s/[ 	].*//'

#### diff binary files
printf 'a\0b' > x; printf 'a\0c' > y; diff x y; echo "st=$?"

#### diff standard input
printf 'a\nb\n' > x; printf 'a\nc\n' | diff x -; echo "st=$?"; printf 'a\nb\n' | diff - x; echo "st=$?"

#### diff missing file
printf 'a\n' > x; diff x nosuch 2>/dev/null; echo "st=$?"

#### diff wrong operand count
printf 'a\n' > x; diff x 2>/dev/null; echo "st=$?"

#### diff unknown option
printf 'a\n' > x; diff -j x x 2>/dev/null; echo "st=$?"

#### diff large files with scattered changes
awk 'BEGIN { srand(1); for (i = 0; i < 2000; i++) print int(rand() * 50) }' > x; awk 'NR % 97 == 0 { print "changed"; next } NR % 131 == 0 { next } { print } NR % 173 == 0 { print "added" }' x > y; (diff -e x y; echo w) | ed -s x; cmp x y && echo same
