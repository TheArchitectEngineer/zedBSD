# ws231-p001: the image T1 boots on QEMU q35 to test the AHCI driver.  The CI amd64 image
# (config/ci/config-amd64.mk, with sshd, the kei account and DHCP) with the AHCI driver on even
# when the release default is switched off.  Build:
#   plan/tools/guest/test-image.sh plan/ws231/tests/config-amd64-ahci.mk build/ws231-ahci
include config/ci/config-amd64.mk

CONFIG_DRIVER_PCI_AHCI := y
