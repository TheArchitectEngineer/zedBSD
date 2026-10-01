# WS104 p004〜p006: compositor の編集の手順（正確な版）

2026-10-01 に Q1 の survey（subagent、HEAD `0eb5e118`）がまとめた、compositor（`userland/desktop/wayland/`）の refactor の**正確な編集の一覧**。
phase004〜006 の phase.md は「何を・なぜ」、この文書は「どの行をどう変えるか」。**行番号は `0eb5e118` の物**で、前の Phase の編集で動く。行番号でなく、
引用した code の文字列で場所を探すこと。code は survey の時点で compile していない（Phase の build で確かめる）。

survey で確かめたこと: `zedbsd/` の中の file から `#include "userland/desktop/wayland/compose.h"` は compile できる（`#include "zwl.h"` は「file not found」で失敗する）。
zedBSD の clang は `__ZEDBSD__`・`__unix__` を定義し、`__linux__` を定義しない。kernel の evdev の ioctl（`src/drivers/generic/input.c:2215-2320`）は `EVIOCGABS`・
`EVIOCGID`・`EVIOCGNAME`・`EVIOCGBIT` で 0 か errno を返す。subdirectory の source は `build/amd64/dynamic/obj/<source の path>.o` に compile される
（`platform/amd64/vmunix.mk:793`、`Makefile:401-404`。`vkdemo/display.c` と `wayland/display.c` が同じ basename で既に共存している）。

## 0. survey で見つかった phase.md の誤りと、Q1 の決定（2026-10-01）

| # | 見つかった事 | 決定 |
| --- | --- | --- |
| 1 | p006: `VkDisplayKHR` がどこにも保存されていない（`compose_display` の local の変数で捨てている、`compose.c:740`） | `struct zwl_compose`（`compose.h`）に `VkDisplayKHR display;` を足し、`compose_display` で設定する（下の P006）。WS104 の範囲の変更として Q1 が承認 |
| 2 | p005 の確かめの grep `uapi/input.h` は `menu-shell.c:72,75` の注釈にも当たる | grep を `'#include <uapi/input.h>'` にする |
| 3 | p005: `input.c` は 195・491・1030・1043・1090・1103 行でも `close()` している | 全てを `zwl_input_device_close(server, fd)` にする（Linux の logind で device を返すため） |
| 4 | p004: 分けた後は import の記録の `calloc` が最後になり、host の memory 不足のときだけ log の行（`VULKAN_IMPORT_ERROR result=-1`、`IMPORT_ERROR errno=22`）が今と変わる | 受け入れる（host の memory 不足の時だけの差。正しい動きの log は変わらない） |
| 5 | p004: `objects.c` は `compose.h` を include していない | `#include "compose.h"` を足す |
| 6 | p004: `zwl-gpu.h` は `zwl.h:43` で include され、`zwl.h` の前方宣言（88 行）より前 | `zwl-gpu.h` に `struct zwl_object;` の前方宣言を足す |
| 7 | 動かす file を名指しする script は `plan/tools/gpu-boundary/` の 3 つ（`v1-check.sh`・`run-gpu-zedbsd-host.sh`・`gpu-zedbsd-host.c`）だけ | 下の「道具」の diff を当てる（main の範囲。subagent は diff を main に送る）。source の注釈の古い file の名前（`home.c:1462`・`zwl.h:626`・`greeter.c:16,28,282,466`・`shell.c:1973`・`compose.c:582`・`protocol.c:1216,1692`・`zwl-gpu.h:15`）は直してよい（任意） |

---

# P004: GPU の境界の引き上げ

## 1. `factory_request`・`factory_fence`・`factory_alpha` が使う物

| symbol | 種類と場所 | protocol.c の他でも使うか | 扱い |
| --- | --- | --- | --- |
| `word_at` | static、`protocol.c:73`（宣言）・`335-347`（定義） | はい（28 箇所） | `gpu-buffer-zedbsd.c` に同じ名前の static として複写する（各 file が自分の複写を持つ慣習: `data_word`・`desktop_word`・`menu_word`） |
| `factory_fence`・`factory_alpha` | static、宣言 85-86 | いいえ | 名前を変えずに移す |
| `zwl_take_fd`・`zwl_find`・`zwl_create`・`zwl_object_destroy`・`zwl_import_set_alpha`・`zwl_milliseconds` | public（`zwl.h:1025-1049`） | — | そのまま |
| `zwl_gpu_buffer_wire_bytes`・`zwl_gpu_buffer_decode` | `zwl-gpu.h:54-55` | — | `zedbsd/gpu-zedbsd.h` へ |
| `zwl_import_create` | `zwl.h:1047`、呼ぶのは `protocol.c:1225` だけ | — | `gpu-buffer-zedbsd.c` の static `buffer_import` になる。`zwl.h` から消す |
| `ZWL_FENCE_MAX`・`ZWL_SURFACE`・`ZWL_BUFFER`・`acquire[]`・`server->log_frames`・`server->dirty`・`server->gpu_limits`・`client->number` | `zwl.h` | — | そのまま |
| `printf`・`close`・`EAGAIN`・`EPROTO`・`memcpy` | libc | — | `<stdio.h>`・`<unistd.h>`・`<errno.h>`・`<string.h>` を include |

macro・file の中の型（`OUTPUT_*`・`struct zwl_global`）は使っていない。`zwl.h` に新しく export する物は無い。移した後も `protocol.c` は `close()`（`commit_fence`）・`printf`・`memcpy` を使う。

## 2. `import.c` の分け方（`import_image`、124-272）

今の `import_image` の行:

- **zedBSD へ移す**: 145-146（`format = image->format`）、152-174（`vkCreateImage`、失敗で `close(descriptor)`）、176-192（requirements と layout の照合、`VULKAN_IMPORT_LAYOUT` の printf、`close(descriptor)`）、
  194-218（dedicated の `vkAllocateMemory`、失敗で `close(descriptor)`）、220-223（`vkBindImageMemory`、fd は既に Vulkan の物）。
