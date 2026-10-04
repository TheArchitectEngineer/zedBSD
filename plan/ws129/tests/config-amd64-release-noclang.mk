# ws129-p004: the release image's configuration (config/release/config-amd64-beta1.mk) without the target clang
# and libc++, which a subagent does not build (AGENTS.md, the toolchain rule): the image the local build and T1's
# checks use (root locked, uname's release VERSION itself, the old installer out, Emacs in).
#   make ZEDBSD_CONFIG=plan/ws129/tests/config-amd64-release-noclang.mk BUILD=build/ws129-p004 \
#        ZEDBSD_EXTRA_INPUTS=$PWD/build/ws129-p004/INDEX \
#        ZEDBSD_EXTRA_FILES="--file /usr/share/licenses/INDEX=$PWD/build/ws129-p004/INDEX"
include config/release/config-amd64-beta1.mk
ZEDBSD_USER_PROGRAMS := $(filter-out clang libcxx,$(ZEDBSD_USER_PROGRAMS))
