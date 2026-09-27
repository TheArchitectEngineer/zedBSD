#!/bin/sh
# ws074: prints the C sources userland/desktop/browser/Makefile lists, one per line, as paths
# from the repository root (the generated tables, which live under build/, are not included).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
sed -n 's/^[^#]*\$(ZDESKTOP_BROWSER_DIR)\/\([^ \\]*\.c\).*/userland\/base\/browser\/\1/p' \
	userland/desktop/browser/Makefile
