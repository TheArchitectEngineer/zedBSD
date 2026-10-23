# zedBSD armv7: the console root the kernel reads from its initial ramdisk.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# The root tree holds the programs of platform/armv7/userland.mk, the
# runtime, the accounts and init's configuration.  Of init's service
# definitions it carries the console's getty and the two it is ordered
# after: the system log daemon, and networking, which init passes over
# here since the network daemon it requires is not in the root.
ARMV7_ROOT_SERVICES := getty_console networking syslogd
ARMV7_ROOT_DATA := /etc/rc.conf=userland/base/etc/rc.conf \
	/etc/fstab=userland/base/etc/fstab \
	$(foreach service,$(ARMV7_ROOT_SERVICES),\
 /etc/service.d/$(service)=userland/base/init/services/$(service))
# OpenSSH's programs also load libutil.  The packages a configuration names
# (ZEDBSD_USER_PROGRAMS) go into the tree with their own files.
ARMV7_ROOT_RUNTIME := ld.so libc.so \
	$(if $(filter openssh,$(ZEDBSD_USER_PROGRAMS)),libutil.so)

ZEDBSD_ROOTFS_INPUTS := $(addprefix $(BUILD)/bin/,$(ARMV7_ROOT_COMMANDS)) \
	$(addprefix $(DYNAMIC_DIR)/,$(ARMV7_ROOT_RUNTIME)) \
	$(ZEDBSD_ACCOUNT_INPUTS) \
	$(foreach entry,$(ARMV7_ROOT_DATA),$(word 2,$(subst =, ,$(entry))))
ZEDBSD_ROOTFS_FILES := $(foreach command,$(ARMV7_ROOT_COMMANDS),\
 --file $(if $(filter umount,$(command)),/sbin/umount,$(call zedbsd_userland_destination,$(command)))=$(BUILD)/bin/$(command)) \
	$(foreach command,$(ARMV7_ROOT_COMMANDS),$(if $(USERLAND_$(command)_MODE),\
 --mode $(call zedbsd_userland_destination,$(command))=$(USERLAND_$(command)_MODE))) \
	$(foreach library,$(ARMV7_ROOT_RUNTIME),\
 --file /lib/$(library)=$(DYNAMIC_DIR)/$(library)) \
	$(foreach entry,$(ARMV7_ROOT_DATA),--file $(entry) \
 --mode $(word 1,$(subst =, ,$(entry)))=0644) \
	$(ZEDBSD_ACCOUNT_FILES)
$(eval $(call ZEDBSD_ROOTFS_TREE_RULE,armv7,$(ZEDBSD_ROOTFS_INPUTS),$(ZEDBSD_ROOTFS_FILES)))

.PHONY: rootfs root-image
rootfs: $(BUILD)/rootfs/.stamp

# The root disk: one MBR partition holding the UFS root made from the tree
# (platform/armv7/tools/make-armv7-root-image.py).  It is loaded over JTAG
# at about 250 KiB/s, so it is no larger than the tree and the files init,
# the log and the logins write need.
# The debugging information of the programs is left out of it: the
# external packages are built with it (OpenSSH's programs are three times
# their size with it), and nothing reads it on the board.
ARMV7_ROOT_MIB ?= 4
ARMV7_STRIP := $(ZEDBSD_TARGET_LLVM_BIN)/llvm-strip
$(BUILD)/root.img: $(BUILD)/rootfs/.stamp $(ARMV7_ROOT_IMAGE_TOOL) \
	tools/build/ufs_format.py tools/build/check-ufs-image.py
	find $(BUILD)/rootfs -type f -exec sh -c 'for file; do \
 magic=$$(head -c 4 "$$file" | od -An -tx1 | tr -d " "); \
 if test "$$magic" = 7f454c46; then $(ARMV7_STRIP) --strip-debug "$$file"; fi; \
 done' sh {} +
	$(PYTHON) $(ARMV7_ROOT_IMAGE_TOOL) --ufs-size-mib $(ARMV7_ROOT_MIB) \
 --tree $(BUILD)/rootfs $@
root-image: $(BUILD)/root.img

# The external packages the ZYBO's root is to carry for the network login
# (WS289): built here on their own, and put into the root only by a
# configuration that names them (plan/ws289/tests/config-zybo-ssh.mk).
.PHONY: armv7-packages
armv7-packages: zlib openssl openssh ca-certificates
