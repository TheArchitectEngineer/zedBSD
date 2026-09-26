#### time -p format
time -p true 2>&1 | sed 's/[0-9][0-9]*\.[0-9][0-9]*/N/'

#### time -p passes the utility status
time -p sh -c 'exit 3' 2>/dev/null; echo "st=$?"

#### time -p keeps the utility output on stdout
time -p sh -c 'echo out; echo err >&2' 2>/dev/null

#### time -p utility killed by a signal
time -p sh -c 'kill -9 $$' 2>/dev/null; echo "st=$?"

#### time -p utility not found
time -p nosuchcmd 2>/dev/null; echo "st=$?"

#### time -p utility not executable
printf 'x\n' > f; chmod 644 f; time -p ./f 2>/dev/null; echo "st=$?"

#### time -p measures sleeping
time -p sleep 1 2>&1 | awk '$1 == "real" { print ($2 >= 0.9 && $2 < 3) }'

#### time -p user time of a busy utility
time -p sh -c 'i=0; while [ $i -lt 300000 ]; do i=$((i + 1)); done' 2>&1 | awk '$1 == "user" || $1 == "sys" { t += $2 } END { print (t > 0.05) }'

#### time -p stops at the utility operand
time -p echo -p 2>/dev/null

#### time -p runs a script without #!
printf 'echo script "$@"\n' > s; chmod 755 s; time -p ./s a 2>/dev/null
