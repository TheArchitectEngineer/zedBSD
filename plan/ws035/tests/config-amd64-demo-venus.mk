# ws035-p116: the demonstration's walk-through on the Venus guest.  The graphical login image
# (config-amd64-graphical.mk: the lean files image with PDF Viewer, the greeter, logo, kmsg=quiet,
# login=graphical) with Notes (WS079) and, from the build script, App Home's demonstration list
# (plan/ws035/demo/apps.conf) as the i915 demo image has it (plan/ws075/demo/config-demo-hdmi.mk).  Build:
#   plan/ws035/tests/build-demo-venus-image.sh [BUILD]
include plan/ws035/tests/config-amd64-graphical.mk
ZEDBSD_USER_PROGRAMS += notes
