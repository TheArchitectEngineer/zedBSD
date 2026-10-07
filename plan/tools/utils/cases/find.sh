#### find default print and a starting point
mkdir -p d/a/b; touch d/f d/a/g d/a/b/h; find d | sort; echo "st=$?"

#### find with no starting point
mkdir -p a; touch a/x; find | sort

#### find -name and patterns
mkdir d; touch d/a.c d/b.h d/.c d/'x[1]'; find d -name '*.c' | sort; find d -name '[ab].?' | sort; find d -name 'x\[1\]'

#### find -path
mkdir -p d/s/t; touch d/s/t/f d/f; find d -path 'd/s/*' | sort; find d -path '*t/f'

#### find -type each letter
mkdir -p d/s; touch d/f; ln -s f d/l; mkfifo d/p; for t in f d l p; do echo "$t:"; find d -type $t | sort; done

#### find -prune
mkdir -p d/skip/x d/keep/y; find d -name skip -prune -o -print | sort

#### find -prune with -depth has no effect on the walk
mkdir -p d/skip/x; find d -depth -name skip -prune -o -print

#### find -depth order
mkdir -p d/a/b; touch d/a/b/f; find d -depth

#### find operators: !, -a, -o and precedence
mkdir d; touch d/a d/b d/c; find d -type f ! -name a | sort; find d -name a -o -name b | sort; find d -name a -o -name b -a -name c | sort; find d \( -name a -o -name b \) -a -type f | sort

#### find juxtaposition is -a
mkdir d; touch d/a d/ab; find d -name 'a*' -name '*b'

#### find -o without -print prints only the matching side
mkdir d; touch d/a d/b; find d -name a -print -o -name b | sort; echo --; find d \( -name a -o -name b \) -print | sort

#### find ! ! and nested parentheses
mkdir d; touch d/a d/b; find d ! ! -name a; find d \( \( -name a \) \) -print

#### find -perm exact, -perm -mode and symbolic
mkdir d; touch d/a d/b d/c; chmod 644 d/a; chmod 755 d/b; chmod 600 d/c; find d -type f -perm 644; find d -type f -perm -644 | sort; find d -type f -perm -u+x; find d -type f -perm u=rw | sort; find d -type f -perm -g=r | sort

#### find -links
mkdir d; touch d/a d/b; ln d/a d/c; find d -type f -links 2 | sort; find d -type f -links -2; find d -type f -links +1 | sort

#### find -size in blocks and characters
mkdir d; printf 'x%.0s' $(seq 1 600) > d/big; printf 'abc' > d/small; : > d/empty; find d -type f -size 1 | sort; find d -type f -size +1; find d -type f -size 2; find d -type f -size 3c; find d -type f -size -4c | sort; find d -type f -size 0 

#### find -mtime, -atime, -ctime
mkdir d; touch d/new; touch -d '2000-01-01' d/old; find d -type f -mtime +100; find d -type f -mtime -1; find d -type f -mtime 0; find d -type f -atime +100; find d -type f -ctime -1 | sort

#### find -newer
mkdir d; touch -d '2000-01-01' d/old d/ref; touch -d '2001-01-01' d/ref; touch d/new; find d -type f -newer d/ref

#### find -user, -group, -nouser, -nogroup
mkdir d; touch d/a; u=$(id -un); g=$(id -gn); find d -type f -user "$u"; find d -type f -group "$g"; find d -type f -user "$(id -u)"; find d -type f -group "$(id -g)"; find d -nouser; find d -nogroup; echo "st=$?"

#### find -exec with ;
mkdir d; touch d/a d/b; find d -type f -exec echo got {} \; | sort; find d -type f -exec test {} = d/a \; -print

#### find -exec with {} inside an argument is not replaced in POSIX
mkdir d; touch d/a; find d -type f -exec echo x{}y \;

#### find -exec with +
mkdir d; touch d/a d/b d/c; find d -type f -exec echo {} + | tr ' ' '\n' | sort; find d -type f -exec sh -c 'echo $#' sh {} +

#### find -exec + is true and the status reflects failure
mkdir d; touch d/a; find d -type f -exec false {} + -print; echo "st=$?"; find d -type f -exec true {} + ; echo "st=$?"

#### find -exec ; failure is false, not an error
mkdir d; touch d/a; find d -type f -exec false \; -o -print; echo "st=$?"

#### find -ok answered from standard input
mkdir d; touch d/a d/b; printf 'y\nn\n' | find d -type f -ok echo yes {} \; 2>/dev/null | wc -l

#### find -print and an action disable the implicit print
mkdir d; touch d/a; find d -name a -exec echo E {} \;; find d -name a -print -print

#### find -xdev stays on the device
mkdir -p d/s; touch d/s/f; find d -xdev | sort

#### find -L follows links
mkdir -p d r/s; touch r/s/f; ln -s ../r d/l; find d | sort; echo --; find -L d | sort; echo --; find -L d -type d | sort

#### find -H follows operands only
mkdir -p r/s; touch r/s/f; ln -s r l; find l; echo --; find -H l | sort; echo --; find -H l -type l; find l -type l

#### find -L detects a loop
mkdir -p d/s; ln -s .. d/s/up; find -L d > out 2> err; echo "st=$?"; sort out; test -s err && echo diagnosed
## skip-status

#### find -L -type l finds dangling links only
mkdir d; touch d/f; ln -s f d/ok; ln -s nowhere d/bad; find -L d -type l

#### find missing starting point
mkdir d; find d nothere > out 2> err; echo "st=$?"; cat out; test -s err && echo diagnosed

#### find bad primary
find . -bogus > out 2> err; test -s err && echo diagnosed
## skip-status

#### find several starting points in order
mkdir a b; touch a/x b/y; find b a

#### find path forms with a trailing slash
mkdir -p d/s; find d/ | sort; find ./d -name s

#### find -exec: a + not after {} is an argument
mkdir d; touch d/a; find d -type f -exec echo + {} \;

#### find -exec + keeps the arguments before {}
mkdir d; touch d/a d/b; find d -type f -exec echo pre {} + | tr ' ' '\n' | sort

#### find -exec + flushes before the end and keeps stdout in order
mkdir d; touch d/a; find d -type f -print -exec echo E {} +

#### find -ok takes only ;
mkdir d; touch d/a; find d -ok echo {} + > out 2> err; test -s out || echo none; test -s err && echo diagnosed
## skip-status
