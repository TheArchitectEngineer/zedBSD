#!/bin/sh
# ws073-p049 (BUG-033): makes the compile-time kit for the guest clang measurement: expat 2.8.5's lib/ sources
# with an expat_config.h from the host's configure (the compile only needs the header; nothing is linked).
# The archive is checked against the digest the expat package records.  Writes OUT (default
# build/p1-q651/bug033/kit.tar) and prints the host's own compile times with the project's clang for the
# zedBSD target, for the ratio.
#   sh plan/ws073/tests/bug033-kit.sh [OUT]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
root=$(pwd)
out=${1:-build/p1-q651/bug033/kit.tar}
work=$(dirname "$out")/kit-work
rm -rf "$work"
mkdir -p "$work"
archive=$work/expat-2.8.5.tar.xz
curl -sSL -o "$archive" https://github.com/libexpat/libexpat/releases/download/R_2_8_5/expat-2.8.5.tar.xz
echo "1e727b8933ec51a77a9a9d9afcf8e688bce45d907c13e36ab7393fe36e703182  $archive" | sha256sum -c -
(cd "$work" && tar xf expat-2.8.5.tar.xz && cd expat-2.8.5 && ./configure --without-docbook >/dev/null 2>&1)
mkdir -p "$work/bug033-kit"
cp "$work/expat-2.8.5/expat_config.h" "$work"/expat-2.8.5/lib/*.c "$work"/expat-2.8.5/lib/*.h "$work/bug033-kit/"
cp plan/ws073/tests/bug033-clang.sh "$work/bug033-kit/"
(cd "$work" && tar cf "$root/$out" bug033-kit)
cc="$root/build/llvm/bin/clang --target=x86_64-unknown-zedbsd --sysroot=$root/build/amd64/sysroot"
cd "$work/bug033-kit"
for f in xmlparse xmltok xmlrole; do
	/usr/bin/time -f "host $f %e s" $cc -DHAVE_EXPAT_CONFIG_H -I. -O2 -w -c $f.c -o /dev/null
done
echo "kit $root/$out"
