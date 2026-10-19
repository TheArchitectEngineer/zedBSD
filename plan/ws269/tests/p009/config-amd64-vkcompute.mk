# ws269-p009: amd64 Venus test image with vkcompute (T1-755).
include plan/tools/guest/config-amd64-ssh.mk
ZEDBSD_USER_PROGRAMS += libwayland-client libvulkan vkcompute
