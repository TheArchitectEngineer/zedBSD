# The SSH guest with TCP (config-amd64-tcp.mk) and lsusb, cmp and cksum, for plan/ws221/tests/qemu-usb-xhci.sh: the
# xHCI's USB storage, network (CDC ECM) and HID devices after a change of the host controller driver.  A test image only.
# Build: plan/tools/guest/test-image.sh plan/ws221/tests/config-amd64-usb.mk BUILD
include plan/ws221/tests/config-amd64-tcp.mk
ZEDBSD_USER_PROGRAMS += lsusb cmp cksum
