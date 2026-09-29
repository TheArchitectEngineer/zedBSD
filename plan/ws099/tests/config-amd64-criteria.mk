# ws099-p001: the Venus guest image the compositor's criteria (plan/ws099/ws.md, C1..C10) are checked on: the
# graphical login image (plan/ws035/tests/config-amd64-graphical.mk: the lean files image with PDF Viewer, the test
# greeter, login=graphical) with Notes (C3's swipe) and Settings; the build script adds the generated wallpapers (C7).
# Build:
#   plan/ws099/tests/build-criteria-image.sh [BUILD]
include plan/ws035/tests/config-amd64-graphical.mk
ZEDBSD_USER_PROGRAMS += notes settings
