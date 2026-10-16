# ws254-p001: the image of the QEMU check of networkd's DHCP retry.
# The SSH guest image (plan/tools/guest/config-amd64-ssh.mk) with plan/ws254/tests/net.conf: ue0 is the harness's
# network (QEMU's own DHCP, SSH forwarded), ue1 a second wired interface whose DHCP server (dhcp-server.py on the
# host) answers only after a while.  Built by plan/ws254/tests/retry-qemu.sh.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/tools/guest/config-amd64-ssh.mk
