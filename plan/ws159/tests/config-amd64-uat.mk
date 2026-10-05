# ws159-p005: the image of the Latitude 5330 UAT of 2026-10-05 (the touch pad's interrupt and rules, the battery and
# the system's events, the Windows key, the network's details, su/sudo/passwd and the Users page, About).  The release's
# configuration (config/release/config-amd64-beta1.mk: root locked, kei the user with the password "kei", sshd on)
# built as a development build (uname's release with the source's revision), with the reader of the system's events.
# Build (Q1 with the user, from the latest main):
#   plan/tools/guest/test-image.sh --no-harness plan/ws159/tests/config-amd64-uat.mk build/uat-0505
# and write build/uat-0505/hdd-image.img to the USB stick; the procedure is plan/ws159/phase005/phase.md.
include config/release/config-amd64-beta1.mk
ZEDBSD_RELEASE_BUILD := n
ZEDBSD_USER_PROGRAMS += systemevents sleepctl phone calendar mailer
