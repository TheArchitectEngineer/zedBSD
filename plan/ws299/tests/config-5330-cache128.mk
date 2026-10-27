# ws299-p001: the 5330 kernel with a 128 MiB buffer cache instead of an eighth of memory, to see whether the read
# slowdown moves with the cache's cap (plan/ws299/phase001/phase.md).  A measurement kernel only.
#   make ZEDBSD_CONFIG=plan/ws299/tests/config-5330-cache128.mk BUILD=<dir> vmunix
include plan/tools/hw5330/config-5330.mk
CONFIG_BUF_CACHE_KIB := 131072
