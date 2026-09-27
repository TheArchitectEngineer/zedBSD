#!/bin/sh
# ws043: builds the text utilities of userland/base for the host (Linux), so
# that the difference tests run in seconds.  They use only POSIX interfaces
# and userland/base/common.
#   sh plan/tools/utils/build-host-utils.sh [OUTPUT_DIR]   (default build/ws043/bin)
#
# ws045: the regular expressions are zedBSD's (src/libc/regex, TRE), not
# glibc's, because the GNU extensions of the regular expressions (\+, \|,
# \w, \< and the others) come from the library the guest links.  The
# program sees zedBSD's <regex.h> first on the include path, and the TRE
# objects linked into the program take the place of glibc's functions.
set -e
out=${1:-build/ws043/bin}
mkdir -p "$out" "$out/.regex"
cp include/libc/regex.h "$out/.regex/regex.h"
regex="src/libc/regex/regcomp.c src/libc/regex/regexec.c src/libc/regex/regerror.c src/libc/regex/tre-mem.c"
for utility in sed grep awk cut wc head tail sort uniq tr od expr paste join comm fold nl split csplit tsort basename dirname cat rm rmdir mkdir ln touch mv cp chmod ls \
    cmp find date stat readlink realpath seq tac timeout truncate env tee xargs; do
	# mv shares cp's copy of file hierarchies (ws001-p025).
	extra=
	if [ "$utility" = mv ]; then
		extra=userland/base/cp/copy.c
	fi
	# mkdir and mkfifo share chmod's mode operand (ws001-p026).
	case $utility in
	mkdir|mkfifo)
		extra=userland/base/chmod/mode.c
		;;
	esac
	cc -std=c11 -D_GNU_SOURCE -O1 -g -w -I"$out/.regex" -I. -Iinclude \
		userland/base/$utility/*.c $extra userland/base/common/command.c \
		$regex -o "$out/$utility" -lm
done
# The shell's echo, printf and test built as commands, and true and false.
for utility in echo printf test; do
	source=printf
	if [ "$utility" = test ]; then
		source=test
	fi
	cc -std=c11 -D_GNU_SOURCE -O1 -g -w -I. -Iinclude \
		userland/base/$utility/main.c userland/base/sh/$source.c \
		userland/base/common/builtin-standalone.c -o "$out/$utility"
done
for utility in true false; do
	cc -std=c11 -O1 -g -w userland/base/$utility/main.c -o "$out/$utility"
done
# egrep and fgrep are grep -E and grep -F.
printf '#!/bin/sh\nexec grep -E "$@"\n' > "$out/egrep"
printf '#!/bin/sh\nexec grep -F "$@"\n' > "$out/fgrep"
chmod +x "$out/egrep" "$out/fgrep"
echo "$out"
