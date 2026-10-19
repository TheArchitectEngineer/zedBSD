# zedBSD riscv64: the C library, the runtime linker and the base programs.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# The base programs are position-independent executables that load
# /lib/libc.so through /lib/ld.so, as on arm64.  Every module's TLS is
# dynamic (general-dynamic, the traditional dialect, no descriptors), since
# the kernel gives no image a static TLS block on this processor and the
# loader lays none out.  Nothing is built or linked with relaxation, so no
# code is addressed through the global pointer.
RISCV64_SYSROOT := $(ZEDBSD_SYSROOT_RISCV64)
RISCV64_USER_ARCH_FLAGS := -march=rv64gc -mabi=lp64d -mno-relax
RISCV64_USER_LDFLAGS := -m elf64lriscv --no-relax -z max-page-size=4096

DYNAMIC_DIR := $(BUILD)/dynamic
DYNAMIC_CPPFLAGS := -nostdinc -I. -Iinclude \
	-isystem $(RISCV64_SYSROOT)/usr/include \
	-DHAL_ARCH_RISCV64 -DKERN_USER_ABI_RISCV64 -DKERN_USER_ABI_LP64 \
	-DKERN_DYNAMIC_LIBC
DYNAMIC_CFLAGS := $(RISCV64_USER_ARCH_FLAGS) -Os -ffreestanding \
	-fPIC -fno-builtin -fno-stack-protector \
	-fno-asynchronous-unwind-tables -fno-unwind-tables \
	-ftls-model=global-dynamic -mtls-dialect=trad -Wall -Wextra -Werror
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
	$(DYNAMIC_DIR)/obj/src/libc/setjmp-riscv64.o
DYNAMIC_RTLD_OBJS := $(DYNAMIC_DIR)/obj/src/rtld/entry.o \
	$(DYNAMIC_DIR)/obj/src/rtld/rtld.o \
	$(DYNAMIC_DIR)/obj/src/rtld/string.o

# The mathematics and the software floating point, including the IEEE
# binary128 long double of this ABI, are part of the C library.
DYNAMIC_FLOAT_DIR := $(DYNAMIC_DIR)/float
DYNAMIC_LIBM_OBJS := $(patsubst src/libc/math/%.c,\
	$(DYNAMIC_FLOAT_DIR)/math/%.o,$(ZEDBSD_LIBM_SOURCES))
DYNAMIC_FLOAT_PARSE_OBJS := $(DYNAMIC_FLOAT_DIR)/softfloat.o \
	$(DYNAMIC_FLOAT_DIR)/compiler-runtime.o \
	$(DYNAMIC_FLOAT_DIR)/softfloat128.o \
	$(DYNAMIC_FLOAT_DIR)/compiler-runtime128.o \
	$(DYNAMIC_FLOAT_DIR)/float-parse.o
DYNAMIC_LIBC_OBJS += $(DYNAMIC_LIBM_OBJS) $(DYNAMIC_FLOAT_PARSE_OBJS)

$(DYNAMIC_DIR)/obj/%.o: %.c $(RISCV64_SYSROOT)/.zedbsd-sysroot-complete
	@mkdir -p $(dir $@)
	$(RISCV64_CC) $(DYNAMIC_CPPFLAGS) $(DYNAMIC_CFLAGS) -MMD -MP -c $< -o $@
$(DYNAMIC_DIR)/obj/userland/base/libc/syscall.o: \
	userland/base/libc/syscall-riscv64.S include/hal/arch.h \
	include/hal/arch/riscv64.h
	@mkdir -p $(dir $@)
	$(RISCV64_CC) $(RISCV64_USER_ARCH_FLAGS) $(DYNAMIC_CPPFLAGS) -c $< -o $@
$(DYNAMIC_DIR)/obj/src/libc/setjmp-riscv64.o: src/libc/setjmp-riscv64.S
	@mkdir -p $(dir $@)
	$(RISCV64_CC) $(RISCV64_USER_ARCH_FLAGS) $(DYNAMIC_CPPFLAGS) -D_ASM_SRC_ -c $< -o $@
