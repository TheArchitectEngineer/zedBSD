# The console's leaving of the boot framebuffer by the test trigger: the base userland with the console mirrored on
# the first serial port (plan/tools/guest/serial.py), the kernel's messages and the console login on the screen, and
# a kernel that ends the console's use of the display 120 seconds after the text layer starts
# (KERN_TEST_TEXT_END_SECONDS in src/drivers/platform/pcat/graphics/text.c).  Built and run by
# plan/ws290/tests/text-end-qemu.sh.
include plan/tools/posix/config-amd64-serial.mk
ZEDBSD_GRAPHICAL_BOOT := n
ZEDBSD_BOOT_KERNEL_MESSAGES := y
ZEDBSD_GRAPHICAL_LOGIN := n
ZEDBSD_TEST_CPPFLAGS := -DKERN_TEST_TEXT_END_SECONDS=120
