#### csplit regex at the first line
printf 'a\nb\nc\na\nd\n' > f; csplit f /a/; ls; cat xx01

#### csplit two regexes
printf 'a\nb\nc\na\nd\n' > f; csplit f /a/ /a/; ls; cat xx01

#### csplit line number
printf 'a\nb\nc\na\nd\n' > f; csplit f 3; ls; cat xx00

#### csplit line number 1
printf 'a\nb\nc\na\nd\n' > f; csplit f 1; echo "st=$?"; ls

#### csplit decreasing line numbers
printf 'a\nb\nc\na\nd\n' > f; csplit f 3 2 2>/dev/null; echo "st=$?"; ls

#### csplit offsets
printf 'a\nb\nc\na\nd\n' > f; csplit f /c/+1; cat xx00; rm xx*; csplit f /c/-1; cat xx00

#### csplit skip
printf 'a\nb\nc\na\nd\n' > f; csplit f %c%; ls; cat xx00

#### csplit match not found removes the pieces
printf 'a\nb\nc\na\nd\n' > f; csplit f /zz/ 2>/dev/null; echo "st=$?"; ls

#### csplit -k keeps the pieces
printf 'a\nb\nc\na\nd\n' > f; csplit -k f /b/ /zz/ 2>/dev/null; echo "st=$?"; ls; cat xx01

#### csplit repetition
printf '1\nx\n2\nx\n3\nx\n4\n' > g; csplit g /x/ '{2}'; ls; cat xx03

#### csplit repetition past the matches
printf '1\nx\n2\nx\n3\nx\n4\n' > g; csplit g /x/ '{5}' 2>/dev/null; echo "st=$?"; ls

#### csplit repeated line number
printf '1\nx\n2\nx\n3\nx\n4\n' > g; csplit g 2 '{2}'; echo "st=$?"; ls

#### csplit -f -n -s
printf '1\nx\n2\nx\n3\nx\n4\n' > g; csplit -s -f p -n 3 g 3; ls

#### csplit line number out of range
printf '1\nx\n2\nx\n3\nx\n4\n' > g; csplit g 10 2>/dev/null; echo "st=$?"; ls

#### csplit standard input
printf 'a\nb\n' | csplit - 2; ls; cat xx01

#### csplit regex offset then line number
printf '1\nx\n2\nx\n3\nx\n4\n' > g; csplit g '/x/1' 5; echo "st=$?"; ls

#### csplit line number then regex
printf 'a\nb\nc\nc\nd\n' > f; csplit f 3 /c/; echo "st=$?"; ls; cat xx01

#### csplit skip then regex
printf 'a\nb\nc\nd\nc\ne\n' > f; csplit f %b% /c/; ls; cat xx00

#### csplit escaped delimiter
printf 'a\nx/y\nb\n' > f; csplit f '/x\/y/'; cat xx01

#### csplit invalid args
printf 'a\n' > f; csplit f /a 2>/dev/null; echo "st=$?"; csplit f '{2}' 2>/dev/null; echo "st=$?"; csplit f 0 2>/dev/null; echo "st=$?"; csplit f /a/x 2>/dev/null; echo "st=$?"; ls

#### csplit missing file
csplit nosuch 1 2>/dev/null; echo "st=$?"
