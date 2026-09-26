#### id -u is the owner of a new file
touch f; test "$(id -u)" = "$(stat -c %u f)" && echo same

#### id -g is the group of a new file
touch f; test "$(id -g)" = "$(stat -c %g f)" && echo same

#### id -un is the name of the user ID
u=$(id -u); test "$(id -un)" = "$(awk -F: -v u="$u" '$3 == u { print $1; exit }' /etc/passwd)" && echo same

#### id -gn is the name of the group ID
g=$(id -g); test "$(id -gn)" = "$(awk -F: -v g="$g" '$3 == g { print $1; exit }' /etc/group)" && echo same

#### id -ur and -gr are the real IDs
test "$(id -ur)" = "$(id -u)" && test "$(id -gr)" = "$(id -g)" && echo same

#### id default format
u=$(id -u); n=$(id -un); g=$(id -g); gn=$(id -gn); id | grep -q "^uid=$u($n) gid=$g($gn) groups=$g($gn)" && echo ok

#### id -G starts with the group
test "$(id -G | awk '{ print $1 }')" = "$(id -g)" && echo same

#### id -Gn has a name for each group
test "$(id -G | wc -w)" = "$(id -Gn | wc -w)" && echo same; test "$(id -Gn | awk '{ print $1 }')" = "$(id -gn)" && echo same

#### id -G has no duplicates
id -G | tr ' ' '\n' | sort | uniq -d | wc -l

#### id of root
r=$(awk -F: '$3 == 0 { print $1; exit }' /etc/group); test "$(id root | cut -d' ' -f1-2)" = "uid=0(root) gid=0($r)" && echo same; id -u root; id -g root; id -un root; id -G root

#### id of a numeric user
id -un 0

#### id of a missing user
id nosuchuser-ws001 2>/dev/null; echo "st=$?"

#### id conflicting options
id -u -g 2>/dev/null; echo "st=$?"; id -G -u 2>/dev/null; echo "st=$?"

#### id -n and -r need a field
id -n 2>/dev/null; echo "st=$?"; id -r 2>/dev/null; echo "st=$?"

#### id two users
id root root 2>/dev/null | uniq -c | awk '{ print $1 }'; id -u root root

#### id unknown option
id -x 2>/dev/null; echo "st=$?"
