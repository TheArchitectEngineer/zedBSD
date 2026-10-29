#!/bin/sh
# Builds and runs a host test (post-test.c, process-test.c) against a Linux build of libkeiland.
#
#   sh plan/ws314/tests/host-test.sh TEST LINUX_BUILD     # e.g. process build/ws314-p005-linux
set -u
test=$1
build=$2
lib=$build/lib
out=build/ws314-host-test
mkdir -p "$out"
case $build in *tsan*) sanitize=-fsanitize=thread ;; *) sanitize= ;; esac
cc $sanitize -std=gnu17 -Wall -Wextra -Werror -O1 -g -Iuserland/desktop/include -I"$build/include" \
	"plan/ws314/tests/$test-test.c" -o "$out/$test-test" -L"$lib" -lkeiland -lwayland-server -pthread \
	-Wl,-rpath,"$PWD/$lib" || exit 1
dir=$(mktemp -d "$PWD/$out/run.XXXXXX")
chmod 700 "$dir"
LD_LIBRARY_PATH="$PWD/$lib" XDG_RUNTIME_DIR=$dir timeout 300 "$out/$test-test"
