# ws136-p003: the image of plan/ws031/tests/vkloop-hw.sh's runs without a mode word (vkdemo through the vkprobe service
# chain on the i915 passthrough of the 5330).  They once used the tree's own config.mk, which a worktree does not have;
# a test's image is its WS's config (2026-10-04 user): the zdesktop image of the same machine with vkdemo.
include plan/ws031/tests/config-zdesktop-hw.mk
ZEDBSD_USER_PROGRAMS += vkdemo
