# ws101-p009: the guest image of the OpenGL ES 3.1 compute test on Venus.  The lean GLSL image
# (plan/ws068/tests/config-amd64-glsl.mk: the zdesktop image without clang, libc++ and noct) with
# /bin/glescompute.  Build with
#   plan/ws101/tests/gles/build-image.sh [BUILD]
include plan/ws068/tests/config-amd64-glsl.mk
ZEDBSD_USER_PROGRAMS += glescompute
