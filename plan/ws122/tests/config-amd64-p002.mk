# ws122-p002: the guest image of the video player's test (videoplayer-p002.sh): the Files image
# (plan/tools/files/config-amd64-files.mk, which has audiod and the HDA driver) with FFmpeg's libraries
# (multimedia/libavcodec) and the player.
#   FILES_CONFIG=plan/ws122/tests/config-amd64-p002.mk \
#   FILES_EXTRA='--file /usr/share/videoplayer-tests/sample.mp4=plan/ws122/tests/sample.mp4' \
#   plan/tools/files/build-files-image.sh BUILD
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += libavcodec videoplayer