- **adopt へ移す**: 149-151（`import->width`・`height`・`draw`）。
- **共通に残す**: 225-271（view、238-250 の layout の移行、252-267 の set、269-271 の `zwl_compose_linear_set`）。

今の後始末: `zwl_import_create`（37-76）は `calloc`（51）→ `dup`（56、失敗で記録を free）→ `import_image`。失敗で `VULKAN_IMPORT_ERROR` を出し、`import_release` し、記録を free し、
EINVAL を返す。`import_release`（341-374）は待っている layout の entry を外し、`set`・`linear_set` を put し、view を消し、**image を消してから memory を free**（それぞれ NULL を確かめて）。

### 新しい `import.c`

`import_layout`・`import_release`・`zwl_import_set_alpha`・`zwl_import_destroy`・`zwl_import_layouts_*` は変えない。`zwl_import_create`（33-76）を消す。29 行の前方宣言は
`static VkResult import_image(struct zwl_compose *compose, VkFormat format, struct zwl_import *import);` になる。

```c
/*
 * Makes a buffer's import from an image and its bound memory that the OS
 * module made (zwl-gpu.h): the view the shader samples, the move to the
 * general layout and the descriptor sets.  The image and the memory are
 * taken: on failure they are destroyed with whatever else was made.
 */
VkResult
zwl_import_adopt(
	struct zwl_object *buffer,
	VkImage image,
	VkDeviceMemory memory,
	uint32_t width,
	uint32_t height,
	VkFormat format)
{
	struct zwl_compose *compose;
	struct zwl_import *import;
	VkResult result;

	/* The compositor's Vulkan device the image belongs to. */
	compose = buffer->client->server->compose;

	/* The import record, owned by the buffer; without it the image and its memory go. */
	import = calloc(1, sizeof(*import));
	if (import == NULL) {
		vkDestroyImage(compose->device, image, NULL);
		vkFreeMemory(compose->device, memory, NULL);
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}

	/* The image, its memory and its size, drawn opaque until set_alpha says otherwise. */
	import->image = image;
	import->memory = memory;
	import->width = width;
	import->height = height;
	import->draw = ZWL_DRAW_OPAQUE;

	/* The view and descriptor sets that sample it, and its move to the general layout. */
	result = import_image(compose, format, import);
	if (result != VK_SUCCESS) {
		import_release(compose, import);
		free(import);
		return result;
	}

	/* Succeeded: the buffer can be drawn in window mode. */
	buffer->import = import;
	return VK_SUCCESS;
}
```

- `import_image` は名前を残し、引数を `(struct zwl_compose *compose, VkFormat format, struct zwl_import *import)` にする。先頭の注釈「Makes the view and descriptor sets that sample an imported image.」、
  宣言 `VkImageViewCreateInfo view; VkDescriptorImageInfo image_info; VkWriteDescriptorSet write; VkResult result;`、本体は今の 225-271 をそのまま。
- `zwl_import_adopt` は public な関数の並びの、`zwl_import_set_alpha` の前に置く。
- `<errno.h>`・`<stdio.h>`・`<unistd.h>` の include が要らなくなるので、grep で確かめて消す。file の先頭の注釈（8-19）を「OS の module が image と memory を作る」に直す。

### `zwl.h` の編集

- 290-291 行（注釈と `struct zwl_buffer_layout layout;`）を消す。
- 1047 行を次に置き換える（Vulkan の型は `zwl-gpu.h` から来る）:

```c
VkResult zwl_import_adopt(struct zwl_object *buffer, VkImage image, VkDeviceMemory memory, uint32_t width, uint32_t height, VkFormat format);
```

`struct zwl_import` は `compose.h:56-71`。`zedbsd/` から `#include "userland/desktop/wayland/compose.h"` で使える（確かめ済み）。

### `gpu-buffer-zedbsd.c` の zedBSD の半分

```c
/*
 * Imports a buffer's image for window mode.  The descriptor stays the
 * caller's; a copy of it is given to Vulkan.
 */
static int
buffer_import(
	struct zwl_object *buffer,
	const struct zwl_buffer_layout *layout,
	int descriptor)
{
	struct zwl_compose *compose;
	VkImage image;
	VkDeviceMemory memory;
	int copy;
	VkResult result;

	/* The compositor's Vulkan device the image is imported into. */
	compose = buffer->client->server->compose;

	/* Vulkan consumes the fd it imports, so it gets its own. */
	copy = dup(descriptor);
	if (copy < 0)
		return errno;

	/* The image bound to the imported memory (the copy is consumed or closed). */
	result = buffer_image(compose, layout, copy, &image, &memory);
	if (result != VK_SUCCESS) {
		printf("ZWL VULKAN_IMPORT_ERROR client=%llu buffer=%u result=%d\n", (unsigned long long)buffer->client->number, buffer->id, (int)result);
		return EINVAL;
	}

	/* Its view and descriptor sets and the buffer's import record (import.c); the image and memory go on failure. */
	result = zwl_import_adopt(buffer, image, memory, layout->width, layout->height, layout->format);
	if (result != VK_SUCCESS) {
		printf("ZWL VULKAN_IMPORT_ERROR client=%llu buffer=%u result=%d\n", (unsigned long long)buffer->client->number, buffer->id, (int)result);
		return EINVAL;
	}

	/* Succeeded: the buffer can be drawn in window mode. */
	if (buffer->client->server->log_frames)
		printf("ZWL VULKAN_IMPORT client=%llu buffer=%u width=%u height=%u\n", (unsigned long long)buffer->client->number, buffer->id, buffer->import->width, buffer->import->height);
	return 0;
}
```

（今の `zwl_import_create` の log の行と同じ形か、実装の時に今の code と照らして確かめる。違えば今の code の形に合わせる。）

`buffer_image(struct zwl_compose *compose, const struct zwl_buffer_layout *image, int descriptor, VkImage *created, VkDeviceMemory *memory)`:

