#### date default format shape
date | awk '{ print NF, length($1), length($2), $5 }'; date | grep -c '^[A-Z][a-z][a-z] [A-Z][a-z][a-z] [ 0-9][0-9] [0-9][0-9]:[0-9][0-9]:[0-9][0-9] UTC [0-9][0-9][0-9][0-9]$'

#### date format of a fixed time zone offset
TZ=JST-9 date '+%Z %z' ; TZ=EST5 date '+%Z %z'; TZ=UTC0 date '+%Z %z'

#### date -u
TZ=JST-9 date -u '+%Z %z'; TZ=JST-9 date -u | awk '{ print $5 }'

#### date year is current
date -u '+%Y' | awk '{ print ($1 >= 2024) }'

#### date conversions
date -u '+%a%A%b%B%h' | awk '{ print length($0) > 10 }'; date -u '+%C%y' | awk '{ print length($0) }'; date -u '+[%d][%e][%H][%I][%j][%m][%M][%S][%u][%w]' | sed 's/[0-9]/N/g'

#### date literal text, percent, newline and tab
date '+a%%b%nc%td' | od -c | sed 's/  */ /g'

#### date composite conversions
date -u '+%D|%T|%R|%F' | sed 's/[0-9]/N/g'; date -u '+%r' | sed 's/[0-9]/N/g'

#### date week numbers
date -u '+%U %V %W %g %G' | sed 's/[0-9]/N/g'

#### date E and O modifiers
date -u '+%Ey %EY %Od %Oe %OH %OM %OS' | sed 's/[0-9]/N/g'

#### date empty format
date '+' | od -c

#### date invalid operand
date 99999999 2>/dev/null; echo "st=$?"; date foo 2>/dev/null; echo "st=$?"; date -x 2>/dev/null; echo "st=$?"

#### date setting without privilege fails
test "$(id -u)" -eq 0 && echo "st=1" || { date 010203042000 > /dev/null 2>&1; echo "st=$?"; }
