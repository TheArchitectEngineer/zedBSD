# ws075-p013 (H4): the demonstration image for the Latitude 5330 with the 10-inch LCD on HDMI (2026-10-17).
# The i915 zdesktop image (plan/ws031/tests/config-zdesktop-hw.mk: the i915 driver and its firmware, the Vulkan,
# EGL/GLES and Wayland libraries, the compositor, the X server and the applications of App Home) with the default
# graphical boot: the loader's Kei splash (logo=), no kernel message on the screen (kmsg=quiet), the greeter
# (login=graphical: sessiond starts /bin/wayland --greeter) and, after a login, the session on the same display.
# display=hdmi makes the HDMI sink the only output when it is connected at boot (the eDP panel stays dark), and the
# panel otherwise.  Build: plan/ws075/demo/build-demo-image.sh [BUILD] [passthrough]
include plan/ws031/tests/config-zdesktop-hw.mk
ZEDBSD_GRAPHICAL_BOOT := y
ZEDBSD_BOOT_EXTRA_LINES ?= display=hdmi
