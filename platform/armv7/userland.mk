# zedBSD armv7: the C library, the runtime linker and the ABI test's root.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# The programs are position-independent executables that load
# /lib/libc.so through /lib/ld.so, as on riscv64.  Every module's TLS is
# dynamic (general-dynamic, the traditional __tls_get_addr form; ARM has
# no other in this toolchain), since the kernel gives no image a static
# TLS block on this processor and the loader lays none out.  The build
# flags are the sysroot's (toolchain/llvm/sysroot.mk), position
# independent.
ARMV7_SYSROOT := $(ZEDBSD_SYSROOT_ARMV7)
ARMV7_USER_CC := $(ZEDBSD_TARGET_LLVM_BIN)/clang \
	--target=armv7-unknown-zedbsd-eabihf --sysroot=$(ARMV7_SYSROOT)
ARMV7_USER_AR := $(ZEDBSD_TARGET_LLVM_BIN)/llvm-ar
ARMV7_USER_ARCH_FLAGS := $(ZEDBSD_SYSROOT_ARMV7_CODE_FLAGS)
ARMV7_USER_LDFLAGS := -m armelf_linux_eabi -z max-page-size=4096

DYNAMIC_DIR := $(BUILD)/dynamic
DYNAMIC_CPPFLAGS := -nostdinc -I. -Iinclude \
	-isystem $(ARMV7_SYSROOT)/usr/include \
	-DHAL_ARCH_ARMV7 -DKERN_USER_ABI_ARMV7 -DKERN_DYNAMIC_LIBC
DYNAMIC_CFLAGS := $(ARMV7_USER_ARCH_FLAGS) -Os -ffreestanding \
	-fPIC -fno-builtin -fno-stack-protector \
	-fno-asynchronous-unwind-tables -fno-unwind-tables \
	-ftls-model=global-dynamic -Wall -Wextra -Werror
DYNAMIC_LIBC_SOURCES := userland/base/libc/posix.c userland/base/libc/poll.c \
	userland/base/libc/termios.c userland/base/libc/pthread.c userland/base/libc/timer.c userland/base/libc/shm.c \
	userland/base/libc/semaphore.c userland/base/libc/mqueue.c userland/base/libc/dlfcn.c \
	userland/base/libc/socket.c userland/base/libc/resolver.c \
	userland/base/libc/resolver-dns.c userland/base/libc/signal.c \
	userland/base/libc/account.c userland/base/libc/crypt.c userland/base/libc/utmpx.c src/libc/heap.c \
	src/libc/string.c src/libc/ctype.c src/libc/locale.c src/libc/wide.c src/libc/int64.c \
	src/libc/strto.c src/libc/format.c \
	src/libc/stdio.c $(ZEDBSD_LIBC_USER_EXTRA_SOURCES)
DYNAMIC_LIBC_OBJS := $(patsubst %.c,$(DYNAMIC_DIR)/obj/%.o,\
	$(DYNAMIC_LIBC_SOURCES)) $(DYNAMIC_DIR)/obj/userland/base/libc/syscall.o \
	$(DYNAMIC_DIR)/obj/src/libc/setjmp-armv7.o
DYNAMIC_RTLD_OBJS := $(DYNAMIC_DIR)/obj/src/rtld/entry.o \
	$(DYNAMIC_DIR)/obj/src/rtld/rtld.o \
	$(DYNAMIC_DIR)/obj/src/rtld/string.o

# The mathematics and the number parsing are part of the C library.  The
# floating-point unit does the arithmetic, so the compiler runtime's
# software operations (src/libc/compiler-runtime.c) are left out; the
# conversions it would also name come from the run-time ABI's helpers
# below.  The number parsing still rounds with the software core.
DYNAMIC_FLOAT_DIR := $(DYNAMIC_DIR)/float
DYNAMIC_LIBM_OBJS := $(patsubst src/libc/math/%.c,\
	$(DYNAMIC_FLOAT_DIR)/math/%.o,$(ZEDBSD_LIBM_SOURCES))
