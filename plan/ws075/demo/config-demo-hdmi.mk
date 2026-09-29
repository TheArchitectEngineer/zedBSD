# ws075-p013 (H4): the demonstration image for the Latitude 5330 with the 10-inch LCD on HDMI (2026-10-17).
# The i915 zdesktop image (plan/ws031/tests/config-zdesktop-hw.mk: the i915 driver and its firmware, the Vulkan,
# EGL/GLES and Wayland libraries, the compositor, the X server and the applications of App Home) with the default
# graphical boot: the loader's Kei splash (logo=), no kernel message on the screen (kmsg=quiet), the greeter
# (login=graphical: sessiond starts /bin/wayland --greeter) and, after a login, the session on the same display.
# display=edp keeps the machine's own LCD (the eDP panel) as the only output (2026-09-29: the HDMI display is set
# aside; display=hdmi takes the HDMI sink when it is connected at boot).  Build: plan/ws075/demo/build-demo-image.sh [BUILD] [passthrough]
include plan/ws031/tests/config-zdesktop-hw.mk
# The handwriting demonstration (WS079): Notes and PDF Viewer (with libpdf and libjpeg-compat); App Home lists them
# (plan/ws035/demo/apps.conf) only when they are in the image.
ZEDBSD_USER_PROGRAMS += libjpeg-compat libpdf notes pdfviewer
# The image viewer (WS091) and its image libraries; App Home lists it the same way.
ZEDBSD_USER_PROGRAMS += libz-compat libpng-compat libgif-compat imageview
ZEDBSD_GRAPHICAL_BOOT := y
ZEDBSD_BOOT_EXTRA_LINES ?= display=edp
# The OpenSSH server for looking into the machine while it shows the demonstration (2026-09-29: on bare metal the
# screen and the login cannot be relied on).  The host keys are made at the first start (sshd-start); root takes
# the password and the guest harness's key (build-demo-image.sh).
ZEDBSD_USER_PROGRAMS += openssl openssh
