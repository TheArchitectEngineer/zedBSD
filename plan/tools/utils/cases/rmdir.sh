#### rmdir removes empty directories
mkdir a b; rmdir a b; echo "st=$?"; ls

#### rmdir non-empty fails but continues
mkdir -p a/x b; rmdir a b 2>/dev/null; echo "st=$?"; ls

#### rmdir a file fails
touch f; rmdir f 2>/dev/null; echo "st=$?"; ls

#### rmdir missing fails
rmdir nosuch 2>/dev/null; echo "st=$?"

#### rmdir -p removes the prefixes
mkdir -p a/b/c; rmdir -p a/b/c; echo "st=$?"; ls

#### rmdir -p stops at a non-empty parent
mkdir -p a/b/c a/x; rmdir -p a/b/c 2>/dev/null; echo "st=$?"; find a | sort

#### rmdir -p with slashes
mkdir -p a/b/c; rmdir -p a//b///c/; echo "st=$?"; ls

#### rmdir trailing slash
mkdir d; rmdir d/; echo "st=$?"; ls

#### rmdir -p of an absolute path stops at a parent
mkdir -p a/b; cd a; rmdir -p "$PWD/b" 2>/dev/null; echo "st=$?"; ls

#### rmdir no operand
rmdir 2>/dev/null; echo "st=$?"
