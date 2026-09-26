#### nohup runs the utility with its output on a pipe
nohup echo a b | cat; ls

#### nohup passes the utility status
nohup sh -c 'exit 5'; echo "st=$?"

#### nohup ignores SIGHUP in the utility
nohup sh -c 'kill -HUP $$; echo survived'; echo "st=$?"

#### nohup without a utility
nohup 2>/dev/null; echo "st=$?"

#### nohup utility not found
nohup nosuchcmd 2>/dev/null; echo "st=$?"

#### nohup utility not executable
printf 'x\n' > f; chmod 644 f; nohup ./f 2>/dev/null; echo "st=$?"

#### nohup -- before the utility
nohup -- echo x

#### nohup keeps stderr when it is not a terminal
nohup sh -c 'echo err >&2' 2>&1 | cat

#### nohup runs a script without #!
printf 'echo script "$@"\n' > s; chmod 755 s; nohup ./s a
