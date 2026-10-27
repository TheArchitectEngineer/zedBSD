# ws299-p004: the QEMU serial test image with a 128 MiB buffer cache, so that reads keep the cache at its cap and
# full-cache misses take lines over (qemu-cache-stress.sh).  Not smaller: at 32 MiB the cache equals the dirty limit
# (BUF_DIRTY_LIMIT) and a large file write fills it with dirty lines until new lines cannot be had.  A test image only.
#   make ZEDBSD_CONFIG=plan/ws299/tests/config-qemu-cache128.mk BUILD=<dir> disk-image
include plan/tools/posix/config-amd64-serial.mk
CONFIG_BUF_CACHE_KIB := 131072