$(DYNAMIC_DIR)/obj/src/rtld/entry.o: src/rtld/entry-riscv64.S
	@mkdir -p $(dir $@)
	$(RISCV64_CC) $(RISCV64_USER_ARCH_FLAGS) -fPIC -c $< -o $@
$(DYNAMIC_LIBM_OBJS): $(DYNAMIC_FLOAT_DIR)/math/%.o: src/libc/math/%.c \
	$(ZEDBSD_LIBM_HEADERS) $(RISCV64_SYSROOT)/.zedbsd-sysroot-complete
	@mkdir -p $(dir $@)
	$(RISCV64_CC) $(DYNAMIC_CPPFLAGS) $(DYNAMIC_CFLAGS) -c $< -o $@
$(DYNAMIC_FLOAT_PARSE_OBJS): $(DYNAMIC_FLOAT_DIR)/%.o: src/libc/%.c \
	src/libc/softfloat.h src/libc/softfloat128.h \
	$(RISCV64_SYSROOT)/.zedbsd-sysroot-complete
	@mkdir -p $(dir $@)
	$(RISCV64_CC) $(DYNAMIC_CPPFLAGS) $(DYNAMIC_CFLAGS) -c $< -o $@

# The loader relocates itself before anything else runs, so it carries
# only relative relocations; the C library leaves the loader's private
# entry points to be found in the loader at run time.
$(DYNAMIC_DIR)/ld.so: $(DYNAMIC_RTLD_OBJS) tools/build/check-dynamic-elf.py
	$(RISCV64_LD) $(RISCV64_USER_LDFLAGS) -shared -Bsymbolic -e _rtld_start \
 --hash-style=sysv -z now -z relro -z separate-code $(DYNAMIC_RTLD_OBJS) -o $@
	$(PYTHON) tools/build/check-dynamic-elf.py --machine riscv64 --role interpreter $@
$(DYNAMIC_DIR)/libc.so: $(DYNAMIC_LIBC_OBJS) tools/build/check-dynamic-elf.py
	$(RISCV64_LD) $(RISCV64_USER_LDFLAGS) -shared -soname libc.so --hash-style=both \
 -z now -z relro -z separate-code -z stack-size=0x100000 \
 --allow-shlib-undefined $(DYNAMIC_LIBC_OBJS) -o $@
	$(PYTHON) tools/build/check-dynamic-elf.py --machine riscv64 --role libc $@

$(eval $(call ZEDBSD_LIBUTIL_RULE,$(DYNAMIC_DIR),$(RISCV64_LD),$(RISCV64_USER_LDFLAGS),riscv64))

# The runtime's test objects the root carries on every LP64 processor: a
# TLS module with an $$ORIGIN run path, a versioned pair and a program.
$(DYNAMIC_DIR)/alt/rpathdep.so: \
	$(DYNAMIC_DIR)/obj/userland/tests/rpathdep.o $(DYNAMIC_DIR)/ld.so
	@mkdir -p $(dir $@)
	$(RISCV64_LD) $(RISCV64_USER_LDFLAGS) -shared -soname rpathdep.so --hash-style=gnu \
 -z now -z relro -z separate-code $< -o $@
$(DYNAMIC_DIR)/tlstest.so: $(DYNAMIC_DIR)/obj/userland/tests/tlstest.o \
	$(DYNAMIC_DIR)/alt/rpathdep.so $(DYNAMIC_DIR)/ld.so
	$(RISCV64_LD) $(RISCV64_USER_LDFLAGS) -shared -soname tlstest.so --hash-style=gnu \
 -z now -z relro -z separate-code \
 --enable-new-dtags -rpath '$$ORIGIN/alt' \
 $(DYNAMIC_DIR)/obj/userland/tests/tlstest.o \
 -L$(DYNAMIC_DIR)/alt -l:rpathdep.so -o $@
	$(PYTHON) tools/build/check-dynamic-elf.py --machine riscv64 --role module $@
