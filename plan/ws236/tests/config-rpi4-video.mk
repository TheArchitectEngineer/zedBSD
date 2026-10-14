# ws236: the Pi 4's CI config with VCHIQ and the H.264 decode as Vulkan Video (p004), and the probe programs.
# A test image only; both drivers are off by default.
include config/ci/config-rpi4.mk
CONFIG_DRIVER_BCM2711_VCHIQ := y
CONFIG_DRIVER_BCM2711_VIDEO := y
ZEDBSD_USER_PROGRAMS += vkvideo-probe
