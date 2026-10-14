# ws236: the Pi 4's CI config with the VCHIQ link (p002) and, from p004, the
# Vulkan Video decode on.  A test build only; both are off by default.
include config/ci/config-rpi4.mk
CONFIG_DRIVER_BCM2711_VCHIQ := y
