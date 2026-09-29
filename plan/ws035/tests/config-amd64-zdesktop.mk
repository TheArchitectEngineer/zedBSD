# sq001 (ws035-p052 and later): the guest image for the zdesktop phases.  The
# SSH guest harness image (openssh, lldb, serial mirror) plus the Venus driver,
# the Vulkan and Wayland libraries, the compositor and its test clients.  Build:
#   eval "make -j16 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk \
#       BUILD=build/ws035-sq $(plan/tools/guest/guest.sh extra-files) disk-image"
include plan/ws035/tests/config-amd64-guest.mk
CONFIG_DRIVER_PCI_VENUS := y
ZEDBSD_USER_PROGRAMS += libvulkan libwayland-client libwayland-egl libegl libglesv2 libtruetype wltest wlshm vkdemo mview terminal egltest libgl glxtest zgears wayland xserver acquire-fence-test
# ws035-p090: App Home's Files and Browser (the browser shows when its start page is there).
ZEDBSD_USER_PROGRAMS += libz-compat libpng-compat files browser
# ws079-p006: PDF Viewer and libpdf (with libjpeg-compat).
ZEDBSD_USER_PROGRAMS += libjpeg-compat libpdf pdfviewer textedit
# ws090-p004: the desktop's shared widgets, which Text Editor's window, scroll and touch come from.
ZEDBSD_USER_PROGRAMS += libkeiui
# ws090-p005: the widgets' sampler, in this test image only (not the default image).
ZEDBSD_USER_PROGRAMS += kuidemo
