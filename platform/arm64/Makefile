# Raspberry Pi 4 and Raspberry Pi 5 build composition.
# The boot partition's firmware cache, whose files the SD image carries.
ifeq ($(ZEDBSD_BOARD),rpi5)
include platform/arm64/rpi5-firmware.mk
ARM64_BOOT_FIRMWARE_DIR := $(RPI5_BOOT_FIRMWARE_DIR)
ARM64_BOOT_FIRMWARE_CACHE_FILES := $(RPI5_BOOT_FIRMWARE_CACHE_FILES)
else
include platform/arm64/firmware.mk
ARM64_BOOT_FIRMWARE_DIR := $(RPI4_BOOT_FIRMWARE_DIR)
ARM64_BOOT_FIRMWARE_CACHE_FILES := $(RPI4_BOOT_FIRMWARE_CACHE_FILES)
endif
include platform/arm64/vmunix.mk
include platform/arm64/bootloader.mk
include platform/arm64/rootfs.mk
include platform/arm64/disk-image.mk
include platform/arm64/run.mk
include platform/arm64/toolchain.mk
