# ws156-p002: the zdesktop image (plan/ws035/tests/config-amd64-zdesktop.mk) with the notification client
# keiland-notify (userland/tests/keiland-notify) for the tests of the applications' notifications.
include plan/ws035/tests/config-amd64-zdesktop.mk
ZEDBSD_USER_PROGRAMS += keiland-notify