DYNAMIC_FLOAT_PARSE_OBJS := $(DYNAMIC_FLOAT_DIR)/softfloat.o \
	$(DYNAMIC_FLOAT_DIR)/float-parse.o
DYNAMIC_LIBC_OBJS += $(DYNAMIC_LIBM_OBJS) $(DYNAMIC_FLOAT_PARSE_OBJS)

# The run-time ABI's helpers (the divisions, the 64-bit conversions and
# the memory helpers, toolchain/llvm/sysroot.mk) built position
# independent from the same LLVM sources as the sysroot's archive.  The C
# library takes the members it calls and exports them, so a program finds
# them there; the loader takes its own copies and keeps them to itself.
DYNAMIC_BUILTIN_SOURCES := $(ZEDBSD_SYSROOT_LLVM_BUILTIN_SOURCES) \
	$(ZEDBSD_SYSROOT_ARMV7_LLVM_BUILTIN_SOURCES)
DYNAMIC_BUILTIN_DIR := $(DYNAMIC_DIR)/builtins
DYNAMIC_BUILTIN_OBJS := $(addprefix $(DYNAMIC_BUILTIN_DIR)/,\
	$(addsuffix .o,$(notdir $(DYNAMIC_BUILTIN_SOURCES))))
DYNAMIC_BUILTIN_ARCHIVE := $(DYNAMIC_DIR)/libbuiltins-pic.a
ARMV7_BUILTIN_SOURCE_DIR := $(ZEDBSD_LLVM_SOURCE)/compiler-rt/lib/builtins

$(DYNAMIC_DIR)/obj/%.o: %.c $(ARMV7_SYSROOT)/.zedbsd-sysroot-complete
	@mkdir -p $(dir $@)
	$(ARMV7_USER_CC) $(DYNAMIC_CPPFLAGS) $(DYNAMIC_CFLAGS) -MMD -MP -c $< -o $@
$(DYNAMIC_DIR)/obj/userland/base/libc/syscall.o: \
	userland/base/libc/syscall-armv7.S include/hal/arch.h \
	include/hal/arch/armv7.h
	@mkdir -p $(dir $@)
	$(ARMV7_USER_CC) $(ARMV7_USER_ARCH_FLAGS) $(DYNAMIC_CPPFLAGS) -c $< -o $@
$(DYNAMIC_DIR)/obj/src/libc/setjmp-armv7.o: src/libc/setjmp-armv7.S
	@mkdir -p $(dir $@)
	$(ARMV7_USER_CC) $(ARMV7_USER_ARCH_FLAGS) $(DYNAMIC_CPPFLAGS) -D_ASM_SRC_ -c $< -o $@
$(DYNAMIC_DIR)/obj/src/rtld/entry.o: src/rtld/entry-armv7.S
	@mkdir -p $(dir $@)
	$(ARMV7_USER_CC) $(ARMV7_USER_ARCH_FLAGS) -fPIC -c $< -o $@
$(DYNAMIC_LIBM_OBJS): $(DYNAMIC_FLOAT_DIR)/math/%.o: src/libc/math/%.c \
	$(ZEDBSD_LIBM_HEADERS) $(ARMV7_SYSROOT)/.zedbsd-sysroot-complete
	@mkdir -p $(dir $@)
	$(ARMV7_USER_CC) $(DYNAMIC_CPPFLAGS) $(DYNAMIC_CFLAGS) -c $< -o $@
$(DYNAMIC_FLOAT_PARSE_OBJS): $(DYNAMIC_FLOAT_DIR)/%.o: src/libc/%.c \
	src/libc/softfloat.h $(ARMV7_SYSROOT)/.zedbsd-sysroot-complete
	@mkdir -p $(dir $@)
	$(ARMV7_USER_CC) $(DYNAMIC_CPPFLAGS) $(DYNAMIC_CFLAGS) -c $< -o $@
