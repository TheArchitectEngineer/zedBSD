# ws073-p009: plan/ws045/tests/config-amd64-base.mk with the hybrid
# layout instead of the native one: an ESP and a BOOT FAT with the
# overlay images, for checking /boot and /boot/esp on that layout.
include plan/ws045/tests/config-amd64-base.mk
ZEDBSD_VARIANT := hybrid
