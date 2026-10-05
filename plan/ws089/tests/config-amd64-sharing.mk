# ws089-p025 (T1-159): the graphical login image (plan/ws035/tests/config-amd64-graphical.mk: the machine boots to
# the test greeter, which starts kei's session) with su, for settings-p025.sh to start Settings as kei in that
# session.  Build (the result is BUILD/hdd-image.img):
#   SETTINGS_CONFIG=plan/ws089/tests/config-amd64-sharing.mk plan/ws089/tests/build-settings-image.sh BUILD
include plan/ws035/tests/config-amd64-graphical.mk
ZEDBSD_USER_PROGRAMS += su
