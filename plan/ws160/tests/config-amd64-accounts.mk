# ws160-p001: the SSH guest image (plan/tools/guest/config-amd64-ssh.mk) with passwd, su and sudo (set-user-ID root).
# A test image only.  Build:
#   plan/tools/guest/test-image.sh plan/ws160/tests/config-amd64-accounts.mk BUILD
include plan/tools/guest/config-amd64-ssh.mk
ZEDBSD_USER_PROGRAMS += passwd su sudo
