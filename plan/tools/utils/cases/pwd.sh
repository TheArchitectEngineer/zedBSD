#### pwd -P of a plain directory
mkdir d; cd d; /usr/bin/env pwd -P | sed "s|$(/bin/pwd -P)|X|"

#### pwd -L follows a symlink kept in PWD
mkdir real; ln -s real link; cd link; export PWD; env pwd -L | sed "s|^$(cd .. && /bin/pwd -P)|X|"

#### pwd -P resolves the symlink
mkdir real; ln -s real link; cd link; export PWD; env pwd -P | sed "s|^$(cd .. && /bin/pwd -P)|X|"

#### pwd default is -L
mkdir real; ln -s real link; cd link; export PWD; env pwd | sed "s|^$(cd .. && /bin/pwd -P)|X|"

#### pwd last option wins
mkdir real; ln -s real link; cd link; export PWD; env pwd -P -L | sed "s|^$(cd .. && /bin/pwd -P)|X|"; env pwd -L -P | sed "s|^$(cd .. && /bin/pwd -P)|X|"

#### pwd -L ignores a PWD with dot components
mkdir real; ln -s real link; cd link; PWD="$PWD/." env pwd -L | sed "s|^$(cd .. && /bin/pwd -P)|X|"

#### pwd -L ignores a PWD naming another directory
mkdir a b; cd a; PWD="$(cd ../b && /bin/pwd -P)" env pwd -L | sed "s|^$(cd .. && /bin/pwd -P)|X|"

#### pwd -L ignores a relative PWD
mkdir a; cd a; PWD=a env pwd -L | sed "s|^$(cd .. && /bin/pwd -P)|X|"

#### pwd ignores an operand
cd /; env pwd x 2>/dev/null; echo "st=$?"

#### pwd rejects an unknown option
env pwd -x 2>/dev/null; echo "st=$?"

#### pwd write error
env pwd >&-; test $? -ne 0 && echo failed