- 宣言: 今の 131-138・143（`external`・`create`・`requirements`・`subresource`・`layout`・`dedicated`・`import_info`・`allocate`・`result`）と `VkFormat format;`。
- 最初の文:

```c
	/* Nothing is made yet. */
	*created = VK_NULL_HANDLE;
	*memory = VK_NULL_HANDLE;
```

- 続けて今の 145-146 と 152-223 を、次の置き換えで: `&import->image` → `created`、`import->image` → `*created`、`&import->memory` → `memory`、`import->memory` → `*memory`。
  149-151 は落とす。失敗のたびに、今ある `close(descriptor)`（172・190・216 行）の後に `buffer_image_release(compose, created, memory);` を足す。
  `vkBindImageMemory` の失敗（222）は `{ buffer_image_release(compose, created, memory); return result; }`。最後は `/* Succeeded: the image is bound to the client's memory. */ return VK_SUCCESS;`。

```c
/* Destroys what buffer_image made, whatever part of it was made, in import_release's order. */
static void
buffer_image_release(
	struct zwl_compose *compose,
	VkImage *image,
	VkDeviceMemory *memory)
{
	/* The image, then its memory. */
	if (*image != VK_NULL_HANDLE)
		vkDestroyImage(compose->device, *image, NULL);
	if (*memory != VK_NULL_HANDLE)
		vkFreeMemory(compose->device, *memory, NULL);
	*image = VK_NULL_HANDLE;
	*memory = VK_NULL_HANDLE;
}
```

log の順（`LAYOUT` → `VULKAN_IMPORT_ERROR`）は同じ。破棄が 2 行目の前になるだけ。

### `zwl_gpu_request`

`factory_request`（`protocol.c:1160-1238`）を移して名前を変えた物（引数の名前は `factory` のまま）。変更: local `struct zwl_buffer_layout layout;` を足す、1217 行は `&buffer->layout` の代わりに `&layout`、
1225 行は `error = buffer_import(buffer, &layout, descriptor);`、1234 行は `layout.width`・`layout.height`・`(unsigned long long)layout.allocation_bytes` を出す（format の文字列は同じ）。

### `gpu-buffer-zedbsd.c` の並び

```c
#include "userland/desktop/wayland/compose.h"
#include "userland/desktop/wayland/zedbsd/gpu-zedbsd.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
```

前方宣言: `word_at`・`factory_fence`・`factory_alpha`・`buffer_import`・`buffer_image`・`buffer_image_release`。
public な関数の順: (1) `zwl_gpu_global_interface` → `"keiland_gpu_buffer_v1"`、(2) `zwl_gpu_global_version` → `3U`、(3) `zwl_gpu_request`、(4) `zwl_gpu_commit`（`(void)surface; (void)buffer;`、
注釈「zedBSD's fences come with set_acquire_fence」）、(5) `zwl_gpu_instance_extensions`（`(void)names; (void)capacity;`）・`zwl_gpu_device_extensions`（`(void)physical; (void)names; (void)capacity;`）、どちらも `0U` を返す（device の hook は physical device を受け取る: Linux の module が後段にある拡張だけを返すため、2026-10-01 の design-reviewer の指摘）、
(6) `zwl_gpu_frame_fence_type` → `VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT`。続けて static: `factory_fence`（1612-1663 をそのまま）、`factory_alpha`（1665-1698 をそのまま）、
`buffer_import`、`buffer_image`、`buffer_image_release`、`word_at`（334-347 をそのまま）。未使用の引数はこの code base では `(void)param;`（`clipboard.c:110`）。

### 新しい `zwl-gpu.h`（今の 27-56 行を置き換え）

file の注釈「The boundary between the compositor and its OS's GPU module (WS103, raised by WS104 p004)」。`<stddef.h>`・`<stdint.h>`・`<vulkan/vulkan.h>`・`<vulkan/vulkan_external.h>` は残す
（ただし `<vulkan/vulkan_external.h>` は下の「`zwl-gpu.h` の `<vulkan/vulkan_external.h>` を消す」のとおり消す。`VkExternalFenceHandleTypeFlagBits` は zedBSD では `vulkan_external.h:59` にあり `vulkan_core.h` 経由で届く。Linux では `vulkan_core.h` の core 1.1）。

```c
struct zwl_object;

/* (今の struct zwl_gpu_limits の block、45-52 行を残す) */

const char *zwl_gpu_global_interface(void);
uint32_t zwl_gpu_global_version(void);
int zwl_gpu_request(struct zwl_object *factory, uint32_t opcode, const unsigned char *bytes, size_t size);
void zwl_gpu_commit(struct zwl_object *surface, struct zwl_object *buffer);
uint32_t zwl_gpu_instance_extensions(const char **names, uint32_t capacity);
uint32_t zwl_gpu_device_extensions(VkPhysicalDevice physical, const char **names, uint32_t capacity);
VkExternalFenceHandleTypeFlagBits zwl_gpu_frame_fence_type(void);
```

関数ごとの注釈は phase004 の phase.md の物を使う。拡張の hook: 「copies up to capacity names and returns how many the module needs; the compositor fails device creation when that exceeds capacity」。

`zedbsd/gpu-zedbsd.h`: guard `#ifndef ZWL_GPU_ZEDBSD_H`、`#include "userland/desktop/wayland/zwl-gpu.h"`・`<stddef.h>`・`<stdint.h>`・Vulkan の header。中身は `struct zwl_buffer_layout`
（`zwl-gpu.h:27-43` から注釈ごと移す）と 54-56 行の 3 つの prototype。**`<uapi/gpu.h>` を include しない。**
`zedbsd/gpu-zedbsd.c`（`git mv` の後）の 20 行 `#include "zwl-gpu.h"` は `#include "userland/desktop/wayland/zedbsd/gpu-zedbsd.h"` に。9 行の注釈は任意で直す。

