#### mkdir makes directories with the umask
umask 022; mkdir a b; ls -ld a b | cut -c1-10; umask 077; mkdir c; ls -ld c | cut -c1-10

#### mkdir existing fails but continues
umask 022; mkdir a; mkdir a b 2>/dev/null; echo "st=$?"; ls

#### mkdir missing parent fails
umask 022; mkdir a/b 2>/dev/null; echo "st=$?"; ls

#### mkdir -p makes parents
umask 022; mkdir -p a/b/c; ls -ld a a/b a/b/c | cut -c1-10

#### mkdir -p parents get owner write and search
umask 077; mkdir -p x/y/z; ls -ld x x/y x/y/z | cut -c1-10; umask 0700; mkdir -p p/q; ls -ld p | cut -c1-10; chmod 700 p p/q

#### mkdir -p -m applies to the last only
umask 022; mkdir -p -m 700 q/r; ls -ld q q/r | cut -c1-10

#### mkdir -p existing is fine
umask 022; mkdir -p a/b; mkdir -p a/b; echo "st=$?"; mkdir -p a; echo "st=$?"

#### mkdir -p through a file fails
umask 022; touch f; mkdir -p f/g 2>/dev/null; echo "st=$?"; mkdir -p f 2>/dev/null; echo "st=$?"

#### mkdir -p with slashes
umask 022; mkdir -p a//b///c/; find a | sort

#### mkdir -m symbolic
umask 022; mkdir -m u=rwx,g=rx,o= e; ls -ld e | cut -c1-10; mkdir -m -w e2; ls -ld e2 | cut -c1-10; mkdir -m 2755 e4; ls -ld e4 | cut -c1-10

#### mkdir -m special bits
umask 022; mkdir -m +s e; ls -ld e | cut -c1-10; mkdir -m g+s e2; ls -ld e2 | cut -c1-10; mkdir -m -x e3; ls -ld e3 | cut -c1-10

#### mkdir -m ignores the umask
umask 077; mkdir -m 755 d; ls -ld d | cut -c1-10

#### mkdir -m invalid
umask 022; mkdir -m u+q d 2>/dev/null; echo "st=$?"; ls

#### mkdir trailing slash
umask 022; mkdir d/; ls

#### mkdir no operand
umask 022; mkdir 2>/dev/null; echo "st=$?"
