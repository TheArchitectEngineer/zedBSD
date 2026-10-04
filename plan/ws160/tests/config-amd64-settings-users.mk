# ws160-p002: the Settings guest image (plan/ws089/tests/config-amd64-settings.mk) with passwd, su and sudo, so that the
# Users page's password change goes through the compositor to passwd.  A test image only.  Build:
#   SETTINGS_CONFIG=plan/ws160/tests/config-amd64-settings-users.mk plan/ws089/tests/build-settings-image.sh BUILD
include plan/ws089/tests/config-amd64-settings.mk
ZEDBSD_USER_PROGRAMS += passwd su sudo
