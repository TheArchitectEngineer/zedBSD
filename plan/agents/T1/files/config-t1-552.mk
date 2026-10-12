# T1-552: the full guest image plus clang (the CI configuration no longer lists it).
include plan/tools/guest/config-amd64-full.mk
ZEDBSD_USER_PROGRAMS += libcxx clang
