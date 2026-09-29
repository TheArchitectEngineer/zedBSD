# 案: `/bin/settings` の link の規則（ws089-p001、未適用、p002 の前提）

所有: `platform/amd64/vmunix.mk`（WS089 の所有でない）。main の許可が要る。

desktop の app は汎用の静的な link の規則（`AMD64_USER_BASIC_COMMAND`）から外し、個別に動的に link する（files の `vmunix.mk` の
「The file manager (WS071)」の規則と同じ形）。汎用の規則に残ると libvulkan・libwayland が無く link に失敗し、個別の規則だけを足すと
同じ target の recipe が二重になる。

```make
# 1. 汎用の規則の filter-out の一覧に settings を足す。
$(foreach command,$(filter-out vkdemo wltest ... files notes pdfviewer settings browser ...,$(USER_BASIC_COMMANDS)),\

# 2. files の規則の後に足す。
# Settings (WS089) imports standard Wayland, Vulkan, TrueType and C library entry
# points, and zdesktop's own extensions and the system's services through libkeiland.
DYNAMIC_ZDESKTOP_SETTINGS_OBJS := $(call ZEDBSD_USERLAND_OBJECTS,$(DYNAMIC_DIR)/obj,settings)

$(BUILD)/bin/settings: $(ZEDBSD_SYSROOT_AMD64)/usr/lib/crt1.o \
	$(DYNAMIC_ZDESKTOP_SETTINGS_OBJS) $(DYNAMIC_DIR)/libvulkan.so $(DYNAMIC_DIR)/libwayland-client.so \
	$(DYNAMIC_DIR)/libkeiland.so $(DYNAMIC_DIR)/libtruetype.so \
	$(DYNAMIC_DIR)/libc.so $(DYNAMIC_DIR)/ld.so $(DYNAMIC_VULKAN_CHECK)
	@mkdir -p $(dir $@)
	$(CC) -m64 -nostdlib -pie -Wl,--no-relax \
 -Wl,--hash-style=sysv,-z,now,-z,relro,-z,separate-code \
 -Wl,-z,stack-size=0x100000,--allow-shlib-undefined \
 -Wl,--dynamic-linker=/lib/ld.so \
 $(ZEDBSD_SYSROOT_AMD64)/usr/lib/crt1.o $(DYNAMIC_ZDESKTOP_SETTINGS_OBJS) \
 -L$(DYNAMIC_DIR) -Wl,-rpath-link,$(DYNAMIC_DIR) \
 -l:libvulkan.so -l:libwayland-client.so -l:libkeiland.so -l:libtruetype.so -l:libc.so -o $@
	$(PYTHON) $(DYNAMIC_VULKAN_CHECK) --machine amd64 --role application \
 --needed libvulkan.so --needed libwayland-client.so --needed libkeiland.so --needed libtruetype.so --needed libc.so $@
```

- files の `canvas.c`・`text.c`・`icons.c` を settings の source の一覧に載せると、同じ object（`$(DYNAMIC_DIR)/obj/userland/desktop/files/*.o`）を
  files と共有する（今は target 別の CPPFLAGS が無いので同じ中身）。
- 変更は約 20 行。p002 の最初に、許可の後に当てる。許可が無い間は p002 の source の build（compile）までを確かめ、link と guest は待つ。
