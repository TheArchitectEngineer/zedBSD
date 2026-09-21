#!/bin/sh
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
exec python3 "$root/plan/ws032/tests/run-target-console.py" \
	--image "$root/build/amd64/hdd-image.img" --timeout 300 \
	'ls -la /usr/lib/libLLVM.so.23.1 /usr/lib/libclang-cpp.so.23.1' \
	'llvm-nm --version; echo "nm=$?"' \
	'ld.lld --version; echo "lld=$?"' \
	'llvm-ar --version; echo "ar=$?"' \
	'clang --version; echo "clang=$?"' \
	'echo PROBE-DONE'
