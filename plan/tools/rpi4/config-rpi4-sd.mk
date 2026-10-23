# The Pi 4 image for the SD regression of the generic SDHCI core (WS289 p002,
# QEMU raspi4b boot-test).  It is the CI Pi 4 configuration without
# the vendor firmware packages, whose licenses are accepted per cache by the
# user; the SD path needs none of them.
include config/ci/config-rpi4.mk
CONFIG_DRIVER_BCM2711_WIFI := n
ZEDBSD_USER_PROGRAMS := $(filter-out bcm43455-firmware brcmbt-firmware \
	intelbt-firmware rtl8822b-firmware,$(ZEDBSD_USER_PROGRAMS))
