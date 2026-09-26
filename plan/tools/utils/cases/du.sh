#### du default reports directories after their contents
mkdir -p d/a/b d/c; printf 'x%.0s' $(seq 1 3000) > d/a/f; printf 'y' > d/c/g; du d | sort -k2; echo "st=$?"

#### du -a writes every file
mkdir -p d/a; printf 'x%.0s' $(seq 1 5000) > d/a/f; : > d/e; du -a d | sort -k2

#### du -s writes the operands only
mkdir -p d/a/b; printf 'x%.0s' $(seq 1 5000) > d/a/b/f; du -s d d/a | sort -k2

#### du -k rounds up
mkdir d; printf 'x%.0s' $(seq 1 5000) > d/f; du -k d; du -sk d; du d | awk '{ print ($1 + 1) / 2 >= 1 }'

#### du file operands and the default
mkdir d; printf 'abc' > d/f; du d/f; cd d; du; du -a | sort -k2

#### du counts hard links once
mkdir d; printf 'x%.0s' $(seq 1 9000) > d/f; ln d/f d/g; du -a d | sort -k2 | wc -l; du -s d; mkdir e; cp d/f e/f; du -s e

#### du counts a file once across operands
mkdir d; printf 'x%.0s' $(seq 1 9000) > d/f; ln d/f h; du d h | wc -l; du d d | wc -l

#### du does not follow symbolic links by default
mkdir -p d r; printf 'x%.0s' $(seq 1 9000) > r/f; ln -s ../r d/l; du -s d r | awk '{ print $2 }'; test "$(du -s d | cut -f1)" -lt "$(du -s r | cut -f1)" && echo smaller

#### du -L follows symbolic links
mkdir -p d r; printf 'x%.0s' $(seq 1 9000) > r/f; ln -s ../r d/l; du -L -a d | sort -k2

#### du -H follows operand links only
mkdir -p r/s; printf 'x%.0s' $(seq 1 9000) > r/f; ln -s r top; ln -s ../r r/s/back; du -H top | sort -k2; du top

#### du trailing slash
mkdir -p d/e; : > d/e/f; du -a d/ | sort -k2

#### du missing operand
mkdir d; du nothere d > out 2> err; echo "st=$?"; cut -f2 out; test -s err && echo diagnosed

#### du unreadable directory
mkdir -p d/s; : > d/s/f; chmod 0 d/s; du d > out 2> /dev/null; echo "st=$?"; cut -f2 out | sort; chmod 755 d/s
## skip-status

#### du -a and -s together
mkdir d; du -a -s d > /dev/null 2>&1; echo "st=$?"

#### du -x on one file system
mkdir -p d/e; : > d/e/f; du -x -a d | sort -k2
