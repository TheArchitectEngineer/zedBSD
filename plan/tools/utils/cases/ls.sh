#### ls default order and -1
mkdir d; touch d/b d/a d/C d/_x; ls d; ls -1 d; echo "st=$?"

#### ls -a and -A
mkdir d; touch d/.h d/v; ls -a d; echo --; ls -A d; echo --; ls -aA d; echo --; ls -Aa d

#### ls -f keeps the directory's order with the dot names
mkdir d; touch d/.h d/v d/w; ls -f d | sort

#### ls -d and directory operands
mkdir -p d/e; touch d/f; ls -d d d/e; ls d/e d/f; echo "st=$?"

#### ls -F and -p marks
mkdir -p d/sub; touch d/f d/x; chmod +x d/x; ln -s f d/l; mkfifo d/p; ls -F d; echo --; ls -p d; echo --; ls -pF d

#### ls -r reverses
mkdir d; touch d/a d/b d/c; ls -r d

#### ls -t newest first and -tr
mkdir d; touch -d '2020-01-01 00:00' d/old; touch -d '2021-01-01 00:00' d/new; touch -d '2020-06-01 00:00' d/mid; ls -t d; ls -tr d

#### ls -S largest first and -Sr
mkdir d; printf 'abc' > d/three; printf 'a' > d/one; printf 'abcdefgh' > d/eight; : > d/zero; ls -S d; ls -Sr d

#### ls -t and -S, the later wins
mkdir d; printf 'abcdefgh' > d/big; touch -d '2020-01-01' d/big; printf 'a' > d/small; touch -d '2021-01-01' d/small; ls -tS d; ls -St d

#### ls -u sorts by access time, -lu shows it
mkdir d; : > d/a; : > d/b; touch -m -d '2021-01-01 00:00' d/a d/b; touch -a -d '2019-01-01 00:00' d/a; touch -a -d '2020-01-01 00:00' d/b; ls -u d; ls -lu d | awk 'NR > 1 { print $6, $7, $8, $9 }'; ls -lut d | awk 'NR > 1 { print $9 }'

#### ls -c sorts by status change time
mkdir d; : > d/a; : > d/b; sleep 1; chmod 600 d/a; ls -c d; ls -ct d; ls -lc d | awk 'NR > 1 { print $9 }'

#### ls -l columns and the old year
mkdir d; printf 'hello' > d/f; touch -d '2000-01-02 03:04' d/f; mkdir d/sub; touch -d '2001-05-06 07:08' d/sub; ls -l d | sed 's/ [^ ]* [^ ]* / OWNER GROUP /' | sed 's/^total .*/total/'

#### ls -g and -o leave out the owner or the group
mkdir d; printf 'hello' > d/f; touch -d '2000-01-02 03:04' d/f; ls -g d | awk 'NR > 1 { print NF, $1, $4, $5, $6, $7, $8 }'; ls -o d | awk 'NR > 1 { print NF, $1, $4, $5, $6, $7, $8 }'; ls -go d | awk 'NR > 1 { print NF, $1, $3, $4, $5, $6, $7 }'

#### ls -n writes the numbers
mkdir d; : > d/f; touch -d '2000-01-02 03:04' d/f; ls -n d | awk 'NR > 1 { print ($3 ~ /^[0-9]+$/), ($4 ~ /^[0-9]+$/), $5, $9 }'

#### ls -s and -k, blocks and the total
mkdir d; printf 'x%.0s' $(seq 1 5000) > d/big; : > d/empty; ls -s d; ls -sk d; ls -ls d | awk '{ print $1 }'; ls -lk d | head -1

#### ls -l total in 512-byte blocks
mkdir d; printf 'x%.0s' $(seq 1 5000) > d/big; ls -l d | head -1; ls -ln d | head -1

#### ls -i serial numbers
mkdir d; : > d/a; ls -i d | awk '{ print ($1 ~ /^[0-9]+$/), $2 }'

#### ls -R recursion and headers
mkdir -p d/a/b d/c; touch d/a/f d/a/b/g d/c/h; ls -R d

#### ls symbolic links: default, -L, -H
mkdir -p r/s; touch r/f; ln -s r link; ln -s nowhere dangle; ls link; ls -d link; ls -L -d link; ls -H link; ls -l link | awk '{ print $9, $10, $11 }'; ls -lH link | head -1 | cut -c1-5

#### ls -L inside directories
mkdir -p d r; ln -s ../r d/l; ls -lL d | awk 'NR > 1 { print substr($1, 1, 1), $9 }'; ls -l d | awk 'NR > 1 { print substr($1, 1, 1), $9 }'

#### ls -m and -x
mkdir d; touch d/a d/b d/c; ls -m d; ls -x d; ls -C d

#### ls missing operand and status
mkdir d; touch d/a; ls d nothere > out 2> err; echo "st=$?"; cat out; test -s err && echo diagnosed
## skip-status

#### ls operand order: files first, then directories
mkdir -p d e; touch f g d/x e/y; ls e f d g

#### ls -1 with -s in a pipe
mkdir d; printf 'abc' > d/f; ls -s1 d | sed -n '2p' | awk '{ print $2 }'
