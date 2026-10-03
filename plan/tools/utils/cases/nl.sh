#### nl default numbers non-empty body lines
printf 'a\n\nb\n' | nl | od -c

#### nl -ba
printf 'a\n\nb\n' | nl -ba

#### nl -ba -l
printf 'a\n\n\n\nb\n\n\n' | nl -ba -l 2

#### nl -bn
printf 'a\nb\n' | nl -bn

#### nl -b pattern
printf 'apple\nbanana\ncherry\n' | nl -b 'pa.a'; printf 'x1\ny\nx2\n' | nl -b 'p^x'

#### nl sections
printf 'h\n\\:\\:\\:\nh1\n\\:\\:\nb1\nb2\n\\:\nf1\n\\:\\:\\:\nh2\n\\:\\:\nb3\n' | nl -ha -fa

#### nl sections default types
printf '\\:\\:\\:\nh1\n\\:\\:\nb1\n\\:\nf1\n' | nl

#### nl -p keeps counting
printf 'a\n\\:\\:\nb\n\\:\\:\\:\n\\:\\:\nc\n' | nl -p

#### nl -n -w -s -v -i
printf 'x\ny\n' | nl -n ln -w 3 -s '|' -v 10 -i 5; printf 'x\ny\n' | nl -n rz -w 3; printf 'x\n' | nl -n rn -w 1 -s ': '

#### nl -v 0 and negative
printf 'x\ny\n' | nl -v 0; printf 'x\ny\n' | nl -v -1

#### nl -d
printf '@@\nx\n' | nl -d @; printf '@:\nx\n' | nl -d @; printf '##\n#1\ny\n' | nl -d '#1'

#### nl a file operand
printf 'a\nb\n' > f; nl f; nl - < f

#### nl no final newline
printf 'a\nb' | nl | od -c

#### nl invalid options
printf 'x\n' | nl -b z 2>/dev/null; echo "st=$?"; printf 'x\n' | nl -n xx 2>/dev/null; echo "st=$?"; printf 'x\n' | nl -w 0 2>/dev/null; echo "st=$?"; printf 'x\n' | nl -i 0 2>/dev/null; echo "st=$?"

#### nl missing file
nl nosuch 2>/dev/null; echo "st=$?"
