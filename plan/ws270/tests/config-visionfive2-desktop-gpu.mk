# WS270 p006a (2b): the VisionFive 2 desktop with graphical login on the PowerVR GPU.
# The desktop programs of config-visionfive2-desktop.mk, plus the PowerVR firmware,
# gpu-selftest (run by hand, /bin/gpu-selftest) and vkmatrix.  The firmware's
# license was agreed by the user; the build reads the accepted cache under
# build/sources/firmware/powervr.
include plan/ws270/tests/config-visionfive2-desktop.mk

ZEDBSD_GRAPHICAL_LOGIN := y
ZEDBSD_USER_PROGRAMS += mount sh gpu-selftest vkmatrix powervr-firmware
