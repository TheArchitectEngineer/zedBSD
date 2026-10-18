DISK_IMAGE_ARTIFACT := $(BUILD)/hdd-image.img

.PHONY: disk-image check-disk-image
disk-image: world bootloader $(DISK_IMAGE_ARTIFACT)

check-disk-image: $(DISK_IMAGE_ARTIFACT)
	$(PYTHON) platform/sparcv9/tools/check-sparcv9-hdd-image.py \
		--stage1 $(BUILD)/boot/stage1.bin --stage2 $(BUILD)/boot/stage2.bin \
		--kernel $(BUILD)/vmunix --ufs-root $(BUILD)/ufs-root.img $<
