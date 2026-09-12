/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Verifies the selected standard interface-description ABI on both x86 models. */

#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include <stddef.h>

#ifdef __cplusplus
#define ABI_ASSERT static_assert
#else
#define ABI_ASSERT _Static_assert
#endif

ABI_ASSERT(sizeof(wl_fixed_t) == 4, "Wayland fixed coordinates are 32 bits");
ABI_ASSERT(offsetof(struct wl_array, size) == 0, "array size is first");
ABI_ASSERT(offsetof(struct wl_interface, name) == 0, "interface name is first");

#if __SIZEOF_POINTER__ == 8
ABI_ASSERT(sizeof(struct wl_array) == 24, "LP64 array layout");
ABI_ASSERT(sizeof(struct wl_list) == 16, "LP64 list layout");
ABI_ASSERT(sizeof(struct wl_message) == 24, "LP64 message layout");
ABI_ASSERT(sizeof(struct wl_interface) == 40, "LP64 interface layout");
ABI_ASSERT(sizeof(union wl_argument) == 8, "LP64 argument layout");
ABI_ASSERT(offsetof(struct wl_array, data) == 16, "LP64 array data offset");
ABI_ASSERT(offsetof(struct wl_interface, version) == 8, "LP64 interface version");
ABI_ASSERT(offsetof(struct wl_interface, methods) == 16, "LP64 methods offset");
ABI_ASSERT(offsetof(struct wl_interface, events) == 32, "LP64 events offset");
#else
ABI_ASSERT(sizeof(struct wl_array) == 12, "ILP32 array layout");
ABI_ASSERT(sizeof(struct wl_list) == 8, "ILP32 list layout");
ABI_ASSERT(sizeof(struct wl_message) == 12, "ILP32 message layout");
ABI_ASSERT(sizeof(struct wl_interface) == 24, "ILP32 interface layout");
ABI_ASSERT(sizeof(union wl_argument) == 4, "ILP32 argument layout");
ABI_ASSERT(offsetof(struct wl_array, data) == 8, "ILP32 array data offset");
ABI_ASSERT(offsetof(struct wl_interface, version) == 4, "ILP32 interface version");
ABI_ASSERT(offsetof(struct wl_interface, methods) == 12, "ILP32 methods offset");
ABI_ASSERT(offsetof(struct wl_interface, events) == 20, "ILP32 events offset");
#endif
