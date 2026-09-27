# ws035-p094〜: the lean zdesktop guest image (plan/tools/files/config-amd64-files.mk) with the graphical
# login's test greeter (greeter-probe).  sessiond comes with zdesktop.  Build:
#   plan/ws035/tests/build-login-image.sh [BUILD]
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += greeter-probe
