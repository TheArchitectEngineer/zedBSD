#### install copies with mode 0755
printf 'data\n' > s; chmod 600 s; install s d; echo "st=$?"; cat d; ls -l d | cut -c1-10

#### install -m octal and symbolic
printf 'x' > s; install -m 644 s a; install -m u=rw,go=r s b; install -m 4755 s c; install --mode=600 s e; ls -l a b c e | cut -c1-10

#### install into a directory and several sources
mkdir t; printf '1' > a; printf '2' > b; install a b t; ls t; cat t/a t/b; install a t/; ls t | wc -l

#### install -t and -T
mkdir t; printf '1' > a; printf '2' > b; install -t t a b; ls t; mkdir u; install -T a u 2>/dev/null; echo "st=$?"; install -T a v; cat v

#### install -D
printf 'x' > s; install -D s p/q/r; ls -l p/q/r | cut -c1-10; ls -ld p p/q | cut -c1-10; install -D -t m/n s; ls m/n

#### install -d
install -d a/b/c x; echo "st=$?"; ls -ld a a/b a/b/c x | cut -c1-10; install -d -m 700 y/z; ls -ld y y/z | cut -c1-10; install -d y/z; echo "st=$?"

#### install -v
printf 'x' > s; install -v s d; mkdir t; install -v s t; install -v -D s e/f; install -dv g/h

#### install -p keeps the times
printf 'x' > s; touch -t 200102030405.06 s; install -p s d; date -r d +%Y%m%d%H%M; install s e; test e -nt s && echo newer

#### install replaces an existing file
printf 'old' > d; chmod 444 d; printf 'new' > s; install s d; cat d; ls -l d | cut -c1-10; ln -s s l; install s l; ls -l l | cut -c1

#### install -C leaves an identical file
printf 'x' > s; install s d; touch -t 200001010000 d; install -C s d; date -r d +%Y; printf 'y' > s; install -C s d; cat d

#### install -o and -g with the user's own ids
printf 'x' > s; install -o "$(id -u)" -g "$(id -g)" s d; echo "st=$?"; test "$(stat -c %u:%g d)" = "$(id -u):$(id -g)" && echo owned; install -o nosuchuser s e 2>/dev/null; echo "st=$?"

#### install errors
printf 'x' > s; mkdir t; install t d 2>/dev/null; echo "st=$?"; install nothere d 2>/dev/null; echo "st=$?"; install s 2>/dev/null; echo "st=$?"; install s s 2>/dev/null; echo "st=$?"; install a b c 2>/dev/null; echo "st=$?"; install -m 999 s x 2>/dev/null; echo "st=$?"

#### install -c is taken
printf 'x' > s; install -c s a; echo "st=$?"; cat a