$(DYNAMIC_BUILTIN_DIR)/%.c.o: $(ARMV7_BUILTIN_SOURCE_DIR)/%.c \
	$(ARMV7_SYSROOT)/.zedbsd-sysroot-complete
	@mkdir -p $(dir $@)
	$(ARMV7_USER_CC) $(ARMV7_USER_ARCH_FLAGS) -DHAL_ARCH_ARMV7 -DKERN_USER_ABI_ARMV7 \
 $(ZEDBSD_SYSROOT_ARMV7_LLVM_BUILTIN_FLAGS) -nostdinc -isystem $(ARMV7_SYSROOT)/usr/include \
 -I$(ARMV7_BUILTIN_SOURCE_DIR) -ffreestanding -fno-builtin -fPIC -fno-stack-protector \
 -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-common -fno-strict-aliasing \
 -Os -Wall -Wextra -Werror -Wno-unused-parameter -c $< -o $@
$(DYNAMIC_BUILTIN_DIR)/%.c.o: $(ARMV7_BUILTIN_SOURCE_DIR)/arm/%.c \
	$(ARMV7_SYSROOT)/.zedbsd-sysroot-complete
	@mkdir -p $(dir $@)
	$(ARMV7_USER_CC) $(ARMV7_USER_ARCH_FLAGS) -DHAL_ARCH_ARMV7 -DKERN_USER_ABI_ARMV7 \
 $(ZEDBSD_SYSROOT_ARMV7_LLVM_BUILTIN_FLAGS) -nostdinc -isystem $(ARMV7_SYSROOT)/usr/include \
 -I$(ARMV7_BUILTIN_SOURCE_DIR) -ffreestanding -fno-builtin -fPIC -fno-stack-protector \
 -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-common -fno-strict-aliasing \
 -Os -Wall -Wextra -Werror -Wno-unused-parameter -c $< -o $@
$(DYNAMIC_BUILTIN_DIR)/%.S.o: $(ARMV7_BUILTIN_SOURCE_DIR)/arm/%.S \
	$(ARMV7_SYSROOT)/.zedbsd-sysroot-complete
	@mkdir -p $(dir $@)
	$(ARMV7_USER_CC) $(ARMV7_USER_ARCH_FLAGS) $(ZEDBSD_SYSROOT_ARMV7_LLVM_BUILTIN_FLAGS) \
 -I$(ARMV7_BUILTIN_SOURCE_DIR) -fPIC -c $< -o $@
$(DYNAMIC_BUILTIN_ARCHIVE): $(DYNAMIC_BUILTIN_OBJS)
	@mkdir -p $(dir $@)
	$(ARMV7_USER_AR) rcsD $@ $(DYNAMIC_BUILTIN_OBJS)

# The loader relocates itself before anything else runs, so it carries
# only relative relocations; the C library leaves the loader's private
# entry points to be found in the loader at run time.  The loader's
# helpers come from the archive with their symbols kept local.
$(DYNAMIC_DIR)/ld.so: $(DYNAMIC_RTLD_OBJS) $(DYNAMIC_BUILTIN_ARCHIVE) \
	tools/build/check-dynamic-elf.py
	$(ZEDBSD_TARGET_LLVM_BIN)/ld.lld $(ARMV7_USER_LDFLAGS) -shared -Bsymbolic -e _rtld_start \
 --hash-style=sysv -z now -z relro -z separate-code --exclude-libs ALL \
 $(DYNAMIC_RTLD_OBJS) $(DYNAMIC_BUILTIN_ARCHIVE) -o $@
	$(PYTHON) tools/build/check-dynamic-elf.py --machine armv7 --role interpreter $@
