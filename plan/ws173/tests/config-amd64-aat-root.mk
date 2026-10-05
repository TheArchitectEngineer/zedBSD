# ws173-p004: the AAT image (plan/tools/aat/config-amd64-aat.mk) with root's password not locked, for a QEMU run of the
# runner when the guest harness's key cannot reach a locked root (os.accounts.su-root-locked then fails, as expected).
# A test image only.  Build:
#   plan/tools/guest/test-image.sh plan/ws173/tests/config-amd64-aat-root.mk BUILD
include plan/tools/aat/config-amd64-aat.mk
ZEDBSD_ROOT_LOCKED := n
