# ws001: a lean amd64 guest for the utility regression (p031 and the phase
# checks).  It is the CI amd64 configuration without the external packages
# that take hours to build in a fresh tree (clang, libcxx, emacs, OpenSSH,
# OpenSSL), without the GPU demos, and with the serial mirror, so that
# plan/tools/guest/serial.py drives it instead of SSH.
include config/ci/config-amd64.mk
CONFIG_PCAT_SERIAL_MIRROR := y
ZEDBSD_USER_PROGRAMS := $(filter-out libcxx emacs clang openssh openssl \
	mview vkdemo zdesktop i915-firmware intelax211-firmware \
	rtl8822b-firmware,$(ZEDBSD_USER_PROGRAMS))
