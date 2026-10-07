# ws083-p004: the QEMU (Venus) regression image of design §8.2: the zdesktop image with vkvideo-probe, so the
# probe's --list shows no video family and no video extension on Venus, and vkdemo and the compositor run as before.
include plan/ws035/tests/config-amd64-zdesktop.mk
ZEDBSD_USER_PROGRAMS += vkvideo-probe
