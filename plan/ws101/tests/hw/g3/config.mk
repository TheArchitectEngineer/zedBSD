# ws101-p011: the image of G3 (Noct's automatic parallelization) on the i915 passthrough of the 5330
# (plan/ws101/tests/hw/g3-hw.sh): the image of plan/ws101/tests/hw/gles/config.mk (the zdesktop image of the 5330
# with glescompute) with the demonstration's program and script (gpudemo).  /bin/noct comes from a build made with
# ZEDBSD_NOCT_ACCEL := y (g3-hw.sh's NOCT), not from this one, which keeps noct out.
include plan/ws101/tests/hw/gles/config.mk
ZEDBSD_USER_PROGRAMS += gpudemo