$(DYNAMIC_DIR)/rpathtest.so: \
	$(DYNAMIC_DIR)/obj/userland/tests/rpathtest.o \
	$(DYNAMIC_DIR)/alt/rpathdep.so $(DYNAMIC_DIR)/ld.so
	$(RISCV64_LD) $(RISCV64_USER_LDFLAGS) -shared -soname rpthtest.so --hash-style=gnu \
 -z now -z relro -z separate-code \
 --disable-new-dtags -rpath '$$ORIGIN/alt' $< \
 -L$(DYNAMIC_DIR)/alt -l:rpathdep.so -o $@
	$(PYTHON) tools/build/check-dynamic-elf.py --machine riscv64 --role rpath-module $@
$(DYNAMIC_DIR)/verstest.so: \
	$(DYNAMIC_DIR)/obj/userland/tests/versiontest.o \
	userland/tests/versiontest.map $(DYNAMIC_DIR)/ld.so
	$(RISCV64_LD) $(RISCV64_USER_LDFLAGS) -shared -soname verstest.so --hash-style=gnu \
 -z now -z relro -z separate-code \
 --version-script=userland/tests/versiontest.map $< -o $@
	$(PYTHON) tools/build/check-dynamic-elf.py --machine riscv64 --role version-definition $@
$(DYNAMIC_DIR)/versuse.so: \
	$(DYNAMIC_DIR)/obj/userland/tests/versionuse.o \
	$(DYNAMIC_DIR)/verstest.so $(DYNAMIC_DIR)/ld.so
	$(RISCV64_LD) $(RISCV64_USER_LDFLAGS) -shared -soname versuse.so --hash-style=gnu \
 -z now -z relro -z separate-code \
 $< -L$(DYNAMIC_DIR) -l:verstest.so -o $@
	$(PYTHON) tools/build/check-dynamic-elf.py --machine riscv64 --role version-consumer $@

# A program is crt1, its own objects and the shared C library.
RISCV64_APP_OBJ := $(DYNAMIC_DIR)/obj
RISCV64_APP_INPUTS := $(RISCV64_SYSROOT)/usr/lib/crt1.o \
	$(DYNAMIC_DIR)/libc.so $(DYNAMIC_DIR)/ld.so \
	tools/build/check-dynamic-elf.py
RISCV64_APP_LINK = $(CC) $(RISCV64_USER_ARCH_FLAGS) -nostdlib -pie \
 -Wl,--no-relax -Wl,--gc-sections \
 -Wl,--hash-style=sysv,-z,now,-z,relro,-z,separate-code \
 -Wl,-z,stack-size=0x100000,--allow-shlib-undefined \
 -Wl,--dynamic-linker=/lib/ld.so $(RISCV64_SYSROOT)/usr/lib/crt1.o
RISCV64_APP_LIBS = -L$(DYNAMIC_DIR) -Wl,-rpath-link,$(DYNAMIC_DIR) -l:libc.so
RISCV64_APP_CHECK = $(PYTHON) tools/build/check-dynamic-elf.py --machine riscv64 \
 --role application --needed libc.so

$(DYNAMIC_DIR)/dyntest: $(RISCV64_SYSROOT)/usr/lib/crt1.o \
	$(DYNAMIC_DIR)/obj/userland/tests/dyntest.o $(DYNAMIC_DIR)/libc.so \
	$(DYNAMIC_DIR)/ld.so $(DYNAMIC_DIR)/tlstest.so \
	$(DYNAMIC_DIR)/versuse.so
	$(RISCV64_APP_LINK) $(DYNAMIC_DIR)/obj/userland/tests/dyntest.o \
 -L$(DYNAMIC_DIR) -Wl,-rpath-link,$(DYNAMIC_DIR) -l:libc.so -o $@
	$(PYTHON) tools/build/check-dynamic-elf.py --machine riscv64 --role program $@

RISCV64_APP_SH_OBJS := $(call ZEDBSD_USERLAND_OBJECTS,$(RISCV64_APP_OBJ),sh) \
	$(RISCV64_APP_OBJ)/userland/base/libedit/readline.o
$(RISCV64_APP_SH_OBJS): DYNAMIC_CPPFLAGS += -Iuserland/base/libedit

