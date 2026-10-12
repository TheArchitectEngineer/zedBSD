# ws204-p003/p004: the UAT image (config/current-uat.mk) with audio-loop, which plays a tone through audiod while it
# records and measures the recording (userland/tests/audio-loop).  QEMU: -device intel-hda -device hda-duplex; the
# Latitude 5330: its HD Audio codec, the speaker heard by the internal microphone.
include config/current-uat.mk
ZEDBSD_USER_PROGRAMS += audio-loop
