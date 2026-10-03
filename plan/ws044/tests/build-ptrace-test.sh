#!/bin/sh
# ws044-p010: builds plan/ws044/tests/ptrace-test.c for a disk image (a
# position-independent program on the build's /lib/libc.so) and copies it,
# as /ptrace, onto the image's FAT partition: the SD card's boot partition
# (arm64) or the EFI system partition (amd64).  The guest mounts that
# partition and copies the program out to run it.
#   sh plan/ws044/tests/build-ptrace-test.sh BUILD arm64|amd64
set -e
BUILD=${1:?build directory}
ARCH=${2:?arm64 or amd64}
LLVM=build/llvm/bin
case $ARCH in
arm64)
	TRIPLE=aarch64-unknown-zedbsd
	SYSROOT=build/arm64/sysroot
	DEFINES="-DHAL_ARCH_ARM64 -DKERN_USER_ABI_AARCH64 -DKERN_USER_ABI_LP64"
	;;
amd64)
	TRIPLE=x86_64-unknown-zedbsd
	SYSROOT=build/amd64/sysroot
	DEFINES="-DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64"
	;;
*)
	echo "unknown architecture: $ARCH" >&2
	exit 1
	;;
esac
OUT=$BUILD/tests/ptrace-test
mkdir -p "$BUILD/tests"
"$LLVM/clang" --target=$TRIPLE --sysroot="$SYSROOT" -O1 -fPIE \
	-Wall -Wextra -Werror $DEFINES \
	-c plan/ws044/tests/ptrace-test.c -o "$OUT.o"
"$LLVM/clang" --target=$TRIPLE --sysroot="$SYSROOT" -nostdlib -pie \
	"$SYSROOT/usr/lib/crt1.o" "$OUT.o" -L"$BUILD/dynamic" -l:libc.so \
	-o "$OUT"

# Finds the byte offset of the FAT partition: the first MBR partition, or
# the GPT entry of type EFI system partition.
OFFSET=$(python3 - "$BUILD/hdd-image.img" <<'EOF'
import struct, sys, uuid
with open(sys.argv[1], 'rb') as f:
    mbr = f.read(512)
    if mbr[450] != 0xee:
        print(struct.unpack_from('<I', mbr, 454)[0] * 512)
        sys.exit(0)
    header = f.read(512)
    lba, count, size = struct.unpack_from('<QII', header, 72)
    f.seek(lba * 512)
    table = f.read(count * size)
esp = uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b').bytes_le
for index in range(count):
    entry = table[index * size:(index + 1) * size]
    if entry[:16] == esp:
        print(struct.unpack_from('<Q', entry, 32)[0] * 512)
        sys.exit(0)
sys.exit('no FAT partition')
EOF
)
mcopy -o -i "$BUILD/hdd-image.img@@$OFFSET" "$OUT" ::/ptrace
echo "$OUT (FAT partition at byte $OFFSET)"
