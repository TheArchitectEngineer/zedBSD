# ws070 (System Menu): a lean zdesktop guest image for the Venus tests.
# The SSH guest harness (openssh, the serial mirror) without clang, lldb and
# libcxx, the Venus driver, the Wayland and Vulkan libraries, libzdesktop,
# the compositor, the terminal and the wl_shm test client.  Build:
#   plan/ws070/tests/build-menu-image.sh [BUILD]
include plan/ws035/tests/config-amd64-userland.mk
CONFIG_PCAT_SERIAL_MIRROR := y
CONFIG_DRIVER_PCI_VENUS := y
ZEDBSD_USER_PROGRAMS += openssl openssh
ZEDBSD_USER_PROGRAMS += libvulkan libwayland-client libtruetype wlshm zdesktop-terminal zdesktop
