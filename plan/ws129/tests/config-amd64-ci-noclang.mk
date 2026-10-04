# ws129-p002: the CI image's configuration without the target clang and libc++, which a subagent does not build
# (AGENTS.md, the toolchain rule): the root filesystem the license check (license-inventory.py --rootfs) reads.
#   make ZEDBSD_CONFIG=plan/ws129/tests/config-amd64-ci-noclang.mk BUILD=build/p2-ci build/p2-ci/rootfs/.stamp
include config/ci/config-amd64.mk
ZEDBSD_USER_PROGRAMS := $(filter-out clang libcxx,$(ZEDBSD_USER_PROGRAMS))
