#### base64 encodes with padding
printf '' | base64; printf 'a' | base64; printf 'ab' | base64; printf 'abc' | base64; printf 'hello, world\n' | base64

#### base64 wraps at 76
awk 'BEGIN { for (i = 0; i < 200; i++) printf "%c", 32 + i % 90 }' | base64; awk 'BEGIN { for (i = 0; i < 57; i++) printf "x" }' | base64

#### base64 -w
awk 'BEGIN { for (i = 0; i < 40; i++) printf "y" }' > f; base64 -w 10 f; base64 -w 0 f; echo; base64 --wrap=20 f; base64 -w0 < f | od -c | tail -2

#### base64 -w with a bad size
base64 -w x /dev/null > /dev/null 2>&1; echo "st=$?"

#### base64 -d
printf 'aGVsbG8sIHdvcmxkCg==\n' | base64 -d; printf 'YQ==' | base64 -d | od -c; printf 'YWI=' | base64 --decode | od -c; printf 'YW\nJj\n' | base64 -d | od -c

#### base64 round trip of every byte
awk 'BEGIN { for (i = 0; i < 256; i++) printf "%c", i }' > f 2>/dev/null; od -An -tx1 f | wc -w; base64 f > e; base64 -d e > g; cmp f g && echo same

#### base64 -d rejects garbage
printf 'aG!k' | base64 -d > /dev/null 2>&1; echo "st=$?"; printf 'aGk' | base64 -d | od -c; echo "st=$?"

#### base64 -d -i skips garbage
printf 'a!G*k=\n' | base64 -d -i | od -c; printf 'Y W I =' | base64 -di | od -c

#### base64 concatenated padded groups
printf 'YQ==YQ==' | base64 -d 2>/dev/null | od -c; echo "st=$?"

#### base64 file operand and -
printf 'xyz' > f; base64 f; base64 - < f; base64 nothere > /dev/null 2>&1; echo "st=$?"; base64 f f > /dev/null 2>&1; echo "st=$?"

#### base64 -d status of a short group
printf 'aGk' | base64 -d > /dev/null 2>&1; echo "st=$?"; printf 'a' | base64 -d > /dev/null 2>&1; echo "st=$?"
