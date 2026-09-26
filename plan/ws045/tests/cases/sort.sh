# ws045: GNU extensions of sort, compared with GNU sort without
# POSIXLY_CORRECT.

#### -V versions
printf '1.10\n1.9\n1.2.3\n1.2\n1.2.10\n1.02\n' | sort -V

#### -V names with suffixes
printf 'foo-1.10.tar.gz\nfoo-1.9.tar.gz\nfoo-1.9.tar.bz2\nfoo-1.9\n' | sort -V

#### -V with ~ and letters
printf '1.0\n1.0~rc1\n1.0a\n1.0-1\n1.0.1\n' | sort -V

#### -V hidden names and empty lines
printf 'b\n.a\n\n.\n..\na\n' | sort -V

#### -V gcc libraries (emacs configure)
printf 'gcc-10\ngcc-9\ngcc-12.1\ngcc-12\n' | sort -V | tail -n 1

#### -k2V
printf 'x 1.10\ny 1.9\nz 1.2\n' | sort -k2V

#### -rV
printf '1.10\n1.9\n' | sort -rV

#### -h
printf '1K\n2M\n512\n3G\n1.5K\n-1K\n0\n' | sort -h

#### -h with -r and a key
printf 'a 10M\nb 2G\nc 100K\n' | sort -k2,2hr

#### -g
printf '1e3\n10\n-2.5\n0x10\nabc\n1.5e-1\n' | sort -g

#### -M
printf 'Mar\njan\nDEC\nxyz\nFeb\n' | sort -M

#### -s keeps the input order of equal keys
printf 'b 1\na 1\nc 0\n' | sort -s -k2,2

#### without -s equal keys are ordered by the whole line
printf 'b 1\na 1\nc 0\n' | sort -k2,2

#### -z
printf 'b\0a\0c\0' | sort -z | od -c

#### long options
printf '3\n10\n2\n3\n' | sort --numeric-sort --reverse --unique

#### --key and --field-separator
printf 'a:3\nb:1\nc:2\n' | sort --field-separator=: --key=2,2n

#### --sort=version
printf '1.10\n1.9\n' | sort --sort=version

#### --check=quiet
printf 'b\na\n' | sort --check=quiet; echo $?

#### --check
printf 'a\nb\n' | sort --check; echo $?

#### --output
printf 'b\na\n' > f
sort --output=f f
cat f

#### options after the operands
printf 'b\na\n' > f
sort f -r

#### -S and -T are accepted
printf 'b\na\n' | sort -S 1M -T /tmp

#### --ignore-case and --dictionary-order
printf 'b\nA\na\n' | sort --ignore-case --stable

#### -f -u keeps the first of equal keys
printf 'a\nA\nb\n' | sort -fu
