# ws158-p003: the image of the compositor's translations on the Venus guest: the Languages image (the Files image with
# the input method, Settings and keiland-settings), whose libkeiland package brings the catalogs
# (/usr/share/keiland/locale/ja/wayland.tr).  A test image only.  Build:
#   plan/tools/guest/test-image.sh plan/ws158/tests/config-amd64-tr.mk BUILD \
#     --file /usr/share/keiland/wallpaper.png=userland/desktop/wallpapers/Birch-Lake.png
include plan/ws154/tests/config-amd64-languages.mk
