#!/bin/sh
# Builds and runs plan/ws314/tests/loader-test.c against libbrowser's network sources (the host's C compiler).
#
#   sh plan/ws314/tests/loader-test.sh [plain|tsan]
#
# The output goes to build/ws314-loader-test/<variant>/; the zlib headers come from the WS074 host build
# (sh plan/ws074/tests/host-build.sh plain makes build/ws074-host/include).
set -u
cd "$(dirname -- "$0")/../../.."
variant=${1:-plain}
case $variant in
plain) sanitize= ;;
tsan) sanitize=-fsanitize=thread ;;
*) echo "loader-test: unknown variant $variant" >&2; exit 2 ;;
esac
out=build/ws314-loader-test/$variant
mkdir -p "$out"
src=userland/desktop/libbrowser
cc $sanitize -std=gnu11 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -I$src -I. -Ibuild/ws074-host/include \
	$src/net/*.c $src/base/*.c userland/base/libz-compat/*.c plan/ws314/tests/loader-test.c \
	-o "$out/loader-test" -ldl -lpthread -lm || exit 1
TSAN_OPTIONS="halt_on_error=1 exitcode=66" timeout 120 "$out/loader-test"
