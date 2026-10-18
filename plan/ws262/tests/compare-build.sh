#!/bin/sh
#
# Compares two bootloader build directories made from different source trees.
#
# usage: compare-build.sh BEFORE_DIR AFTER_DIR
#
# Every output file present in BEFORE_DIR is compared byte for byte with the
# same path under AFTER_DIR.  A differing ELF object or image is compared
# again by its section contents and disassembly, so a difference that is only
# in symbol tables or file names is told apart from a difference in code.
# Build logs and dependency files are skipped.

set -u

before=$1
after=$2
objdump=${OBJDUMP:-/home/awe/zedBSD-claude1/build/llvm/bin/llvm-objdump}
same=0
differ=0
codediff=0

for file in $(cd "$before" && find . -type f ! -name '*.d' ! -name '*.log' | sort); do
	if [ ! -f "$after/$file" ]; then
		echo "MISSING $file"
		differ=$((differ + 1))
		continue
	fi
	if cmp -s "$before/$file" "$after/$file"; then
		same=$((same + 1))
		continue
	fi
	differ=$((differ + 1))
	case "$file" in
	*.o|*.elf)
		a=$($objdump -d -s --no-show-raw-insn "$before/$file" 2>/dev/null | sed 1,3d)
		b=$($objdump -d -s --no-show-raw-insn "$after/$file" 2>/dev/null | sed 1,3d)
		if [ "$a" = "$b" ]; then
			echo "DIFF-NONCODE $file"
		else
			echo "DIFF-CODE $file"
			codediff=$((codediff + 1))
		fi
		;;
	*)
		echo "DIFF-BYTES $file"
		codediff=$((codediff + 1))
		;;
	esac
done

echo "same=$same differ=$differ code_or_bytes_differ=$codediff"
