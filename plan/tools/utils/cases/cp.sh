#### cp copies a file
printf 'hello\n' > a; cp a b; cat b; ls

#### cp overwrites an existing file keeping its mode
printf 'new\n' > a; printf 'old old old\n' > b; chmod 600 b; cp a b; cat b; ls -l b | cut -c1-10

#### cp new file gets the source mode less umask
printf 'x\n' > a; chmod 755 a; umask 022; cp a b; ls -l b | cut -c1-10; umask 077; cp a c; ls -l c | cut -c1-10

#### cp into a directory
mkdir d; printf 'x\n' > a; printf 'y\n' > b; cp a b d; ls d; cat d/a d/b

#### cp into a directory with trailing slash
mkdir d; printf 'x\n' > a; cp a d/; ls d

#### cp several sources to a non-directory fails
printf 'x\n' > a; printf 'y\n' > b; printf 'z\n' > c; cp a b c 2>/dev/null; echo "st=$?"; cat c

#### cp a missing source fails but continues
mkdir d; printf 'x\n' > a; cp nosuch a d 2>/dev/null; echo "st=$?"; ls d

#### cp a directory without -R fails
mkdir d e; cp d e 2>/dev/null; echo "st=$?"; ls e

#### cp same file fails
printf 'x\n' > a; cp a a 2>/dev/null; echo "st=$?"; cat a

#### cp same file through a hard link fails and keeps content
printf 'x\n' > a; ln a b; cp a b 2>/dev/null; echo "st=$?"; cat a

#### cp -R copies a tree
mkdir -p s/t/u; printf '1\n' > s/f; printf '2\n' > s/t/g; printf '3\n' > s/t/u/h; cp -R s d; find d | sort; cat d/t/u/h

#### cp -R into an existing directory
mkdir -p s/t e; printf '1\n' > s/t/f; cp -R s e; find e | sort

#### cp -R merges into an existing destination tree
mkdir -p s/t d/t; printf 'new\n' > s/t/f; printf 'keep\n' > d/t/k; cp -R s/. d; find d | sort; cat d/t/f

#### cp -r is -R
mkdir -p s/t; printf '1\n' > s/t/f; cp -r s d; find d | sort

#### cp -R copies symlinks as symlinks by default
mkdir s; printf 'x\n' > s/f; ln -s f s/l; ln -s /nonexistent s/dangling; cp -R s d; ls -l d | awk '{ print $1, $NF }' | cut -c1 ; readlink d/l; readlink d/dangling

#### cp -R -L follows symlinks
mkdir s; printf 'x\n' > s/f; ln -s f s/l; cp -R -L s d; test -L d/l && echo link || echo file; cat d/l

#### cp -R -H follows operand symlinks only
mkdir s; printf 'x\n' > s/f; ln -s f s/l; ln -s s top; cp -R -H top d; test -L d && echo link || echo notlink; test -L d/l && echo inner-link

#### cp -R -P keeps an operand symlink
mkdir s; ln -s s top; cp -R -P top d; test -L d && echo link; readlink d

#### cp last of -H -L -P wins
mkdir s; printf 'x\n' > s/f; ln -s f s/l; cp -R -L -P s d; test -L d/l && echo link

#### cp without -R follows an operand symlink
printf 'x\n' > f; ln -s f l; cp l c; test -L c && echo link || echo file; cat c

#### cp -P without -R copies the symlink
printf 'x\n' > f; ln -s f l; cp -P l c; test -L c && echo link; readlink c

#### cp -R a directory into itself fails
mkdir -p s/t; cp -R s s/t 2>/dev/null; echo "st=$?"; test -d s/t && echo kept

#### cp -R onto a file fails
mkdir s; printf 'x\n' > f; cp -R s f 2>/dev/null; echo "st=$?"; cat f

#### cp a file onto a directory named as target file
mkdir -p d/a; printf 'x\n' > a; cp a d 2>/dev/null; echo "st=$?"; find d | sort

#### cp -R new directory gets the source mode less umask
mkdir s; chmod 750 s; umask 022; cp -R s d; ls -ld d | cut -c1-10

#### cp -R copies a read-only directory
mkdir -p s/t; printf 'x\n' > s/t/f; chmod 555 s/t; cp -R s d; ls -ld d/t | cut -c1-10; cat d/t/f; chmod 755 s/t d/t

#### cp -p keeps mode and times
age() { if [ "$1" -nt ref ]; then echo newer; elif [ "$1" -ot ref ]; then echo older; else echo same; fi; }; touch -t 200001020304.05 ref; printf 'x\n' > a; chmod 640 a; touch -t 200001020304.05 a; cp -p a b; ls -l b | cut -c1-10; age b

#### cp -p keeps directory times after filling it
age() { if [ "$1" -nt ref ]; then echo newer; elif [ "$1" -ot ref ]; then echo older; else echo same; fi; }; touch -t 200001020304.05 ref; mkdir -p s/t; printf 'x\n' > s/t/f; touch -t 200001020304.05 s/t s; cp -R -p s d; age d; age d/t

#### cp without -p gives a new time
age() { if [ "$1" -nt ref ]; then echo newer; elif [ "$1" -ot ref ]; then echo older; else echo same; fi; }; touch -t 200001020304.05 ref; printf 'x\n' > a; touch -t 200001020304.05 a; cp a b; age b

#### cp -p keeps special mode bits
printf 'x\n' > a; chmod 1755 a; cp -p a b; ls -l b | cut -c1-10

#### cp -i declines on no
printf 'new\n' > a; printf 'old\n' > b; echo n | cp -i a b 2>/dev/null; echo "st=$?"; cat b

#### cp -i accepts on yes
printf 'new\n' > a; printf 'old\n' > b; echo y | cp -i a b 2>/dev/null; echo "st=$?"; cat b

#### cp -i does not ask for a new file
printf 'new\n' > a; cp -i a b < /dev/null 2>/dev/null; echo "st=$?"; cat b

#### cp -R -i asks inside a tree
mkdir -p s d/s; printf 'new\n' > s/f; printf 'old\n' > d/s/f; echo n | cp -R -i s d 2>/dev/null; cat d/s/f

#### cp a FIFO without -R reads it
mkfifo p; (printf 'data\n' > p &) ; cp p out; cat out

#### cp -R recreates a FIFO
mkdir s; mkfifo s/p; cp -R s d; test -p d/p && echo fifo

#### cp /dev/null makes an empty file
printf 'old\n' > f; cp /dev/null f; wc -c < f

#### cp -- ends options
printf 'x\n' > -a; cp -- -a b; cat b

#### cp unknown option
printf 'x\n' > a; cp -j a b 2>/dev/null; echo "st=$?"; ls

#### cp missing operand
printf 'x\n' > a; cp a 2>/dev/null; echo "st=$?"

#### cp -n keeps an existing file
printf 'new\n' > a; printf 'old\n' > b; cp -n a b; echo "st=$?"; cat b

#### cp large file
awk 'BEGIN { for (i = 0; i < 200000; i++) print "line", i }' > a; cp a b; cmp a b && echo same

#### cp -R empty directory
mkdir s; cp -R s d; ls -la d | wc -l

#### cp -R of dot copies the contents
mkdir -p s/t e; printf '1\n' > s/f; cd s; cp -R . ../e; cd ..; find e | sort

#### cp -a keeps hard links together
mkdir s; printf 'x\n' > s/f; ln s/f s/g; cp -a s d; ls -i d/f d/g | awk '{ print $1 }' | uniq | wc -l
