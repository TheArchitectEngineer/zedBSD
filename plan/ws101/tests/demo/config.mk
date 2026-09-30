# ws101-p015: the demonstration image (plan/ws075/demo/config-demo-hdmi.mk, with gpudemo and the accelerator-enabled
# noct) for scene S13 on the 5330's passthrough, built by plan/ws101/tests/demo/build-s13-image.sh.  noct is left
# out of the build and its /bin/noct given as a file (build-s13-image.sh's NOCT, made with ZEDBSD_NOCT_ACCEL := y by
# the main checkout), so WS101 does not build Noct (the toolchain rule).
include plan/ws075/demo/config-demo-hdmi.mk
ZEDBSD_USER_PROGRAMS := $(filter-out noct,$(ZEDBSD_USER_PROGRAMS))
