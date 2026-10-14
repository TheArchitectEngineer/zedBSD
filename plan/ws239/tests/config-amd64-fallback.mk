# ws239-p002: the image of the graphical login's fallback test: the CI configuration (graphical boot: the logo,
# kmsg=quiet, login=graphical, sessiond and the compositor) as it is.  The failing compositor of cases B and C is
# copied over /bin/wayland as an individual file (plan/ws239/phase001/phase.md, "T procedure"):
#   plan/tools/guest/test-image.sh plan/ws239/tests/config-amd64-fallback.mk BUILD
#   plan/tools/guest/test-image.sh plan/ws239/tests/config-amd64-fallback.mk BUILD \
#       --file /bin/wayland=plan/ws239/tests/wayland-exit.sh --mode /bin/wayland=0755
include config/ci/config-amd64.mk
