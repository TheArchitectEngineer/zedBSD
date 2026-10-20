# ws269-p008 q1508: the VisionFive 2 test image of the gpu-selftest
# configuration (plan/ws269/tests/p010/config-visionfive2-selftest.mk) with
# the CPU and GPU coherency stress, vkcoherency (/bin/vkcoherency, run by
# hand).  Built as that configuration is, with SELFTEST_FIRMWARE for a
# runnable image.
include plan/ws269/tests/p010/config-visionfive2-selftest.mk

ZEDBSD_USER_PROGRAMS += vkcoherency
