# ws220-p002: config-amd64-tcp.mk with the kernel's sequence numbers started 16 MiB before the 2^32 wrap
# (CONFIG_TCP_ISN_WRAP_BEFORE in src/kern/net/tcp.c), so an up run of 64 MiB from the guest crosses the wrap every time.
# The down runs' sequence numbers are the host's and are not moved.
# Build: plan/tools/guest/test-image.sh plan/ws220/tests/config-amd64-tcp-wrap.mk BUILD
include plan/ws220/tests/config-amd64-tcp.mk
ZEDBSD_TEST_CPPFLAGS += -DCONFIG_TCP_ISN_WRAP_BEFORE=16777216U
