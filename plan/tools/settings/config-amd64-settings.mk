# ws135: the Settings image with the input method (plan/ws089/tests/config-amd64-settings-ime.mk) and the settings probe
# keiland-settings (userland/tests/keiland-settings) for the tests of WS135.
include plan/ws089/tests/config-amd64-settings-ime.mk
ZEDBSD_USER_PROGRAMS += keiland-settings
