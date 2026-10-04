#!/bin/sh
# ws045: builds the base utilities WS045 changed for the amd64 guest, as
# dynamic programs against the libc.so of an existing guest image's build,
# so that they can be copied into a running guest and tried there without
# building a new image.  zedBSD's regex (src/libc/regex, with the ERE
# back-references of ws045-p002) is linked into each program, where its
# regcomp and regexec take the place of the image's libc's.
#
#   sh plan/tools/gnu-utils/build-guest-utils.sh IMAGE_BUILD [OUTPUT_DIR]
#
# IMAGE_BUILD is the build directory of the image the guest runs (read only),
# the full guest image's (plan/tools/guest/build-full-image.sh); its
# dynamic/libc.so is linked against, and this tree's build/amd64 gives the
# sysroot (make sysroot-amd64).  ws136-p003: the defaults were another tree's
# build, now gone.  OUTPUT_DIR defaults to build/ws045/guest-bin.
set -eu
image_build=${1:?usage: build-guest-utils.sh IMAGE_BUILD [OUTPUT_DIR]}
out=${2:-build/ws045/guest-bin}
sysroot=build/amd64/sysroot
clang=build/llvm/bin/clang
objects=$out/.obj
mkdir -p "$out" "$objects"

cflags="--target=x86_64-unknown-zedbsd --sysroot=$sysroot -m64 -march=x86-64 \
-mno-red-zone -Os -ffreestanding -fPIC -fno-builtin -fno-stack-protector \
-Wall -Wextra -nostdinc -I. -Iinclude -isystem $sysroot/usr/include \
-DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64"
link="--target=x86_64-unknown-zedbsd --sysroot=$sysroot -m64 -nostdlib -pie \
-Wl,--no-relax -Wl,--gc-sections -Wl,--hash-style=sysv,-z,now,-z,relro,-z,separate-code \
-Wl,-z,stack-size=0x100000,--allow-shlib-undefined -Wl,--dynamic-linker=/lib/ld.so \
$sysroot/usr/lib/crt1.o"
libs="-L$image_build/dynamic -Wl,-rpath-link,$image_build/dynamic -l:libc.so"

# Compiles one C file into the object directory; prints the object's path.
compile() {
	object=$objects/$(printf '%s' "$1" | tr '/.' '__').o
	# shellcheck disable=SC2086
	$clang $cflags -c "$1" -o "$object"
	printf '%s\n' "$object"
}

# The shared pieces: the command helpers and the regex.
common=$(compile userland/base/common/command.c)
regex=""
for source in src/libc/regex/regcomp.c src/libc/regex/regexec.c \
    src/libc/regex/regerror.c src/libc/regex/tre-mem.c; do
	regex="$regex $(compile "$source")"
done

# Each utility.
for utility in sed grep awk sort head tail cmp touch date find readlink stat \
    expr rm mv mkdir ln basename cut wc uniq tr env split tee cp; do
	programs=""
	for source in userland/base/$utility/*.c; do
		programs="$programs $(compile "$source")"
	done
	# shellcheck disable=SC2086
	$clang $link $programs $common $regex $libs -o "$out/$utility"
done

# The shell (for its env builtin, which leaves GNU's options to the env
# command), with its line editor; in a directory of its own, since the
# cases run the image's /bin/sh.
mkdir -p "$out.sh"
programs=""
for source in userland/base/sh/*.c userland/base/libedit/readline.c; do
	object=$objects/$(printf '%s' "$source" | tr '/.' '__').o
	# shellcheck disable=SC2086
	$clang $cflags -Iuserland/base/libedit -c "$source" -o "$object"
	programs="$programs $object"
done
# shellcheck disable=SC2086
$clang $link $programs $common $libs -o "$out.sh/sh"

# echo is the shell's builtin as a command.
programs="$(compile userland/base/echo/main.c) $(compile userland/base/sh/printf.c) \
$(compile userland/base/common/builtin-standalone.c)"
# shellcheck disable=SC2086
$clang $link $programs $common $libs -o "$out/echo"
echo "$out"
