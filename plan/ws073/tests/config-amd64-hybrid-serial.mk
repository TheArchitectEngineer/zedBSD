# ws073-p015: the hybrid layout (an ESP and a BOOT FAT with the overlay
# images and the swap file) with the console mirrored on the first serial
# port, so that plan/tools/guest/serial.py can drive the lean image, which
# has no sshd.
include plan/ws073/tests/config-amd64-hybrid.mk
CONFIG_PCAT_SERIAL_MIRROR := y
