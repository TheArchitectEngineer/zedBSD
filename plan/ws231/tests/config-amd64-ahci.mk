# ws231-p001: the image T1 boots on QEMU q35 to test the AHCI driver.  The SSH guest image
# (plan/tools/guest/config-amd64-ssh.mk) with the AHCI driver on even when the release default is switched
# off, and sleepctl for the S0 idle (devices mode) round trip.  Build:
#   plan/tools/guest/test-image.sh plan/ws231/tests/config-amd64-ahci.mk build/ws231-ahci
include plan/tools/guest/config-amd64-ssh.mk
ZEDBSD_USER_PROGRAMS += sleepctl

CONFIG_DRIVER_PCI_AHCI := y
