# The console's leaving of the boot framebuffer at a GPU's first flip: the graphical login image
# (plan/ws035/tests/config-amd64-graphical.mk: the compositor and its test greeter on the Venus virtio-gpu, the boot
# framebuffer on the standard VGA adapter) with the kernel's messages left on the screen, so that the console draws
# there until the first flip.  The SSH harness comes with the image (plan/tools/guest/test-image.sh).  Built and run
# by plan/ws290/tests/text-end-qemu.sh.
include plan/ws035/tests/config-amd64-graphical.mk
ZEDBSD_BOOT_KERNEL_MESSAGES := y
