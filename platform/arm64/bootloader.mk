ifeq ($(ZEDBSD_BOARD),rpi5)
# The Pi 5's bootloader is in the board's EEPROM: the boot partition carries
# only config.txt, the kernel and the device trees (platform/arm64/rpi5-firmware.mk).
RPI5_BOOTLOADER_FILES := $(RPI5_BOOT_FIRMWARE_CACHE_FILES) $(BUILD)/config.txt

.PHONY: bootloader
bootloader: $(RPI5_BOOTLOADER_FILES)
else
# The boot partition's firmware (platform/arm64/firmware.mk) and config.txt.
# The device tree is also put beside the image, where the QEMU boot of the
# image (plan/tools/boot-test.sh) finds it with the kernel.
RPI4_FIRMWARE_FILES := $(RPI4_BOOT_FIRMWARE_CACHE_FILES) \
	$(BUILD)/bcm2711-rpi-4-b.dtb \
	$(BUILD)/config.txt

.PHONY: bootloader
bootloader: $(RPI4_FIRMWARE_FILES)

$(BUILD)/bcm2711-rpi-4-b.dtb: $(RPI4_BOOT_FIRMWARE_DTB)
	@mkdir -p $(dir $@)
	cp $< $@
endif
