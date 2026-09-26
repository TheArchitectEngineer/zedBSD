#!/bin/sh
# Runs expat's configure and make -j4, then builds and runs tests/runtests,
# on the amd64 guest with a given /bin/sh, and prints the status of each
# step and a checksum of every file configure generated, so that two
# shells can be compared (the same checksums mean configure made the same
# decisions).  The guest boots a copy of IMAGE and is stopped afterwards.
# Timing is not measured (the machine may be loaded).  From ws065-p004.
#
#   [IMAGE=...] [GUEST_RUNTIME=...] sh plan/tools/sh/guest-expat.sh SH [EXPAT_TAR]
#
# SH is a sh built for the guest (build-guest-sh.sh); EXPAT_TAR defaults to
# /home/awe/zedBSD-rpi4/build/ws046/expat-src.tar (expat 2.8.5, read only).
# IMAGE defaults to the native guest image
# /home/awe/zedBSD-rpi4/build/ws053-full-hal-guest/hdd-image.img, which has
# clang and make.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
sh_binary=$1
expat=${2:-/home/awe/zedBSD-rpi4/build/ws046/expat-src.tar}
image=${IMAGE:-/home/awe/zedBSD-rpi4/build/ws053-full-hal-guest/hdd-image.img}
guest="python3 plan/tools/guest/guest.py"

$guest stop >/dev/null 2>&1
$guest start --disk nvme "$image" >/dev/null
$guest wait >/dev/null
$guest put "$sh_binary" /bin/sh.new
$guest run 'chmod 755 /bin/sh.new && mv /bin/sh.new /bin/sh'
$guest put "$expat" /root/expat-src.tar
$guest run 'cd /root && rm -rf expat-2.8.5 && pax -r -f expat-src.tar && rm -f expat-src.tar'
$guest run 'cd /root/expat-2.8.5 && ./configure --build=x86_64-unknown-zedbsd CC=clang >/root/configure.log 2>&1; echo configure status $?'
$guest run 'cd /root/expat-2.8.5 && for f in config.status Makefile lib/Makefile xmlwf/Makefile tests/Makefile expat_config.h libtool expat.pc run.sh; do [ -f "$f" ] && echo "$(cksum < "$f") $f"; done'
$guest run 'cd /root/expat-2.8.5 && make -j4 >/root/make.log 2>&1; echo make status $?'
$guest run 'cd /root/expat-2.8.5/tests && make -j4 runtests >/root/make-tests.log 2>&1; echo make runtests status $?'
$guest run 'cd /root/expat-2.8.5 && ./tests/runtests >/root/runtests.log 2>&1; echo runtests status $?; tail -2 /root/runtests.log'
$guest stop >/dev/null 2>&1
