#!/bin/sh
# ws074: prints the C sources userland/base/zdesktop-browser/Makefile lists, one per line, as paths
# from the repository root (the generated tables, which live under build/, are not included).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
sed -n 's/^[^#]*\$(ZDESKTOP_BROWSER_DIR)\/\([^ \\]*\.c\).*/userland\/base\/zdesktop-browser\/\1/p' \
	userland/base/zdesktop-browser/Makefile