## 3. registry と bind（`protocol.c`）

- 表: 50 行を `{ 3, NULL, 0, ZWL_FACTORY },` に。`globals[].interface` を読むのは 415・418・570 行だけ（grep で確かめ済み）。
- 新しい static（宣言は 73-90 の並び、定義は `string_at` の後）:

```c
/* Reports a global's interface name and version: the table's, or the OS module's for its GPU buffer global. */
static void
global_identity(
	const struct zwl_global *global,
	const char **interface,
	uint32_t *version)
{
	/* The OS module names its GPU buffer global (zwl-gpu.h; keiland_gpu_buffer_v1 version 3 on zedBSD). */
	if (global->kind == ZWL_FACTORY) {
		*interface = zwl_gpu_global_interface();
		*version = zwl_gpu_global_version();
		return;
	}

	/* Every other global is the table's. */
	*interface = global->interface;
	*version = global->version;
}
```

- `registry_events`（392-429）: local `const char *interface; uint32_t version;` を足す。見える物の確かめ（409 行）の後に `/* The global's interface and version. */ global_identity(&globals[index], &interface, &version);`。
  415 行 → `length = strlen(interface) + 1U;`、418 行 → `memcpy(payload + 8, interface, length);`、420 行 → `word = version;`。
- `bind_global`（530-617）: local の `interface` は client の文字列。`const char *offered; uint32_t offered_version;` を足し、569-572 行を次にする（global は番号で先に照合し（566 行）、その後 interface の文字列を `strcmp`）:

```c
		/* Names, interface strings and negotiated versions are checked together. */
		global_identity(&globals[index], &offered, &offered_version);
		same = strcmp(interface, offered);
		if (same != 0 || version == 0 || version > offered_version)
			return EPROTO;
```

- dispatch: 188 行 → `error = zwl_gpu_request(object, opcode, bytes, size);`。
- 消す: 前方宣言 82・85・86、block 1160-1239（`factory_request` と後の空行）・1612-1664（`factory_fence` と注釈）・1665-1699（`factory_alpha`）。**下の block から先に消す**（上の行番号が動かないように）。
  9 行の file の注釈は任意で直す。

## 4. `commit_fence`（`protocol.c:1557-1579`）

呼ぶ所は 2 つ（874 行 `surface_commit`、918 行 `zwl_surface_queue`）で、どちらも呼ぶ前に `surface->queued` を commit の buffer にしている。sub-surface も `zwl_surface_queue` を通る。
`zwl_object` の field: `import`（`zwl.h:315`）は GPU の buffer だけ非 NULL（wl_shm は surface の `shm_image`）、`shm`（`zwl.h:356`）は wl_shm の buffer だけ、`kind == ZWL_BUFFER` は両方。
本体の最初（1564 行の前）に入れる:

```c
	/* A commit that attached a GPU buffer tells the OS module first, which may take the buffer's own fence (zwl-gpu.h). */
	if (attached &&
	    surface->queued != NULL &&
	    surface->queued->import != NULL &&
	    surface->queued->shm == NULL)
		zwl_gpu_commit(surface, surface->queued);

```

## 5. `compose.c`

### `compose_device`（568-685）の拡張の配列

`instance_extensions[5]`（572-578）と `device_extensions[7]`（585-593）は `static const char *const`。数は 616 行（`5U`）と 674-676 行（`5U`、`fence_fd` なら `7U`）に直書き。
include の後に足す:

```c
/* Room for the compositor's own Vulkan extensions and those its OS module asks for (zwl-gpu.h). */
#define COMPOSE_EXTENSIONS_MAX	16U
```

新しい local: `const char *instance_names[COMPOSE_EXTENSIONS_MAX]; const char *device_names[COMPOSE_EXTENSIONS_MAX]; uint32_t instance_count; uint32_t device_count; uint32_t extra; VkExternalFenceHandleTypeFlagBits fence_type;`。
608 行の前に:

```c
	/* The compositor's instance extensions, then those the OS module needs (none on zedBSD). */
	memcpy(instance_names, instance_extensions, sizeof(instance_extensions));
	instance_count = 5U;
	extra = zwl_gpu_instance_extensions(instance_names + instance_count, COMPOSE_EXTENSIONS_MAX - instance_count);
	if (extra > COMPOSE_EXTENSIONS_MAX - instance_count)
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	instance_count += extra;
```

616-617 行 → `instance.enabledExtensionCount = instance_count;`・`instance.ppEnabledExtensionNames = instance_names;`。658-661 行 →

```c
	/* Both, or neither; and only when the OS module exports the frame's fence at all. */
	fence_type = zwl_gpu_frame_fence_type();
	compose->fence_fd = 0;
	if (wanted == 2U && fence_type != 0)
		compose->fence_fd = 1;
```

663 行の前に:

```c
	/* The compositor's device extensions (the external fence pair when the device has it), then the OS module's. */
	device_count = 5U;
	if (compose->fence_fd)
		device_count = 7U;
	memcpy(device_names, device_extensions, device_count * sizeof(device_names[0]));
	extra = zwl_gpu_device_extensions(compose->physical, device_names + device_count, COMPOSE_EXTENSIONS_MAX - device_count);
	if (extra > COMPOSE_EXTENSIONS_MAX - device_count)
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	device_count += extra;
```

674-677 行 → `device.enabledExtensionCount = device_count;`・`device.ppEnabledExtensionNames = device_names;`。zedBSD では `extra` は 0 で、数（5 と 5・7）と順は変わらない。

### `OPAQUE_FD` の直書き

`grep VK_EXTERNAL_FENCE_HANDLE_TYPE` は 2 つだけ: `compose.c:976`（`compose_objects`）→ `export.handleTypes = zwl_gpu_frame_fence_type();`、`compose.c:1947`（`compose_submit`）→
`fd_info.handleType = zwl_gpu_frame_fence_type();`。type が 0 なら `fence_fd` は 0 で、`export` は chain されず（979 行）、`compose_submit` は 1938 行で戻り（1943 の前）、
今の `vkGetFenceStatus` の道（`compose.c:453-484`）になる。wayland の directory で他に `vkGetFenceFdKHR` を使う所は無い。

