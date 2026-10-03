#### mktemp default file in TMPDIR
mkdir t; TMPDIR=$PWD/t; export TMPDIR; f=$(mktemp); echo "st=$?"; test -f "$f" && echo file; ls -l "$f" | cut -c1-10; case $f in "$PWD"/t/tmp.??????????) echo name;; esac; test -s "$f" || echo empty

#### mktemp -d
mkdir t; f=$(TMPDIR=$PWD/t mktemp -d); test -d "$f" && echo directory; ls -ld "$f" | cut -c1-10; ls "$f" | wc -l

#### mktemp with a template
f=$(mktemp fooXXXX); case $f in foo????) echo name;; esac; test -f "$f" && echo file; f=$(mktemp d/xXXX 2>/dev/null); echo "st=$?"

#### mktemp templates with a suffix
f=$(mktemp aXXXXX.txt); case $f in a?????.txt) echo implied;; esac; f=$(mktemp --suffix=.c bXXX); case $f in b???.c) echo suffix;; esac; mktemp --suffix=/x cXXX 2>/dev/null; echo "st=$?"

#### mktemp too few X's
mktemp fooXX 2> err; echo "st=$?"; test -s err && echo diagnosed; mktemp -q fooXX 2> err; echo "st=$?"; test -s err || echo quiet

#### mktemp -p and --tmpdir
mkdir p q; f=$(mktemp -p p); case $f in p/tmp.??????????) echo p;; esac; f=$(mktemp -p q zXXX); case $f in q/z???) echo template;; esac; f=$(TMPDIR=$PWD/q mktemp --tmpdir wXXX); case $f in "$PWD"/q/w???) echo tmpdir;; esac; f=$(mktemp --tmpdir=p vXXX); case $f in p/v???) echo tmpdir-value;; esac; mktemp -p p a/bXXX 2>/dev/null; echo "st=$?"

#### mktemp -t
mkdir p q; f=$(TMPDIR=$PWD/q mktemp -t -p p xXXX); case $f in "$PWD"/q/x???) echo tmpdir-wins;; esac; f=$(env -u TMPDIR mktemp -t -p p yXXX); case $f in p/y???) echo p;; esac

#### mktemp -u makes nothing
f=$(mktemp -u gXXXXX); echo "st=$?"; case $f in g?????) echo name;; esac; test -e "$f" || echo absent

#### mktemp in a missing directory
mktemp nothere/aXXX 2> err; echo "st=$?"; test -s err && echo diagnosed; mktemp -d nothere/aXXX 2> /dev/null; echo "st=$?"

#### mktemp names differ
a=$(mktemp -u hXXXXXXXX); b=$(mktemp -u hXXXXXXXX); test "$a" != "$b" && echo differ

#### mktemp two templates
mktemp aXXX bXXX > /dev/null 2>&1; echo "st=$?"
