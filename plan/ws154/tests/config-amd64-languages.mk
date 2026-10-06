# ws154 (Languages and SKK): the lean zdesktop guest image for the Venus tests (the Files image,
# plan/tools/files/config-amd64-files.mk) with the input method, both dictionaries, the test client, Settings and the
# settings tool.  Build (the result is BUILD/hdd-image.img):
#   plan/tools/guest/test-image.sh plan/ws154/tests/config-amd64-languages.mk BUILD \
#     --file /usr/share/keiland/wallpaper.png=userland/desktop/wallpapers/Birch-Lake.png
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += keiland-ime ime-dict-ja ime-dict-skk ime-probe settings keiland-settings
