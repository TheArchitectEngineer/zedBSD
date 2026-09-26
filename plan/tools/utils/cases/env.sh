#### env -i writes nothing
env -i

#### env -i with assignments
env -i A=1 B=2

#### env - is -i
env - A=1

#### env replaces an inherited variable
FOO=old env FOO=new sh -c 'echo $FOO'

#### env adds a variable
env NEWVAR=x sh -c 'echo $NEWVAR'

#### env later assignment wins
env -i A=1 A=2

#### env value with equals
env -i 'A=b=c'

#### env empty value
env -i A= sh -c 'echo "[${A-unset}]"'

#### env -i clears the environment of the utility
FOO=x env -i /bin/sh -c 'echo "[${FOO-unset}]"'

#### env -- ends the options
env -i -- A=1

#### env utility status
env sh -c 'exit 7'; echo "st=$?"

#### env utility not found
env nosuchcmd 2>/dev/null; echo "st=$?"

#### env utility not executable
printf 'x\n' > f; chmod 644 f; env ./f 2>/dev/null; echo "st=$?"

#### env runs a script without #!
printf 'echo script "$@"\n' > s; chmod 755 s; env ./s a

#### env uses the new PATH
mkdir d; printf '#!/bin/sh\necho found-in-d\n' > d/tool; chmod 755 d/tool; env PATH="$PWD/d:/usr/bin:/bin" tool

#### env utility arguments are not assignments
env -i echo A=1

#### env inherited environment is printed
env -i X=1 env | grep -c '^X=1$'

#### env write error to a closed stdout
env -i A=1 >&-; test $? -ne 0 && echo failed
