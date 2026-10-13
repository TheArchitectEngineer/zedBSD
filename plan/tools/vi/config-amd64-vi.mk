# ws228-p002: the amd64 guest T1 edits files with vi on.  It is the CI amd64
# configuration without the external packages that take hours to build in a
# fresh tree, without the GPU demos, and with the serial mirror so that
# plan/tools/guest/serial.py drives it; vi is added, since the CI
# configuration lists its programs and does not name vi yet.
include config/ci/config-amd64.mk
CONFIG_PCAT_SERIAL_MIRROR := y
ZEDBSD_USER_PROGRAMS := $(filter-out libcxx emacs clang openssh openssl \
	mview vkdemo zdesktop i915-firmware intelax211-firmware \
	rtl8822b-firmware,$(ZEDBSD_USER_PROGRAMS)) vi
