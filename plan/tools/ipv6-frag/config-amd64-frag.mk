# The SSH guest (plan/tools/guest/config-amd64-ssh.mk) with ifconfig, ping, sysctl and udp6-recv, for
# plan/tools/ipv6-frag/qemu-frag.sh: IPv6 fragments from this host reassembled on the guest.
# Build: plan/tools/guest/test-image.sh plan/tools/ipv6-frag/config-amd64-frag.mk BUILD
include plan/tools/guest/config-amd64-ssh.mk
ZEDBSD_USER_PROGRAMS += ifconfig ping sysctl udp6-recv
