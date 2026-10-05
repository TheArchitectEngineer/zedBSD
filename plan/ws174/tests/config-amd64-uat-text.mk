# BUG-202: the UAT image with text boot and kernel messages, so a panic on the
# 5330 can be read on screen (until WS174's Ctrl safe boot exists).
include plan/ws159/tests/config-amd64-uat.mk
ZEDBSD_GRAPHICAL_BOOT := n
ZEDBSD_BOOT_KERNEL_MESSAGES := y
