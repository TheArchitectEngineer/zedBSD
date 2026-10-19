# zedBSD riscv64: the UEFI disk image.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# A GPT disk whose EFI system partition holds the loader at the firmware's
# default path, zedbsd.cfg, the kernel and the root image, which the loader
# hands to the kernel as its initial ramdisk.
RISCV64_DISK_IMAGE_TOOL := platform/riscv64/tools/make-riscv64-disk-image.py

.PHONY: disk-image
disk-image: $(BUILD)/disk.img

$(BUILD)/disk.img: $(BUILD)/uefi/BOOTRISCV64.EFI $(BUILD)/vmunix \
	$(BUILD)/root.img platform/riscv64/zedbsd.cfg $(RISCV64_DISK_IMAGE_TOOL)
	$(PYTHON) $(RISCV64_DISK_IMAGE_TOOL) --force --efi $(BUILD)/uefi/BOOTRISCV64.EFI \
 --kernel $(BUILD)/vmunix --config platform/riscv64/zedbsd.cfg \
 --root-image $(BUILD)/root.img $@
