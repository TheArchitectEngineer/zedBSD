# ws136-p003: the full guest image for the tests that need the whole userland (clang, make, sshd, the libraries): the CI
# image (config/ci/config-amd64.mk) booting to the text console, with the guest harness's files added by
# plan/tools/guest/test-image.sh.  It takes the place of the earlier "full HAL guest" image of another tree
# (/home/awe/zedBSD-rpi4/build/ws053-full-hal-guest, gone) that tools copied and changed.
# Build: plan/tools/guest/build-full-image.sh BUILD (the clang package is built: the test runners T1/T2 build it in their
# own worktree's build/, 2026-10-03 main's permission; an implementing agent asks them for the image).
include config/ci/config-amd64.mk
ZEDBSD_GRAPHICAL_BOOT := n
