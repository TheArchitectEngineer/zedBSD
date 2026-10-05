# ws174: the SSH guest image with the build's graphical boot configuration
# (logo=logo.ppm login=graphical kmsg=quiet), for the boot keys' QEMU cells
# (Ctrl: kmsg=console without a logo, Shift: login=console).  Build with
#   plan/tools/guest/test-image.sh plan/ws174/tests/config-amd64-keys.mk build/ws174-keys
include plan/tools/guest/config-amd64-ssh.mk
ZEDBSD_GRAPHICAL_BOOT := y
ZEDBSD_BOOT_KERNEL_MESSAGES := n