### `vkGetFenceFdKHR` を直接呼ばない（WS105 の survey で追加、2026-10-01）

`compose.c:1949` は `vkGetFenceFdKHR` を直接（link の symbol として）呼ぶ。Linux の Khronos の loader（と libvulkan-compat、design §4.7）は拡張の関数を export しないので、
Linux では link が `undefined reference to vkGetFenceFdKHR` で失敗する（2026-10-01 の host の試しの compile で確かめた）。zedBSD の libvulkan は
`vkGetDeviceProcAddr(device, "vkGetFenceFdKHR")` に答える（`libvulkan/dispatch-table.inc`）ので、次の変更は zedBSD では振る舞いを変えない:

- `compose.h` の `unsigned fence_fd;`（164 行）の後に、注釈 `/* vkGetFenceFdKHR, fetched from the device when fence_fd is set (an extension's function is not linked by name). */` と
  field `PFN_vkGetFenceFdKHR get_fence_fd;`。
- `compose_device` の `vkCreateDevice` が成功した後（676-686 行の近く、device を作った直後）に:

```c
	/* The fence export is fetched from the device: an extension's function is reached through it, not by name. */
	compose->get_fence_fd = NULL;
	if (compose->fence_fd) {
		compose->get_fence_fd = (PFN_vkGetFenceFdKHR)vkGetDeviceProcAddr(compose->device, "vkGetFenceFdKHR");
		if (compose->get_fence_fd == NULL)
			compose->fence_fd = 0;
	}
```

- 1949 行の `vkGetFenceFdKHR(compose->device, &fd_info, &fd)` を `compose->get_fence_fd(compose->device, &fd_info, &fd)` に。

### `zwl-gpu.h` の `<vulkan/vulkan_external.h>` を消す（同じ理由）

`<vulkan/vulkan_external.h>` は zedBSD だけの header（Linux の host の `/usr/include/vulkan` に無い）。zedBSD の `include/libc/vulkan/vulkan_core.h:5679` が既に
`#include "vulkan_external.h"` しているので、`zwl-gpu.h` の `#include <vulkan/vulkan_external.h>` の行を消しても zedBSD では何も変わらない。消す。
（上の「新しい `zwl-gpu.h`」の include の一覧から `<vulkan/vulkan_external.h>` を除く。`zedbsd/gpu-zedbsd.h` は zedBSD の file なので Vulkan の header は `<vulkan/vulkan.h>` だけでよい。）

## 6. `objects.c` の `zwl_buffer_size`（185-187 行）

13 行（`"zwl.h"`）の後に `#include "compose.h"`。185-187 行を:

```c
	/* A GPU buffer's size is its image's, made with the buffer (import.c). */
	*width = buffer->import->width;
	*height = buffer->import->height;
```

GPU の buffer は `zwl_gpu_request` でだけ作られ、import が失敗すると消され、`buffer->import` は `object_free`（`objects.c:560`）でだけ free される。surface が指しうる GPU の buffer では import は非 NULL。

## 7. 道具（main の範囲。subagent は diff を main に送る）

`plan/tools/gpu-boundary/v1-check.sh`:

```diff
-#  1. grep: no source but the zedBSD backend module (gpu-zedbsd.c) includes a GPU UAPI header (uapi/gpu*.h), and no
+#  1. grep: no source but the zedBSD backend module (zedbsd/gpu-zedbsd.c) includes a GPU UAPI header (uapi/gpu*.h), and no
-#     compositor source but gpu-zedbsd.c is compiled with -fsyntax-only (the zedBSD build's compiler and flags); a source
+#     compositor source but zedbsd/gpu-zedbsd.c is compiled with -fsyntax-only (the zedBSD build's compiler and flags); a source
-backend=$dir/gpu-zedbsd.c
+backend=$dir/zedbsd/gpu-zedbsd.c
+files=$(find $dir -name '*.[ch]' | sort)
-readers=$(grep -ln '#include <uapi/gpu' $dir/*.c $dir/*.h | grep -v "^$backend$")
+readers=$(grep -ln '#include <uapi/gpu' $files | grep -v "^$backend$")
-calls=$(grep -n 'ioctl([^,]*, *GPU_' $dir/*.c $dir/*.h)
+calls=$(grep -n 'ioctl([^,]*, *GPU_' $files)
-[ -n "$sources" ] || sources=$(ls $dir/*.c)
+[ -n "$sources" ] || sources=$(find $dir -name '*.c' | sort)
-nodes=$(grep -n '"/dev/gpu' $dir/*.c $dir/*.h)
+nodes=$(grep -n '"/dev/gpu' $files)
-option=$(grep -n '"--gpu' $dir/*.c)
+option=$(grep -n '"--gpu' $(find $dir -name '*.c'))
```

script は `KEILAND_SOURCES` を `make print` で読むので、変数の改名の後も動く。新しい `gpu-buffer-zedbsd.c` は GPU の header を毒にした compile でも通る（`gpu-zedbsd.h` は uapi を include しない）。
p005 の後、4 行の注釈は「the evdev ioctls of zedbsd/input-zedbsd.c」にしてよい。
**WS105 の後**（`wayland/linux/` ができた後）: `find $dir` は `linux/` も拾う。linux の file は zedBSD の sysroot で compile できないので、WS105 p006 で v1-check の対象から `linux/` を外す（`find $dir -path '*/linux' -prune -o ...`）。

`plan/tools/gpu-boundary/run-gpu-zedbsd-host.sh`:

