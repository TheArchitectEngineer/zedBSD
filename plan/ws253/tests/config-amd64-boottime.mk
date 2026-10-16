# ws253: the image whose boot is timed in QEMU -- the current UAT's image (config/current-uat.mk: the beta 2 release's
# configuration, graphical boot, kmsg=quiet, login=graphical, kei logged in by itself, sshd on) with two differences
# that do not touch the boot: the target's clang, libc++ and Emacs are left out (an implementation agent does not
# build the target toolchain), and root keeps the tree's password so that the guest harness can log in over SSH.
# Build with the guest harness's files (plan/tools/guest/test-image.sh):
#   flock /tmp/zedbsd-image-build.lock plan/tools/guest/test-image.sh plan/ws253/tests/config-amd64-boottime.mk BUILD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include config/current-uat.mk
ZEDBSD_USER_PROGRAMS := $(filter-out clang libcxx emacs,$(ZEDBSD_USER_PROGRAMS))
ZEDBSD_ROOT_LOCKED := n
