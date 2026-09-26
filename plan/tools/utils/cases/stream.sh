#### cat files and standard input
printf 'a\n' > x; printf 'b' > y; printf 'c\n' | cat x - y; echo; echo "st=$?"

#### cat -u
printf 'a\nb\n' | cat -u

#### cat standard input twice
printf 'abc\n' | cat - -

#### cat missing file continues
printf 'a\n' > x; cat nosuch x 2>/dev/null; echo "st=$?"

#### cat a directory
mkdir d; printf 'a\n' > x; cat d x 2>/dev/null; echo "st=$?"

#### cat no operands and --
printf 'z\n' | cat; printf 'q\n' > -x; cat -- -x

#### cat binary data
printf 'a\0b\377\n' | cat | od -c | sed 's/  */ /g'

#### cat large input
awk 'BEGIN { for (i = 0; i < 100000; i++) print i }' > x; cat x x | cmp - /dev/stdin <<EOF 2>/dev/null; cat x x | wc -l
EOF

#### cat write error
printf 'a\n' | cat >&- 2>/dev/null; test $? -ne 0 && echo failed

#### cat unknown option
cat -z 2>/dev/null; echo "st=$?"

#### cksum standard vectors
printf '' | cksum; printf 'a' | cksum; printf 'abc' | cksum; printf '123456789' | cksum; printf 'message digest\n' | cksum

#### cksum files
printf 'hello\n' > x; : > e; cksum x e; echo "st=$?"

#### cksum sizes across the length boundary
for n in 255 256 257 65535 65536 65537; do awk -v n=$n 'BEGIN { for (i = 0; i < n; i++) printf "%c", 65 + i % 26 }' | cksum; done

#### cksum missing file continues
printf 'x\n' > x; cksum nosuch x 2>/dev/null; echo "st=$?"

#### cksum large file
awk 'BEGIN { for (i = 0; i < 200000; i++) print i }' > x; cksum x

#### dd copies
printf 'hello world\n' > x; dd if=x of=y 2>/dev/null; cat y; echo "st=$?"

#### dd statistics
printf 'hello world\n' > x; dd if=x of=y bs=5 2>&1 >/dev/null | head -2

#### dd bs count skip seek
awk 'BEGIN { for (i = 0; i < 20; i++) printf "%c", 65 + i }' > x; dd if=x bs=3 count=2 skip=1 2>/dev/null; echo; printf '0123456789' > y; dd if=x of=y bs=2 seek=2 count=1 conv=notrunc 2>/dev/null; cat y; echo

#### dd seek truncates without notrunc
printf '0123456789' > y; printf 'AB' | dd of=y bs=1 seek=3 2>/dev/null; cat y; echo

#### dd ibs obs
printf 'abcdefghij' | dd ibs=3 obs=4 2>&1 >/dev/null | head -2; printf 'abcdefghij' | dd ibs=3 obs=4 2>/dev/null; echo

#### dd conv=ucase lcase swab
printf 'Hello World\n' | dd conv=ucase 2>/dev/null; printf 'Hello World\n' | dd conv=lcase 2>/dev/null; printf 'abcdef' | dd conv=swab 2>/dev/null; echo

#### dd conv=sync
printf 'abc' | dd bs=8 conv=sync 2>/dev/null | od -c | sed 's/  */ /g'

#### dd conv=block and unblock
printf 'ab\ncdef\n' | dd cbs=4 conv=block 2>/dev/null | od -c | sed 's/  */ /g'; printf 'ab  cdef' | dd cbs=4 conv=unblock 2>/dev/null | od -c | sed 's/  */ /g'

#### dd truncated records
printf 'abcdefgh\nxy\n' | dd cbs=4 conv=block 2>&1 >/dev/null | grep truncated

#### dd conv=ascii and ebcdic
printf 'AB' | dd conv=ebcdic 2>/dev/null | od -An -tx1; printf '\301\302' | dd conv=ascii 2>/dev/null; echo

#### dd count=0 and skip past the end
printf 'abc' | dd count=0 2>&1 >/dev/null | head -1; printf 'abc' > x; dd if=x bs=1 skip=10 2>/dev/null | wc -c

#### dd size suffixes
awk 'BEGIN { for (i = 0; i < 3000; i++) printf "x" }' | dd bs=1k count=2 2>/dev/null | wc -c; awk 'BEGIN { for (i = 0; i < 30; i++) printf "x" }' | dd bs=2x5 2>/dev/null | wc -c; awk 'BEGIN { for (i = 0; i < 1100; i++) printf "x" }' | dd bs=1b count=1 2>/dev/null | wc -c

#### dd invalid operands
dd bs=0 < /dev/null 2>/dev/null; echo "st=$?"; dd foo=1 < /dev/null 2>/dev/null; echo "st=$?"; dd conv=nosuch < /dev/null 2>/dev/null; echo "st=$?"; dd if=nosuch 2>/dev/null; echo "st=$?"

#### dd conversion tables for every byte
awk 'BEGIN { for (i = 0; i < 256; i++) printf "%c", i }' > all; wc -c < all; dd if=all conv=ebcdic 2>/dev/null | cksum; dd if=all conv=ibm 2>/dev/null | cksum; dd if=all conv=ascii 2>/dev/null | cksum

#### dd ascii with unblock and ebcdic with block
printf 'ab\ncd\n' | dd cbs=4 conv=ebcdic 2>/dev/null | od -An -tx1; printf 'ab\ncd\n' | dd cbs=4 conv=ebcdic 2>/dev/null | dd cbs=4 conv=ascii 2>/dev/null

#### dd lcase with ebcdic
printf 'AbC' | dd conv=ebcdic,lcase 2>/dev/null | od -An -tx1

#### dd several conversions and noerror
printf 'Hello' | dd conv=ucase,swab,noerror 2>/dev/null; echo
