# ws089-p017b: the image of the dark appearance's applications (settings-p017b.sh): the Settings image
# (config-amd64-settings.mk, with keiland-settings) with the applications that draw in the appearance.  A test image only.
# Build:
#   plan/tools/guest/test-image.sh plan/ws089/tests/config-amd64-dark-apps.mk BUILD
include plan/ws089/tests/config-amd64-settings.mk
ZEDBSD_USER_PROGRAMS += textedit notes imageview phone calendar mailer
