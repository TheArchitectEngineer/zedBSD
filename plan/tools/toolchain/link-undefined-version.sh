#!/bin/sh
# Links a shared object with a version script that names a
# symbol the object does not define, for each zedbsd target, with a given
# clang: by default (the driver passes --undefined-version, so it must
# link) and with -Wl,--no-undefined-version (which must fail).  Prints one
# line per target: "TARGET default=STATUS strict=STATUS", then the
# linker command the driver builds for x86_64.
#   sh plan/tools/toolchain/link-undefined-version.sh CLANG [WORK_DIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
clang=$1
work=${2:-build/ws055/link-test}
mkdir -p "$work"
printf 'int present(void) { return 1; }\n' > "$work/t.c"
printf 'V1 {\n global: present; missing;\n local: *;\n};\n' > "$work/v.map"
for target in x86_64-unknown-zedbsd i386-unknown-zedbsd aarch64-unknown-zedbsd; do
	"$clang" --target="$target" -nostdlib -shared -fPIC "$work/t.c" \
	    -Wl,--version-script="$work/v.map" -o "$work/lib.so" 2> "$work/default.err"
	default=$?
	"$clang" --target="$target" -nostdlib -shared -fPIC "$work/t.c" \
	    -Wl,--version-script="$work/v.map" -Wl,--no-undefined-version \
	    -o "$work/lib-strict.so" 2> "$work/strict.err"
	strict=$?
	echo "$target default=$default strict=$strict"
done
"$clang" --target=x86_64-unknown-zedbsd -nostdlib -shared -fPIC "$work/t.c" \
    -Wl,--version-script="$work/v.map" -o "$work/lib.so" -### 2>&1 | tail -1
