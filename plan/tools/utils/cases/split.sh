#### split default 1000 lines
awk 'BEGIN { for (i = 0; i < 2500; i++) print i }' > f; split f; ls; wc -l < xaa; wc -l < xac; cat xa? | cmp - f && echo same

#### split -l
printf '1\n2\n3\n4\n5\n' > s; split -l 2 s; ls; cat xab

#### split -b
printf '1\n2\n3\n4\n5\n' > s; split -b 3 s; ls; od -c xab; cat x?? | cmp - s && echo same

#### split -b k and m
awk 'BEGIN { for (i = 0; i < 3000; i++) print "line" }' > f; split -b 2k f; ls; wc -c < xaa; split -b 1m f p; ls p*

#### split -a and a name
printf '1\n2\n3\n4\n5\n' > s; split -l 1 -a 1 s pre; ls pre*; split -l 2 -a 3 s q; ls q*

#### split suffixes exhausted
awk 'BEGIN { for (i = 0; i < 30; i++) print i }' | split -l 1 -a 1; echo "st=$?"; ls x* | wc -l; cat xz

#### split empty input makes no file
: > e; split e; ls

#### split standard input
printf 'a\nb\nc\n' | split -l 2 - part; ls; cat partab

#### split last line without newline
printf 'a\nb\nc' | split -l 2; od -c xab

#### split long lines by bytes
awk 'BEGIN { for (i = 0; i < 10; i++) printf "%0100d\n", i }' > f; split -b 250 f; ls | wc -l; cat x?? | cmp - f && echo same

#### split overwrites an existing piece
printf 'old old old\n' > xaa; printf 'new\n' | split; cat xaa

#### split invalid counts
printf 'a\n' | split -l 0 2>/dev/null; echo "st=$?"; printf 'a\n' | split -b x 2>/dev/null; echo "st=$?"; printf 'a\n' | split -l 1x 2>/dev/null; echo "st=$?"

#### split missing file
split nosuch 2>/dev/null; echo "st=$?"
