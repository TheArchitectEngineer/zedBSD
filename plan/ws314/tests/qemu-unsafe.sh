#!/bin/sh
# QEMU for WS314's guests: the guest harness's command (plan/tools/guest/guest.py start --qemu THIS) with the boot
# disk's cache unsafe (the copy is thrown away after the test; BUG-359).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
for argument do
	shift
	case $argument in
	if=none,id=boot,*) argument="$argument,cache=unsafe" ;;
	esac
	set -- "$@" "$argument"
done
exec qemu-system-x86_64 "$@"