```diff
-# ws103-p004: host test of the compositor's check of a GPU buffer's description (userland/desktop/wayland/gpu-zedbsd.c),
+# ws103-p004: host test of the compositor's check of a GPU buffer's description (userland/desktop/wayland/zedbsd/gpu-zedbsd.c),
-        -I"$work/include" -I"$repo/include" -I"$repo/userland/desktop/wayland" \
-        "$repo/plan/tools/gpu-boundary/gpu-zedbsd-host.c" "$repo/userland/desktop/wayland/gpu-zedbsd.c" \
+        -I"$work/include" -I"$repo/include" -I"$repo" \
+        "$repo/plan/tools/gpu-boundary/gpu-zedbsd-host.c" "$repo/userland/desktop/wayland/zedbsd/gpu-zedbsd.c" \
```

`plan/tools/gpu-boundary/gpu-zedbsd-host.c`: 20 行 `#include "zwl-gpu.h"` → `#include "userland/desktop/wayland/zedbsd/gpu-zedbsd.h"`、10 行の注釈の path を直す。
**host の試験は `-std=c89` で build するので、新しい header（`zwl-gpu.h`・`gpu-zedbsd.h`）は C89 で通る書き方にする**（`//` の注釈を使わない、宣言を block の先頭に、など）。
`run-dedicated-host.sh`・`dedicated-host.c` は関係しない（`libvulkan/dedicated.c` の試験）。

### Makefile（5-9 行）

```make
# The operating system's side of the compositor (zwl-gpu.h, WS103; raised by WS104): zedBSD's.
KEILAND_ZEDBSD_SOURCES := userland/desktop/wayland/zedbsd/gpu-zedbsd.c userland/desktop/wayland/zedbsd/gpu-buffer-zedbsd.c
```

9 行は `$(KEILAND_ZEDBSD_SOURCES) \`。`KEILAND_SOURCES`（`:=`）より前に定義すること。

---

# P005: 入力

## `input.c` の中身

- **残す code と共有の static**: `BITMAP_WORD_BITS`（73-74）・`bit_is_set`（570-585）・`multitouch`（1122-1152）・`read_ranges`（541-568）は残す。
  `struct input_capabilities`（76-86）は `zwl-input.h` へ `struct zwl_input_caps` として移す。大きさは `EV_MAX / (8U * sizeof(unsigned long)) + 1U` と `KEY_MAX`・`REL_MAX`・`ABS_MAX` の同じ式
  （`BITMAP_WORD_BITS` でなく式を直書き）。zedBSD の値: `EV_MAX` 0x1f、`KEY_MAX` 0x2ff、`REL_MAX` 0x0f、`ABS_MAX` 0x3f（`include/uapi/input.h:59,198,226,238`、Linux と同じ値）。
- **`zedbsd/input-zedbsd.c` へ移す**: `INPUT_DIRECTORY`（33-34）、`zwl_input_scan`（105-161、そのまま）、`event_node_name`（361-383、そのまま）、`device_open`（385-408、そのまま。`zwl.h` の
  `ZWL_INPUT_MAX` を使う）、`probe_device` の open の半分（429-439）、`read_capabilities`（506-539、型の名前の変更だけ）。
- `ZWL_INPUT_*` の macro は `zwl.h`（62-67）に残す（両方が `zwl.h` を include する）。
- 他の header: `<uapi/input.h>`・`<sys/ioctl.h>` を include するのは `tablet.c:34`・`touch.c:80`（`sys/ioctl.h`）だけ。`menu-shell.c` は注釈で `<uapi/input.h>` に触れるだけ。
  `keymap.c`・`seat.c` はどちらも include しない。evdev の型は `zwl.h:44` を通って全ての file に届く。

## header

`zwl-evdev.h`: guard の中は次だけ（と phase005 の phase.md の注釈）。zedBSD の clang は `__linux__` を定義しない（確かめ済み）。

```c
#if defined(__linux__)
#include <linux/input.h>
#else
#include <uapi/input.h>
#endif
```

`zwl-input.h`: guard、`#include "zwl-evdev.h"`・`<stddef.h>`・`<stdint.h>`・`<sys/types.h>`、`struct zwl_server;`、`struct zwl_input_caps`（4 つの配列と役割の注釈）、そして:

```c
void zwl_input_scan(struct zwl_server *server);
int zwl_input_device_absinfo(int descriptor, uint32_t axis, struct input_absinfo *info);
int zwl_input_device_name(int descriptor, char *name, size_t size);
int zwl_input_device_id(int descriptor, struct input_id *id);
ssize_t zwl_input_device_read(int descriptor, struct input_event *events, size_t capacity);
void zwl_input_device_close(struct zwl_server *server, int descriptor);
void zwl_input_probe(struct zwl_server *server, int descriptor, const char *path, const struct zwl_input_caps *capabilities);
```

`zwl.h`: 44 行を `#include "zwl-evdev.h"` と `#include "zwl-input.h"` の 2 行に。1195 行（`zwl_input_scan`、`zwl-input.h` へ移った）を消す。

## `zedbsd/input-zedbsd.c`

include: `"userland/desktop/wayland/zwl.h"`・`<sys/ioctl.h>`・`<dirent.h>`・`<fcntl.h>`・`<unistd.h>`・`<errno.h>`・`<stdio.h>`・`<string.h>`。
static: `event_node_name`・`device_open`・`probe_device`・`read_capabilities(int, struct zwl_input_caps *)`。`zwl_input_scan` は一字一句同じ（`probe_device(server, path)` を呼ぶ。中身は open の半分になる）:

```c
/* Opens one node, reads its bits and hands it to the seat's classification (input.c). */
static void
probe_device(
	struct zwl_server *server,
	const char *path)
{
	struct zwl_input_caps capabilities;
	int descriptor;
	int error;

	/* The seat only reads, never blocks and does not pass the node to children. */
	descriptor = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (descriptor < 0)
		return;

	/* The capability bitmaps decide what kind of device this is. */
	error = read_capabilities(descriptor, &capabilities);
	if (error != 0) {
		close(descriptor);
		return;
	}

	/* The seat classifies the node and keeps it, or closes it (input.c). */
	zwl_input_probe(server, descriptor, path, &capabilities);
}
```

