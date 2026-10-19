#!/bin/sh
# Host test of the static TLS placement (ws276-p001).
# Usage: plan/tools/tls-layout/run.sh [output-directory]
# Runs from the repository root.  Output defaults to build/ws276-p001.
set -eu

out=${1:-build/ws276-p001}
here=plan/tools/tls-layout
mkdir -p "$out"

# The placement for 64-bit and 32-bit pointers (amd64 and i386 sizes).
cc -std=c89 -Wall -Wextra -Werror -I. -o "$out/tls-layout-64" \
	"$here/tls-layout-host-test.c"
cc -m32 -std=c89 -Wall -Wextra -Werror -I. -o "$out/tls-layout-32" \
	"$here/tls-layout-host-test.c"

# The m68k linker's resolution of local-exec accesses with x68k's user.ld.
m68k-linux-gnu-gcc -m68030 -msoft-float -ffreestanding -fno-pic -fno-pie \
	-Os -c "$here/probe.c" -o "$out/probe.o"
m68k-linux-gnu-ld -static -T platform/x68k/user.ld "$out/probe.o" \
	-o "$out/probe.elf"
m68k-linux-gnu-readelf -lW "$out/probe.elf" > "$out/probe.phdrs"
m68k-linux-gnu-nm "$out/probe.elf" > "$out/probe.syms"
m68k-linux-gnu-objdump -d "$out/probe.elf" > "$out/probe.dis"
accesses=$(python3 -I "$here/accesses.py" "$out/probe.phdrs" \
	"$out/probe.syms" "$out/probe.dis")

"$out/tls-layout-64" $accesses
"$out/tls-layout-32" $accesses
