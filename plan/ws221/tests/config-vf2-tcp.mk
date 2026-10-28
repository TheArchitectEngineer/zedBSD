# The VisionFive 2 kernel and the TCP probes for the board's measurements (plan/ws221/tests/vf2-tcp.sh): the CI
# configuration with sysctl (net.tcpstats) and tcp-perf.  Only the kernel and the two programs go to the board.
#   make ZEDBSD_CONFIG=plan/ws221/tests/config-vf2-tcp.mk BUILD=build/vf2 build/vf2/vmunix build/vf2/bin/tcp-perf
include config/ci/config-visionfive2.mk
ZEDBSD_USER_PROGRAMS += sysctl tcp-perf ifconfig ping
