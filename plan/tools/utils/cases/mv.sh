#### mv renames a file
printf 'x\n' > a; mv a b; ls; cat b

#### mv replaces an existing file
printf 'new\n' > a; printf 'old\n' > b; mv a b; ls; cat b

#### mv into a directory
mkdir d; printf 'x\n' > a; printf 'y\n' > b; mv a b d; ls; ls d

#### mv into a directory with trailing slash
mkdir d; printf 'x\n' > a; mv a d/; ls d

#### mv a directory
mkdir -p s/t; printf 'x\n' > s/t/f; mv s d; find . | sort

#### mv a directory onto an empty directory
mkdir -p s/t e; printf 'x\n' > s/t/f; mv s e/s; find e | sort

#### mv a directory onto a non-empty directory fails
mkdir -p s d/s/x; mv s d 2>/dev/null; echo "st=$?"; find . | sort

#### mv a file onto a directory named as target file
mkdir -p d/a; printf 'x\n' > a; mv a d 2>/dev/null; echo "st=$?"; find . | sort

#### mv a directory onto a file fails
mkdir s; printf 'x\n' > f; mv -T s f 2>/dev/null; echo "st=$?"; find . | sort

#### mv several sources to a non-directory fails
printf 'x\n' > a; printf 'y\n' > b; printf 'z\n' > c; mv a b c 2>/dev/null; echo "st=$?"; ls

#### mv a missing source fails but continues
mkdir d; printf 'x\n' > a; mv nosuch a d 2>/dev/null; echo "st=$?"; ls d

#### mv a directory into itself fails
mkdir -p s/t; mv s s/t 2>/dev/null; echo "st=$?"; find . | sort

#### mv same file fails
printf 'x\n' > a; mv a a 2>/dev/null; echo "st=$?"; ls

#### mv a symlink moves the link
printf 'x\n' > f; ln -s f l; mkdir d; mv l d; test -L d/l && echo link; readlink d/l

#### mv -i declines on no
printf 'new\n' > a; printf 'old\n' > b; echo n | mv -i a b 2>/dev/null; echo "st=$?"; ls; cat b

#### mv -i accepts on yes
printf 'new\n' > a; printf 'old\n' > b; echo y | mv -i a b 2>/dev/null; echo "st=$?"; ls; cat b

#### mv -f after -i does not ask
printf 'new\n' > a; printf 'old\n' > b; mv -i -f a b < /dev/null 2>/dev/null; echo "st=$?"; ls

#### mv -i after -f asks
printf 'new\n' > a; printf 'old\n' > b; echo n | mv -f -i a b 2>/dev/null; ls

#### mv unwritable destination without a terminal replaces it
printf 'new\n' > a; printf 'old\n' > b; chmod 444 b; mv a b < /dev/null; echo "st=$?"; cat b

#### mv keeps the mode and times
age() { if [ "$1" -nt ref ]; then echo newer; elif [ "$1" -ot ref ]; then echo older; else echo same; fi; }; touch -t 200001020304.05 ref; printf 'x\n' > a; chmod 640 a; touch -t 200001020304.05 a; mv a b; ls -l b | cut -c1-10; age b

#### mv a file across file systems
age() { if [ "$1" -nt ref ]; then echo newer; elif [ "$1" -ot ref ]; then echo older; else echo same; fi; }; touch -t 200001020304.05 ref; x=/dev/shm; test -d $x || x=/tmp; t=$x/ws001-mv.$$; mkdir $t; printf 'data\n' > f; chmod 640 f; touch -t 200001020304.05 f; mv f $t/g; echo "st=$?"; ls; cat $t/g; ls -l $t/g | cut -c1-10; age $t/g; rm -rf $t

#### mv a tree across file systems
x=/dev/shm; test -d $x || x=/tmp; t=$x/ws001-mv.$$; mkdir $t; mkdir -p s/t/u; printf '1\n' > s/f; printf '2\n' > s/t/u/g; ln -s f s/l; mkfifo s/p; chmod 750 s/t; mv s $t/s; echo "st=$?"; ls; (cd $t && find s | sort); readlink $t/s/l; test -p $t/s/p && echo fifo; ls -ld $t/s/t | cut -c1-10; rm -rf $t

#### mv a tree across file systems keeps hard links
x=/dev/shm; test -d $x || x=/tmp; t=$x/ws001-mv.$$; mkdir $t; mkdir s; printf '1\n' > s/f; ln s/f s/g; mv s $t/s; ls -i $t/s/f $t/s/g | awk '{ print $1 }' | uniq | wc -l; rm -rf $t

#### mv across file systems replaces an existing file
x=/dev/shm; test -d $x || x=/tmp; t=$x/ws001-mv.$$; mkdir $t; printf 'new\n' > f; printf 'old\n' > $t/f; mv f $t/f; echo "st=$?"; cat $t/f; ls; rm -rf $t

#### mv across file systems onto a non-empty directory fails
x=/dev/shm; test -d $x || x=/tmp; t=$x/ws001-mv.$$; mkdir $t; mkdir -p s $t/s/x; mv s $t/s 2>/dev/null; echo "st=$?"; ls; rm -rf $t

#### mv -n keeps an existing file
printf 'new\n' > a; printf 'old\n' > b; mv -n a b; ls; cat b

#### mv -- ends options
printf 'x\n' > -a; mv -- -a b; ls

#### mv unknown option
printf 'x\n' > a; mv -j a b 2>/dev/null; echo "st=$?"; ls

#### mv missing operand
printf 'x\n' > a; mv a 2>/dev/null; echo "st=$?"
