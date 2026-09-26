#### chgrp to a numeric group
g2=$(id -G | awk '{ print $2 }'); test -n "$g2" || g2=24; touch f; chgrp "$g2" f; echo "st=$?"; test "$(stat -c %g f)" = "$g2" && echo changed

#### chgrp to a group name
g2=$(id -G | awk '{ print $2 }'); test -n "$g2" || g2=24; touch f; chgrp "$g2" f; chgrp "$(id -gn)" f; echo "st=$?"; test "$(stat -c %g f)" = "$(id -g)" && echo back

#### chown to the own user name and ID
touch f; chown "$(id -un)" f; echo "st=$?"; chown "$(id -u)" f; echo "st=$?"; test "$(stat -c %u f)" = "$(id -u)" && echo same

#### chown user:group
g2=$(id -G | awk '{ print $2 }'); test -n "$g2" || g2=24; touch f; chown "$(id -un):$g2" f; echo "st=$?"; test "$(stat -c %g f)" = "$g2" && echo changed

#### chown :group
g2=$(id -G | awk '{ print $2 }'); test -n "$g2" || g2=24; touch f; chown ":$g2" f; echo "st=$?"; test "$(stat -c %g f)" = "$g2" && echo changed

#### chown user: gives the login group
g2=$(id -G | awk '{ print $2 }'); test -n "$g2" || g2=24; touch f; chgrp "$g2" f; chown "$(id -un):" f; echo "st=$?"; test "$(stat -c %g f)" = "$(id -g)" && echo login

#### chgrp follows a symlink operand
g2=$(id -G | awk '{ print $2 }'); test -n "$g2" || g2=24; touch f; ln -s f l; chgrp "$g2" l; test "$(stat -c %g f)" = "$g2" && echo target; test "$(stat -c %g l)" = "$(id -g)" && echo link-kept

#### chgrp -h changes the link
g2=$(id -G | awk '{ print $2 }'); test -n "$g2" || g2=24; touch f; ln -s f l; chgrp -h "$g2" l; test "$(stat -c %g l)" = "$g2" && echo link; test "$(stat -c %g f)" = "$(id -g)" && echo target-kept

#### chgrp -R changes a tree and links inside
g2=$(id -G | awk '{ print $2 }'); test -n "$g2" || g2=24; mkdir -p d/e; touch d/f d/e/g out; ln -s ../out d/l; chgrp -R "$g2" d; for p in d d/e d/f d/e/g d/l; do test "$(stat -c %g $p)" = "$g2" && echo "$p changed"; done; test "$(stat -c %g out)" = "$(id -g)" && echo out-kept

#### chgrp -R -L follows links inside
g2=$(id -G | awk '{ print $2 }'); test -n "$g2" || g2=24; mkdir d; touch out; ln -s ../out d/l; chgrp -R -L "$g2" d; test "$(stat -c %g out)" = "$g2" && echo out-changed

#### chgrp -R -H follows an operand link only
g2=$(id -G | awk '{ print $2 }'); test -n "$g2" || g2=24; mkdir real; touch real/f out; ln -s ../out real/l; ln -s real top; chgrp -R -H "$g2" top; test "$(stat -c %g real/f)" = "$g2" && echo inside-changed; test "$(stat -c %g top)" = "$(id -g)" && echo top-link-kept

#### chgrp -R -P changes an operand link itself
g2=$(id -G | awk '{ print $2 }'); test -n "$g2" || g2=24; mkdir real; touch real/f; ln -s real top; chgrp -R -P "$g2" top; test "$(stat -c %g top)" = "$g2" && echo link-changed; test "$(stat -c %g real/f)" = "$(id -g)" && echo inside-kept

#### chgrp last of -H -L -P wins
g2=$(id -G | awk '{ print $2 }'); test -n "$g2" || g2=24; mkdir d; touch out; ln -s ../out d/l; chgrp -R -L -P "$g2" d; test "$(stat -c %g out)" = "$(id -g)" && echo out-kept

#### chgrp invalid group
touch f; chgrp nosuchgroup-ws001 f 2>/dev/null; echo "st=$?"

#### chown invalid user
touch f; chown nosuchuser-ws001 f 2>/dev/null; echo "st=$?"; chown "$(id -un):nosuchgroup-ws001" f 2>/dev/null; echo "st=$?"

#### chgrp missing file continues
g2=$(id -G | awk '{ print $2 }'); test -n "$g2" || g2=24; touch f; chgrp "$g2" nosuch f 2>/dev/null; echo "st=$?"; test "$(stat -c %g f)" = "$g2" && echo changed

#### chown missing operand
chown "$(id -un)" 2>/dev/null; echo "st=$?"; chgrp 0 2>/dev/null; echo "st=$?"

#### chown unknown option
touch f; chown -x "$(id -un)" f 2>/dev/null; echo "st=$?"