$(DYNAMIC_DIR)/libc.so: $(DYNAMIC_LIBC_OBJS) $(DYNAMIC_BUILTIN_ARCHIVE) \
	tools/build/check-dynamic-elf.py
	$(ZEDBSD_TARGET_LLVM_BIN)/ld.lld $(ARMV7_USER_LDFLAGS) -shared -soname libc.so --hash-style=both \
 -z now -z relro -z separate-code -z stack-size=0x100000 \
 --allow-shlib-undefined $(DYNAMIC_LIBC_OBJS) $(DYNAMIC_BUILTIN_ARCHIVE) -o $@
	$(PYTHON) tools/build/check-dynamic-elf.py --machine armv7 --role libc $@

# libutil (openpty, forkpty, login_tty): the library the external packages
# link beside the C library (OpenSSH's server, userland/packages/external.mk).
$(eval $(call ZEDBSD_LIBUTIL_RULE,$(DYNAMIC_DIR),$(ZEDBSD_TARGET_LLVM_BIN)/ld.lld,$(ARMV7_USER_LDFLAGS),armv7))

# A program is crt1, its own objects and the shared C library.
ARMV7_APP_INPUTS := $(ARMV7_SYSROOT)/usr/lib/crt1.o \
	$(DYNAMIC_DIR)/libc.so $(DYNAMIC_DIR)/ld.so \
	tools/build/check-dynamic-elf.py
ARMV7_APP_LINK = $(ARMV7_USER_CC) $(ARMV7_USER_ARCH_FLAGS) -nostdlib -pie \
 -Wl,--gc-sections \
 -Wl,--hash-style=sysv,-z,now,-z,relro,-z,separate-code \
 -Wl,-z,stack-size=0x100000,--allow-shlib-undefined \
 -Wl,--dynamic-linker=/lib/ld.so $(ARMV7_SYSROOT)/usr/lib/crt1.o
ARMV7_APP_LIBS = -L$(DYNAMIC_DIR) -Wl,-rpath-link,$(DYNAMIC_DIR) -l:libc.so

# The ABI test (plan/ws286/tests/abitest.c), run as init from a root that
# holds nothing else: the loader, the C library and the test.
ARMV7_ABITEST_SOURCE := plan/ws286/tests/abitest.c
ARMV7_ABITEST_OBJ := $(DYNAMIC_DIR)/obj/plan/ws286/tests/abitest.o
ARMV7_ROOT_IMAGE_TOOL := platform/armv7/tools/make-armv7-root-image.py

# The test has thread-local variables of its own, which it reaches through
# __tls_get_addr.  The loader defines that function, and no library the
# program links against does, so the link leaves undefined symbols to the
# loader (-z undefs) and the check then requires every one of them to be
# the C library's or one of the loader's own entry points.
$(BUILD)/abitest: $(ARMV7_APP_INPUTS) $(ARMV7_ABITEST_OBJ)
	@mkdir -p $(dir $@)
	$(ARMV7_APP_LINK) -Wl,-z,undefs $(ARMV7_ABITEST_OBJ) $(ARMV7_APP_LIBS) -o $@
	$(PYTHON) tools/build/check-dynamic-elf.py --machine armv7 --role program \
 --provider $(DYNAMIC_DIR)/libc.so $@

$(BUILD)/abi.img: $(BUILD)/abitest $(DYNAMIC_DIR)/ld.so $(DYNAMIC_DIR)/libc.so \
	$(ARMV7_ROOT_IMAGE_TOOL) tools/build/ufs_format.py tools/build/check-ufs-image.py
	$(PYTHON) $(ARMV7_ROOT_IMAGE_TOOL) --dir dev --dir tmp \
 --file lib/ld.so=$(DYNAMIC_DIR)/ld.so --file lib/libc.so=$(DYNAMIC_DIR)/libc.so \
 --file abitest=$(BUILD)/abitest $@

.PHONY: abi-image
abi-image: $(BUILD)/abi.img

