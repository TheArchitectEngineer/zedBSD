# ws113-p002: the image of the GPU scanout rule's checks: the CI image's configuration without the target clang and
# libc++ (which a subagent does not build), with the display inventory probe (userland/tests/display-inventory).
#   make ZEDBSD_CONFIG=plan/ws113/tests/config-amd64-p002.mk BUILD=build/ws113-p002
include plan/ws129/tests/config-amd64-ci-noclang.mk
ZEDBSD_USER_PROGRAMS += display-inventory
