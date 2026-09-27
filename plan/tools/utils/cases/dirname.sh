#### dirname one string
dirname /usr/lib/libz.so; dirname libz.so; dirname /; dirname //; dirname ''; dirname a//b///; dirname /a; dirname .; dirname ..

#### dirname several strings
dirname a/b / c /usr//bin ''; echo "st=$?"

#### dirname -z and --zero
dirname -z a/b c | od -c; dirname --zero x/y | od -c

#### dirname -- and a string that starts with -
dirname -- -a/b -c; echo "st=$?"

#### dirname without a string
dirname > /dev/null 2>&1; echo "st=$?"
