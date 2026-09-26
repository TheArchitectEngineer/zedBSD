#### patch normal diff with file operand
printf 'a\nb\nc\nd\ne\n' > x; printf 'a\nB\nc\ne\nf\n' > y; diff x y > p; patch x p > /dev/null; echo "st=$?"; cmp x y && echo same

#### patch normal diff from -i
printf '1\n2\n3\n' > x; printf '0\n1\n3\n4\n' > y; diff x y > p; patch -i p x > /dev/null; echo "st=$?"; cat x

#### patch unified with names from the headers
awk 'BEGIN { for (i = 1; i <= 20; i++) print i }' > x; sed 's/^5$/five/; /^12$/d; s/^18$/18\neighteen/' x > y; cp x z; diff -u z y > p; patch < p > /dev/null; echo "st=$?"; cmp z y && echo same; cmp x z > /dev/null || echo changed-z-only

#### patch context with names from the headers
awk 'BEGIN { for (i = 1; i <= 20; i++) print i }' > x; sed 's/^3$/three/; /^15$/d' x > y; cp x z; diff -c z y > p; patch < p > /dev/null; echo "st=$?"; cmp z y && echo same

#### patch ed script
printf 'a\nb\nc\nd\ne\n' > x; printf 'a\nB\nc\ne\nf\n.\ng\n' > y; diff -e x y > p; patch -e x p > /dev/null; echo "st=$?"; cmp x y && echo same

#### patch forced formats
printf 'a\nb\n' > x; printf 'a\nc\n' > y; diff x y > p; cp x w; patch -n w p > /dev/null; echo "st=$?"; cat w; diff -u x y > p; cp x w; patch -u w p > /dev/null; echo "st=$?"; cat w; diff -c x y > p; cp x w; patch -c w p > /dev/null; echo "st=$?"; cat w

#### patch -p strips leading components
mkdir -p a/d b/d; printf '1\n2\n3\n' > a/d/f; printf '1\ntwo\n3\n' > b/d/f; diff -u a/d/f b/d/f > p; mkdir -p d; cp a/d/f d/f; patch -p1 < p > /dev/null; echo "st=$?"; cat d/f; cp a/d/f f; patch < p > /dev/null; echo "st=$?"; cat f

#### patch -p0 keeps the whole name
mkdir d; printf '1\n2\n' > d/f; printf '1\n3\n' > n; diff -u d/f n > p; patch -p0 < p > /dev/null; echo "st=$?"; cat d/f

#### patch -d changes directory
mkdir d; printf 'x\ny\n' > d/f; printf 'x\nz\n' > g; cp d/f f0; diff f0 g > p; patch -d d f < p > /dev/null; echo "st=$?"; cat d/f

#### patch -R reverses
printf 'a\nb\nc\n' > x; printf 'a\nB\nc\nd\n' > y; diff -u x y > p; cp y w; patch -R w p > /dev/null; echo "st=$?"; cmp w x && echo same

#### patch -R with normal diff
printf 'a\nb\nc\n' > x; printf 'b\nc\nD\n' > y; diff x y > p; cp y w; patch -R w p > /dev/null; echo "st=$?"; cmp w x && echo same

#### patch -b saves the original
printf 'a\nb\n' > x; printf 'a\nc\n' > y; diff x y > p; cp x w; patch -b w p > /dev/null; echo "st=$?"; cat w w.orig

#### patch -o writes elsewhere
printf 'a\nb\n' > x; printf 'a\nc\n' > y; diff -u x y > p; cp x w; patch -o out w p > /dev/null; echo "st=$?"; cat w out

#### patch rejects a hunk
awk 'BEGIN { for (i = 1; i <= 20; i++) print i }' > x; sed 's/^3$/three/; s/^17$/seventeen/' x > y; diff -u x y > p; sed 's/^16$/sixteen/; s/^17$/SEVEN/; s/^18$/eighteen/' x > w; patch w p > /dev/null; echo "st=$?"; cat w; sed '1,2s/[ 	].*//' w.rej

#### patch -r names the reject file
printf '1\n2\n3\n4\n5\n6\n7\n8\n' > x; sed 's/^6$/six/' x > y; diff -c x y > p; printf '1\n2\n3\n4\nfive\nSIX\nseven\n8\n' > w; patch -r rej w p > /dev/null; echo "st=$?"; sed '1,2s/[ 	].*//' rej; ls

#### patch offset
awk 'BEGIN { for (i = 1; i <= 20; i++) print i }' > x; sed 's/^10$/ten/' x > y; diff -u x y > p; (printf 'new1\nnew2\nnew3\n'; cat x) > w; patch w p > /dev/null; echo "st=$?"; sed -n '12,14p' w

#### patch offset backwards
awk 'BEGIN { for (i = 1; i <= 20; i++) print i }' > x; sed 's/^10$/ten/; s/^16$/sixteen/' x > y; diff -c x y > p; sed '1,4d' x > w; patch w p > /dev/null; echo "st=$?"; cat w

