# ws236 design §12: the Pi 4's CI config with VCHIQ and the boot-time probe of the VideoCore decoder's output order.
# A test image only.  The probe writes "rpi4-mmal-probe:" lines to the kernel log (dmesg) after boot.
include config/ci/config-rpi4.mk
CONFIG_DRIVER_BCM2711_VCHIQ := y
CONFIG_DRIVER_BCM2711_VIDEO_PROBE := y
