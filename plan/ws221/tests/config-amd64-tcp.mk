# The SSH guest (plan/tools/guest/config-amd64-ssh.mk) with sysctl, ifconfig and tcp-perf, for
# plan/ws221/tests/qemu-tcp.sh: TCP transfers both ways between the guest and this host.
# Build: plan/tools/guest/test-image.sh plan/ws221/tests/config-amd64-tcp.mk BUILD
include plan/tools/guest/config-amd64-ssh.mk
ZEDBSD_USER_PROGRAMS += sysctl ifconfig tcp-perf