wrapper:

- `zwl_input_device_absinfo`: `result = ioctl(descriptor, EVIOCGABS(axis), info); if (result < 0) return errno; return 0;`
- `zwl_input_device_name`: 同じく `EVIOCGNAME(size)`（Linux は長さを返すので、0 以上は成功）。
- `zwl_input_device_id`: 同じく `EVIOCGID`。
- `zwl_input_device_close`: `(void)server; close(descriptor);`
- `zwl_input_device_read`:

```c
	/* Reads as many whole events as the buffer holds. */
	bytes = read(descriptor, events, capacity * sizeof(events[0]));
	if (bytes < 0)
		return -1;            /* errno is read()'s */

	/* The end of the device. */
	if (bytes == 0)
		return 0;

	/* A torn event means the node no longer speaks evdev. */
	if (((size_t)bytes % sizeof(events[0])) != 0) {
		errno = EIO;
		return -1;
	}

	/* Succeeded: how many whole events. */
	return bytes / (ssize_t)sizeof(events[0]);
```

## `input.c` の変更

- include: `<sys/ioctl.h>`・`<dirent.h>`・`<fcntl.h>`・`<unistd.h>`（25-28 行）を消す（変更の後 `unistd` の他の使い方は無い）。
- 定義: 33-34 行と 76-86 行を消す。
- 前方宣言: 88-91 を消す。103 行 → `static int multitouch(const struct zwl_input_caps *capabilities);`、1125 行も同じく。
- 105-161 行: `zwl_input_scan` を public な `zwl_input_probe(server, descriptor, path, const struct zwl_input_caps *capabilities)` に置き換える。注釈は phase005 の phase.md の物、
  宣言は 417-427 から `capabilities`・`descriptor` を除いた物、本体は 441-503 で `capabilities.` → `capabilities->`、`multitouch(&capabilities)` → `multitouch(capabilities)`、
  491 行の `close(descriptor)` → `zwl_input_device_close(server, descriptor)`。
- 361-539 行を消す（ただし `read_ranges`（541-568）は残す）。
- `read_ranges`: 552-554・558-560 行 → `error = zwl_input_device_absinfo(descriptor, ABS_X, x); if (error != 0) return error;` と `ABS_Y`・`y` の同じ物。
- `zwl_input_read`（239-291）: local を `ssize_t count;` にし、`bytes` と古い `size_t count` を落とす。257-286 行を:

```c
		/* Read as many whole events as the buffer holds. */
		count = zwl_input_device_read(device->fd, events, sizeof(events) / sizeof(events[0]));
		if (count < 0) {
			error = errno;
			(262-273 行はそのまま)
		}

		/* End of file means the node is gone. */
		if (count == 0) {
			printf("ZWL INPUT_CLOSED device=%s errno=%d\n", device->path, EIO);
			zwl_input_close(server, device);
			return;
		}

		/* Apply the events in the order the device produced them. */
		for (index = 0; index < (size_t)count; index++)
			consume_event(server, device, &events[index]);
```

  切れた event は errno EIO で `count < 0` の枝に入り、同じ `errno=5` の行を出す。（EOF の時の今の log の行と同じ形か、今の code と照らして確かめる。）
- `close` の呼び出し: 195・314（`device->fd`）・349（`server->inputs[index].fd`）・1030・1043・1090・1103 行 → `zwl_input_device_close(server, <同じ fd>)`。

## `tablet.c` の `read_axes`（500-551）と `touch.c` の `read_axes`（659-692）

tablet.c（各行の周りは残す）: 509 行（`ABS_X`）→ `error = zwl_input_device_absinfo(descriptor, ABS_X, &device->axis_x); if (error != 0) return error;`。512・517・523/525 行も
`ABS_Y`・`ABS_PRESSURE`・`ABS_TILT_X`・`ABS_TILT_Y` で同じく（tilt の 2 つは `if (error == 0)` の判定を残す）。537 行 →
`error = zwl_input_device_name(descriptor, device->name, sizeof(device->name) - 1U); if (error != 0) device->name[0] = '\0';`。543 行 → `error = zwl_input_device_id(descriptor, &identity);`
（`error == 0 && ...` の判定はそのまま）。34 行の `#include <sys/ioctl.h>` を消す。

touch.c: 668・673・685 行を `ABS_MT_POSITION_X`・`ABS_MT_POSITION_Y`・`ABS_MT_SLOT` で同じく（それぞれ `if (error != 0) return error;`）。80 行を消す。

kernel は成功で 0 を返す（確かめ済み）ので、`< 0` と `== 0` の判定を「0 か errno を返す wrapper」に替えても全く同じ動きになる。
（置き換えの前に、各行の今の判定が `< 0` か `!= 0` かを読み、同じ意味になるように書く。）

Makefile: `userland/desktop/wayland/zedbsd/input-zedbsd.c` を `KEILAND_ZEDBSD_SOURCES` に足す。

---

# P006: session と OS の hook

## `zwl-os.h`

guard、`<poll.h>`・`<stddef.h>`・`<vulkan/vulkan.h>`、`struct zwl_server;`、phase006 の phase.md の 6 つの prototype と下の `zwl_os_display_release`（計 7 つ）。注釈に: `zwl_os_close` は `zwl_os_open` が失敗しても終わりに呼ばれる、
`descriptors` は module の最初の entry を指す。`main.c`（17 行の後）・`compose.c`（24 行の後）・`os-zedbsd.c` から include する。

## `zedbsd/os-zedbsd.c`

`#include "userland/desktop/wayland/zwl-os.h"`。全ての関数は引数を `(void)` にする。`zwl_os_open` → 0、`zwl_os_poll_count` → 0、`zwl_os_display_acquire` → `VK_SUCCESS`、他は空。
file の注釈は phase006 の phase.md の物。

