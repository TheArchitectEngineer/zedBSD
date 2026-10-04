#!/bin/sh
# ws160-p002: a stand-in passwd for host-account-backend.c: writes its arguments and the two lines it reads to
# $FAKE_PASSWD_LINES and exits with $FAKE_PASSWD_STATUS.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
IFS= read -r current
IFS= read -r fresh
printf 'args=%s\n%s\n%s\n' "$*" "$current" "$fresh" > "$FAKE_PASSWD_LINES"
exit "${FAKE_PASSWD_STATUS:-1}"