$(BUILD)/bin/sh: $(RISCV64_APP_INPUTS) $(RISCV64_APP_SH_OBJS)
	@mkdir -p $(dir $@)
	$(RISCV64_APP_LINK) $(RISCV64_APP_SH_OBJS) $(RISCV64_APP_LIBS) -o $@
	$(RISCV64_APP_CHECK) $@

$(BUILD)/bin/sysctl: $(RISCV64_APP_INPUTS) $(RISCV64_APP_OBJ)/userland/base/sysctl/main.o
	@mkdir -p $(dir $@)
	$(RISCV64_APP_LINK) $(RISCV64_APP_OBJ)/userland/base/sysctl/main.o $(RISCV64_APP_LIBS) -o $@
	$(RISCV64_APP_CHECK) $@

$(BUILD)/bin/mount: $(RISCV64_APP_INPUTS) $(RISCV64_APP_OBJ)/userland/base/mount/main.o
	@mkdir -p $(dir $@)
	$(RISCV64_APP_LINK) $(RISCV64_APP_OBJ)/userland/base/mount/main.o $(RISCV64_APP_LIBS) -o $@
	$(RISCV64_APP_CHECK) $@
$(BUILD)/bin/umount: $(BUILD)/bin/mount
	@mkdir -p $(dir $@)
	cp -f $< $@

# Every other base program is its objects with the shared command helpers;
# a network program has the network helpers instead.
RISCV64_APP_COMMON_OBJS := $(addprefix $(RISCV64_APP_OBJ)/userland/base/common/,command.o pager.o)
RISCV64_APP_NET_OBJS := $(addprefix $(RISCV64_APP_OBJ)/userland/base/net/,netutil.o dhcp.o)

define RISCV64_APP_BASIC_COMMAND
$(BUILD)/bin/$(1): $(RISCV64_APP_INPUTS) $(RISCV64_APP_COMMON_OBJS) \
	$(call ZEDBSD_USERLAND_OBJECTS,$(RISCV64_APP_OBJ),$(1))
	@mkdir -p $$(dir $$@)
	$(RISCV64_APP_LINK) $(RISCV64_APP_COMMON_OBJS) \
 $(call ZEDBSD_USERLAND_OBJECTS,$(RISCV64_APP_OBJ),$(1)) $(RISCV64_APP_LIBS) -o $$@
	$(RISCV64_APP_CHECK) $$@
endef
$(foreach command,$(USERLAND_SELECTED_BASIC_PROGRAMS),\
	$(eval $(call RISCV64_APP_BASIC_COMMAND,$(command))))

define RISCV64_APP_NET_COMMAND
$(BUILD)/bin/$(1): $(RISCV64_APP_INPUTS) $(RISCV64_APP_NET_OBJS) \
	$(call ZEDBSD_USERLAND_OBJECTS,$(RISCV64_APP_OBJ),$(1))
	@mkdir -p $$(dir $$@)
	$(RISCV64_APP_LINK) $(RISCV64_APP_NET_OBJS) \
 $(call ZEDBSD_USERLAND_OBJECTS,$(RISCV64_APP_OBJ),$(1)) $(RISCV64_APP_LIBS) -o $$@
	$(RISCV64_APP_CHECK) $$@
endef
$(foreach command,$(USERLAND_SELECTED_NETWORK_PROGRAMS),\
	$(eval $(call RISCV64_APP_NET_COMMAND,$(command))))

# The root tree: the base programs, the accounts, the base data and the
# runtime, staged by the common rule.
include platform/common/userland-rootfs.mk
RISCV64_ROOTFS_INPUTS := $(ZEDBSD_ROOTFS_INPUTS)
RISCV64_ROOTFS_FILES := $(ZEDBSD_ROOTFS_FILES)
$(eval $(call ZEDBSD_ROOTFS_TREE_RULE,riscv64,$(RISCV64_ROOTFS_INPUTS),$(RISCV64_ROOTFS_FILES)))
rootfs: $(BUILD)/rootfs/.stamp

-include $(wildcard $(DYNAMIC_DIR)/obj/*/*/*.d $(DYNAMIC_DIR)/obj/*/*/*/*.d $(DYNAMIC_DIR)/obj/*/*/*/*/*.d)
