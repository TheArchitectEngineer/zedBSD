# ws068-p019: the guest image of the GLSL compiler's Venus tests.  The zdesktop image
# (plan/ws035/tests/config-amd64-zdesktop.mk: the SSH guest harness, the Venus driver, the Vulkan,
# Wayland, EGL, GLES and GL libraries, zdesktop and its test clients) without the packages that take
# hours to build and that these tests do not use: clang and libc++ (lldb) and noct.  Build with
#   plan/ws068/tests/build-glsl-image.sh [BUILD]
include plan/ws035/tests/config-amd64-zdesktop.mk
ZEDBSD_USER_PROGRAMS := $(filter-out noct libcxx clang,$(ZEDBSD_USER_PROGRAMS))