## `zedbsd/handoff-zedbsd.c`

`git mv` し、26 行 `#include "zwl.h"` → `#include "userland/desktop/wayland/zwl.h"`（quote の include はこの 1 つだけ）。

## `main.c`

起動の error は stdout に `printf("ZWL <PART> unavailable errno=%d\n", error)`（139 行、compose の失敗）。`error != 0` のまま `service_cleanup` に進み、`ZWL EXIT ... error=%d` を出して 1 を返す。

起動: 124 行（SIGTERM の handler）の後、126 行（prefetch）の前に入れる（失敗のとき prefetch の thread を起こさないように）:

```c

	/* The OS's part of the start comes before the Vulkan device (zwl-os.h: the seat on Linux, nothing on zedBSD). */
	if (error == 0) {
		error = zwl_os_open(&server);
		if (error != 0)
			printf("ZWL OS unavailable errno=%d\n", error);
	}
```

（`error` の変数がこの時点で使われているか、`main` の今の流れを読んで確かめる。prefetch や compose の呼び出しが `if (error == 0)` の形でないなら、今の形に合わせる。）

後始末: 929 行（`zwl_preferences_close(server);`）の後（`zwl_input_cleanup` の後になる）:

```c

	/* The OS's part of the start is given back last (zwl-os.h). */
	zwl_os_close(server);
```

`event_loop` の poll の並び: `[0]` listener、`[1, first_input)` client、`[first_input, last_input)` device、`count` の位置に `frame_slot`（0 は無し）、`[first_fence, …)` fence。
611 行の後に local `size_t first_os;`・`size_t os_count;`。699 行（fence の数えの loop の終わり）の後:

```c

		/* The OS module's descriptors are polled last (zwl-os.h: logind's bus on Linux, none on zedBSD). */
		first_os = count;
		os_count = zwl_os_poll_count(server);
		count += os_count;
```

769 行（fence の詰めの loop の終わり）の後:

```c

		/* The OS module fills its own entries. */
		zwl_os_poll_fill(server, descriptors + first_os);
```

811 行（入力の読みの loop の終わり）の後:

```c

		/* The OS module handles what poll reported for its entries. */
		zwl_os_poll_done(server, descriptors + first_os);
```

（`descriptors` の配列の大きさが固定なら、OS の entry の分の余裕があるか確かめる。zedBSD では 0 個。）

## `compose.c` の `zwl_compose_output_open`（194-246）

`compose->physical` はある（`compose.h:109`）。`VkDisplayKHR` は無い（上の 0 の 1）。

- `compose.h`: 109 行の後に、注釈 `/* The display window mode shows on (compose_display chose it; zwl_os_display_acquire is given it). */` と field `VkDisplayKHR display;`。
- `compose_display`: 750 行の後に:

```c

	/* The OS module is given this display before the swapchain claims it (zwl_compose_output_open). */
	compose->display = display;
```

- `zwl_compose_output_open`: 211 行の後、213 行の swapchain の注釈の前に:

```c

	/* The OS hands Vulkan the display's device before the swapchain claims the display (zwl-os.h; nothing on zedBSD). */
	result = zwl_os_display_acquire(server, compose->physical, compose->display);
	if (result != VK_SUCCESS) {
		printf("ZWL VULKAN_ERROR operation=display-acquire result=%d\n", (int)result);
		vkdemo_display_close(compose->instance, compose->device, &compose->output);
		compose->output_prepared = 0;
		return EIO;
	}
```

swapchain の失敗の道（219-222 行）と同じ形。呼ぶ所は `display.c:452` だけ（449 行の `zwl_handoff_wait` の後）。
注意: `vkdemo_display_open` は、恒等の transform の最初の display に使える mode か plane が無いと後の display を選ぶことがある。`compose_display` は既に両方が同じ display を選ぶ前提で書かれている。

## `zwl_os_display_release`（2026-10-01 の design-reviewer の指摘で追加）

Linux では、swapchain を消した後に `vkReleaseDisplayEXT` を呼び（logind の pause・resume で display を取り直すため、WS105 design §5.5）、acquire の対になる hook が要る。
`zwl-os.h` に 7 つ目の関数を足す:

```c
/* Called after the swapchain is gone (zwl_compose_output_close): gives the display back to the OS (Linux: vkReleaseDisplayEXT).  zedBSD: nothing. */
void zwl_os_display_release(struct zwl_server *server, VkPhysicalDevice physical, VkDisplayKHR display);
```

`zedbsd/os-zedbsd.c` の実装は空（`(void)` の 3 行）。呼ぶ所: `compose.c` の `zwl_compose_output_close`（252-281 行）の、`vkdemo_display_close` の後（swapchain が消えた後）:

```c

	/* The OS takes the display back now that the swapchain is gone (zwl-os.h; nothing on zedBSD). */
	zwl_os_display_release(server, compose->physical, compose->display);
```

（`zwl_compose_output_close` が `server` を受け取っているか確かめる。受け取らない形なら、`compose` から server に届く field を使うか、呼ぶ側（`handoff`・`compose_close`）で呼ぶ。
output が開いていない時（`output_prepared == 0`）は呼ばない。）

## Makefile と参照の確かめ

最後の形:

```make
KEILAND_ZEDBSD_SOURCES := userland/desktop/wayland/zedbsd/gpu-zedbsd.c userland/desktop/wayland/zedbsd/gpu-buffer-zedbsd.c \
	userland/desktop/wayland/zedbsd/input-zedbsd.c \
	userland/desktop/wayland/zedbsd/handoff-zedbsd.c userland/desktop/wayland/zedbsd/os-zedbsd.c
```

18 行から `userland/desktop/wayland/handoff.c` を外す。phase006 の手順 5 の grep `wayland/handoff.c` は `Makefile:18`・`plan/ws104/phase006/phase.md`・`plan/ws035/phase101/phase.md` だけに当たる
（最後は他の WS の記録なので変えない）。直す script は無い。
