# The SSH guest of plan/ws221/tests/config-amd64-tcp.mk with ping and su, for plan/ws221/tests/qemu-mtu.sh: the MTU
# interface (limits, change while up, the default, privilege) and the datagram length against it.
# Build: plan/tools/guest/test-image.sh plan/ws221/tests/config-amd64-jumbo.mk BUILD
include plan/ws221/tests/config-amd64-tcp.mk
ZEDBSD_USER_PROGRAMS += ping su id