# The programs of the console root (platform/armv7/rootfs.mk), the fewest
# that log in on the console and give a shell: init with the system log
# daemon its console login is ordered after and the setup commands it runs
# (mount, swapon), getty, login, the shell, and a few commands, among them
# the ones that check a disk root (cksum, dd, df, rm, sync, time).  The
# board loads the root over JTAG into its 512 MiB, so the root carries
# nothing else; a configuration may name more.  None of them has
# thread-local variables of its own, so each links as an ordinary
# application: every symbol it names is the C library's.
ARMV7_ROOT_COMMANDS ?= cat cksum dd df dmesg echo getty init login ls mount \
	ps reboot rm sh swapon sync syslogd time umount uname
# The script that starts OpenSSH's server makes /var/empty and sets the
# modes of the host keys it makes, so a root with OpenSSH has those commands.
ARMV7_ROOT_COMMANDS += $(if $(filter openssh,$(ZEDBSD_USER_PROGRAMS)),chmod mkdir)
# A configuration adds programs to the default set here (the ZYBO's
# programmable logic tools: plan/ws286/tests/config-zybo-fpga.mk).
ARMV7_ROOT_EXTRA_COMMANDS ?=
ARMV7_ROOT_COMMANDS += $(ARMV7_ROOT_EXTRA_COMMANDS)
ARMV7_APP_OBJ := $(DYNAMIC_DIR)/obj
ARMV7_APP_CHECK = $(PYTHON) tools/build/check-dynamic-elf.py --machine armv7 \
 --role application --needed libc.so

ARMV7_APP_SH_OBJS := $(call ZEDBSD_USERLAND_OBJECTS,$(ARMV7_APP_OBJ),sh) \
	$(ARMV7_APP_OBJ)/userland/base/libedit/readline.o
$(ARMV7_APP_SH_OBJS): DYNAMIC_CPPFLAGS += -Iuserland/base/libedit

$(BUILD)/bin/sh: $(ARMV7_APP_INPUTS) $(ARMV7_APP_SH_OBJS)
	@mkdir -p $(dir $@)
	$(ARMV7_APP_LINK) $(ARMV7_APP_SH_OBJS) $(ARMV7_APP_LIBS) -o $@
	$(ARMV7_APP_CHECK) $@

$(BUILD)/bin/mount: $(ARMV7_APP_INPUTS) $(ARMV7_APP_OBJ)/userland/base/mount/main.o
	@mkdir -p $(dir $@)
	$(ARMV7_APP_LINK) $(ARMV7_APP_OBJ)/userland/base/mount/main.o $(ARMV7_APP_LIBS) -o $@
	$(ARMV7_APP_CHECK) $@
$(BUILD)/bin/umount: $(BUILD)/bin/mount
	@mkdir -p $(dir $@)
	cp -f $< $@

# Every other program of the root is its objects with the shared command
# helpers.
ARMV7_APP_COMMON_OBJS := $(addprefix $(ARMV7_APP_OBJ)/userland/base/common/,command.o pager.o)

define ARMV7_APP_BASIC_COMMAND
$(BUILD)/bin/$(1): $(ARMV7_APP_INPUTS) $(ARMV7_APP_COMMON_OBJS) \
	$(call ZEDBSD_USERLAND_OBJECTS,$(ARMV7_APP_OBJ),$(1))
	@mkdir -p $$(dir $$@)
	$(ARMV7_APP_LINK) $(ARMV7_APP_COMMON_OBJS) \
 $(call ZEDBSD_USERLAND_OBJECTS,$(ARMV7_APP_OBJ),$(1)) $(ARMV7_APP_LIBS) -o $$@
	$(ARMV7_APP_CHECK) $$@
endef
$(foreach command,$(filter-out sh mount umount,$(ARMV7_ROOT_COMMANDS)),\
	$(eval $(call ARMV7_APP_BASIC_COMMAND,$(command))))

-include $(wildcard $(DYNAMIC_DIR)/obj/*/*/*.d $(DYNAMIC_DIR)/obj/*/*/*/*.d $(DYNAMIC_DIR)/obj/*/*/*/*/*.d)
