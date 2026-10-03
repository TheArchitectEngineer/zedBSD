# ws101-p010: the image of OpenGL ES 3.1's compute on the i915 passthrough of the 5330 (plan/ws101/tests/hw/gles-hw.sh).
# The zdesktop image of the 5330 (plan/ws031/tests/config-zdesktop-hw.mk: the i915 driver and its firmware, the Vulkan,
# Wayland, EGL and GLES libraries, the compositor, egltest) with /bin/glescompute, without the packages that take
# long to build and that the run does not use (App Home's Files and Browser, clang, libc++, noct).
include plan/ws031/tests/config-zdesktop-hw.mk
ZEDBSD_USER_PROGRAMS := $(filter-out browser files libz-compat libpng-compat noct libcxx clang,$(ZEDBSD_USER_PROGRAMS))
ZEDBSD_USER_PROGRAMS += glescompute
