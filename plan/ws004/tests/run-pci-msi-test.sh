#!/bin/sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
# PCI MSI/MSI-X establish and checked removal: exact capability restore and
# INTx Disable held only while the message interrupt is in use (BUG-158).
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$test_dir/../../.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/zedbsd-pci-msi.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM

cc=${CC:-cc}
cc_flags="-std=c11 -I$repo_root/include -I$repo_root -I$repo_root/src -Wall -Wextra -Werror"

# shellcheck disable=SC2086
$cc $cc_flags "$repo_root/src/drivers/pci/pci.c" "$test_dir/pci-msi-test.c" \
	-o "$build_dir/pci-msi-test"
"$build_dir/pci-msi-test"

# shellcheck disable=SC2086
$cc $cc_flags -O1 -g -fsanitize=address,undefined \
	"$repo_root/src/drivers/pci/pci.c" "$test_dir/pci-msi-test.c" \
	-o "$build_dir/pci-msi-test-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$build_dir/pci-msi-test-sanitize"

echo "pci msi: ordinary, ASan/UBSan PASS"
