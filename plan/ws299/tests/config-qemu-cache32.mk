# ws299: the QEMU serial test image with a 32 MiB buffer cache, the size at which the cache equals the old fixed
# dirty bound (qemu-cache32-write.sh).  A test image only.
#   make ZEDBSD_CONFIG=plan/ws299/tests/config-qemu-cache32.mk BUILD=<dir> disk-image
include plan/tools/posix/config-amd64-serial.mk
CONFIG_BUF_CACHE_KIB := 32768
