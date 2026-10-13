# ws215: the Raspberry Pi 4's Bluetooth test image: the CI's Pi 4 config (config/ci/config-rpi4.mk, which has bluetoothd, bt
# and the input bridge by default) with the HCI node probe bt-probe.  A test image only.
#   make -o <worktree>/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=plan/ws215/tests/config-rpi4-bt.mk ...
include config/ci/config-rpi4.mk
ZEDBSD_USER_PROGRAMS += bt-probe