#### patch fuzz
awk 'BEGIN { for (i = 1; i <= 20; i++) print i }' > x; sed 's/^10$/ten/' x > y; diff -u x y > p; sed 's/^7$/seven/; s/^13$/thirteen/' x > w; patch w p > /dev/null; echo "st=$?"; sed -n '6,14p' w

#### patch -l loose blanks
printf 'int  a;\nint b;\nint c;\n' > x; printf 'int  a;\nint B;\nint c;\n' > y; diff -u x y > p; printf 'int a;\nint\tb;\n  int c;\n' > w; patch -l w p > /dev/null; echo "st=$?"; cat w; printf 'int a;\nint\tb;\n  int c;\n' > v; patch v p > /dev/null; echo "st=$?"

#### patch -D keeps both versions
printf 'a\nb\nc\nd\ne\nf\n' > x; printf 'a\nB\nc\nd\ne\nf\nnew\n' > y; diff x y > p; cp x w; patch -D NEW w p > /dev/null; echo "st=$?"; cat w

#### patch -D with a deletion
printf 'a\nb\nc\n' > x; printf 'a\nc\n' > y; diff x y > p; cp x w; patch -D OLD w p > /dev/null; echo "st=$?"; cat w

#### patch several files
printf '1\n2\n' > a; printf '3\n4\n' > b; printf '1\nTWO\n' > a2; printf 'THREE\n4\n' > b2; cp a a0; cp b b0; { diff -u a a2; diff -u b b2; } > p; patch < p > /dev/null; echo "st=$?"; cat a b

#### patch several hunks
awk 'BEGIN { for (i = 1; i <= 40; i++) print i }' > x; sed 's/^2$/two/; s/^20$/twenty/; /^30$/d; s/^40$/forty\nmore/' x > y; for f in "" -c -u -e; do cp x w; diff $f x y > p; patch $f w p > /dev/null; echo "st=$?"; cmp w y && echo same; done

#### patch creates a file named by the operand
printf 'n1\nn2\n' > n; diff -u /dev/null n > p; rm n; patch n < p > /dev/null; echo "st=$?"; cat n

#### patch Index line names the file
printf '1\n2\n' > f; printf '1\n3\n' > g; { echo 'Index: f'; diff -c f g | sed '1,2s/[ 	].*$/ x/' | sed '1s/^/*** nothere/;1s/ x$//;2s/^/--- alsonot/;2s/ x$//'; } > p; head -3 p | cut -c1-20; patch < p > /dev/null; echo "st=$?"; cat f

#### patch missing newline at end
printf 'a\nb' > x; printf 'a\nc\n' > y; diff -u x y > p; cp x w; patch w p > /dev/null; echo "st=$?"; od -c w; diff -u y x > p; cp y w; patch w p > /dev/null; echo "st=$?"; od -c w

#### patch leading text before the diff
printf 'a\nb\n' > x; printf 'a\nc\n' > y; { echo 'From: someone'; echo 'Subject: change'; echo; diff -u x y; } > p; cp x w; patch w p > /dev/null; echo "st=$?"; cat w

#### patch keeps the mode
printf 'a\nb\n' > x; printf 'a\nc\n' > y; diff x y > p; cp x w; chmod 751 w; patch w p > /dev/null; ls -l w | cut -c1-10

#### patch garbage input
printf 'nothing here\n' > p; printf 'a\n' > w; patch w p > /dev/null 2>&1; echo "st=$?"; cat w

#### patch -p1 with diff -ru output
mkdir -p a/s b/s; awk 'BEGIN { for (i = 1; i <= 30; i++) print i }' > a/s/one; sed 's/^4$/four/; s/^25$/twenty-five/' a/s/one > b/s/one; printf 'x\ny\n' > a/two; printf 'x\nY\nz\n' > b/two; diff -ru a b > p; mkdir t; cp -R a/. t; cd t; patch -p1 < ../p > /dev/null; echo "st=$?"; cd ..; diff -r t b && echo same

#### patch -R with a context diff and fuzz
awk 'BEGIN { for (i = 1; i <= 20; i++) print i }' > x; sed 's/^10$/ten/' x > y; diff -c x y > p; sed 's/^7$/seven/' y > w; patch -R w p > /dev/null; echo "st=$?"; sed -n '6,11p' w

#### patch hunk not applicable leaves others applied
awk 'BEGIN { for (i = 1; i <= 30; i++) print i }' > x; sed 's/^3$/three/; s/^15$/fifteen/; s/^27$/twenty-seven/' x > y; diff -U1 x y > p; sed 's/^15$/FIFTEEN/' x > w; patch w p > /dev/null; echo "st=$?"; grep -c . w; grep -n 'e' w; sed '1,2d' w.rej
