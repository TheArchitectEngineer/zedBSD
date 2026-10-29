# The Latitude 5330 regression image (plan/tools/hw5330/config-5330.mk) with the network probes, for
# plan/ws221/tests/5330-ncm.sh: the RTL8156's CDC NCM maximum datagram, jumbo echoes against the VisionFive 2, and TCP
# both ways against this host.  A test image only.
#   make ZEDBSD_CONFIG=plan/ws221/tests/config-5330-jumbo.mk BUILD=<dir> disk-image
include plan/tools/hw5330/config-5330.mk
ZEDBSD_USER_PROGRAMS += sysctl ifconfig ping tcp-perf udp-hold lsusb camcap
