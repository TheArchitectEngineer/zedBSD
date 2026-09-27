# ws070 (System Menu): a lean zdesktop guest image for the Venus tests.
# The SSH guest harness (openssh, the serial mirror) without clang, lldb and
# libcxx, the Venus driver, the Wayland and Vulkan libraries, libzdesktop,
# the compositor, the terminal and the test clients (wlshm, wltest, mview,
# menu-probe).  Build:
#   plan/ws070/tests/build-menu-image.sh [BUILD]
include plan/ws035/tests/config-amd64-userland.mk
CONFIG_PCAT_SERIAL_MIRROR := y
CONFIG_DRIVER_PCI_VENUS := y
ZEDBSD_USER_PROGRAMS += openssl openssh
ZEDBSD_USER_PROGRAMS += libvulkan libwayland-client libtruetype wlshm wltest mview zdesktop-terminal zdesktop menu-probe titlebar-probe popup-probe subsurface-probe
# For zdesktop-p070 (App Home starts X applications through zdesktop-x11: zdesktop-x11server since
# WS069 p008, zterm, zgears over libGL).
ZEDBSD_USER_PROGRAMS += libwayland-egl libegl libglesv2 libgl zgears zdesktop-x11server
