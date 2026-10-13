# ws220-p001, p002: the SSH guest (plan/tools/guest/config-amd64-ssh.mk) with sysctl, which reads net.tcpstats, and
# tcp-stream, the blocking sender of the up run, for plan/ws220/tests/tcp-stream-series.sh.
# Build: plan/tools/guest/test-image.sh plan/ws220/tests/config-amd64-tcp.mk BUILD
include plan/tools/guest/config-amd64-ssh.mk
ZEDBSD_USER_PROGRAMS += sysctl tcp-stream
