# The SSH guest (plan/tools/guest/config-amd64-ssh.mk) with sysctl, for plan/ws221/tests/qemu-usb-idle.sh: each CPU's
# busy share read from hw.cputimes while the guest has a USB hub on its xHCI and nothing to do.
# Build: plan/tools/guest/test-image.sh plan/ws221/tests/config-amd64-idle.mk BUILD
include plan/tools/guest/config-amd64-ssh.mk
ZEDBSD_USER_PROGRAMS += sysctl
