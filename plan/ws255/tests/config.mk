# ws255-p001: the image of the QEMU check of a static IPv4 test image: the SSH guest image
# (plan/tools/guest/config-amd64-ssh.mk) with plan/ws255/tests/net-qemu.conf copied over /etc/net.conf
# (see plan/ws255/tests/static-qemu.sh).  The machines' images keep their own configurations and only add the copy
# of their net-<machine>.conf (plan/ws255/static-ip.md).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/tools/guest/config-amd64-ssh.mk
