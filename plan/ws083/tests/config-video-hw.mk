# ws083-p003b: the i915 test build on the passthrough of the 5330 (plan/ws075/tests/config-test-hw.mk) booted with
# i915.debug=video, so a GT with VCS0 offers Vulkan video decode (the capset's 176-byte native word and the video
# queue family).  Used with plan/ws075/tests/test-hw.sh as ZEDBSD_CONFIG.
include plan/ws075/tests/config-test-hw.mk
ZEDBSD_BOOT_EXTRA_LINES := i915.debug=video
# ws083-p004/p005: vkvideo-probe decodes the test streams (copy plan/ws083/tests/streams/*.h264 and *.sha256 in).
ZEDBSD_USER_PROGRAMS += vkvideo-probe
