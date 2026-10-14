# ws236: the Pi 4's CI config with VCHIQ, the H.264 decode as Vulkan Video, vkvideo-probe and the boot-time probe of
# the decoder's output order (one boot for the probe and the Vulkan path).  A test image only.
include plan/ws236/tests/config-rpi4-video.mk
CONFIG_DRIVER_BCM2711_VIDEO_PROBE := y
