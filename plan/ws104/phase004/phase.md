<!-- awesome-plan project=zedbsd record=ws104-p004 -->

# ws104-p004: compositor の GPU の buffer の境界を引き上げる

Status: planned
Disposition: normal
Parent: [WS104](../ws.md)
Queue: なし
実行者: phase-runner（high）

## 目的

決定 D17。WS103 で作った OS の境界（`zwl-gpu.h` と `gpu-zedbsd.c`）は「client が wire で送った 64 byte の記述を decode する」所にある。
decode の結果 `struct zwl_buffer_layout` は共通の code（`import.c`・`objects.c`・`protocol.c` の log）が読む。Linux（WS105）の buffer は
`zwp_linux_dmabuf_v1` の params（plane ごとの fd・offset・stride・modifier）で届き、形が全く違う。そこで境界を
**「OS の protocol の request を処理し、client の buffer から VkImage と VkDeviceMemory を作る」** 所まで上げる。共通の code が buffer について知るのは、
OS の module が作った VkImage・VkDeviceMemory・幅・高さ・VkFormat・合成の仕方（opaque か alpha か）だけにする。

zedBSD の振る舞い（protocol・検査・log の行・性能）は変えない。

## 今の姿（2026-10-01 の行番号、`userland/desktop/wayland/`）

- `zwl-gpu.h`: `struct zwl_buffer_layout`（35-43）、`struct zwl_gpu_limits`（49-52）、`zwl_gpu_buffer_wire_bytes`・`zwl_gpu_buffer_decode`・`zwl_gpu_buffer_handle_type`（54-56）。
- `gpu-zedbsd.c`: 上の 3 関数の実装（`<uapi/gpu.h>` を読む唯一の file）。host の試験 `plan/tools/gpu-boundary/run-gpu-zedbsd-host.sh` がこの file を単独で compile する。
- `protocol.c`:
  - global の表 `globals[]`（47-71）の 50 行 `{ 3, "keiland_gpu_buffer_v1", 3, ZWL_FACTORY }`。`registry_events`（392-430）と `bind_global`（530-617）がこの表を読む。
  - `zwl_dispatch`（95-332）の 186-188 行で `ZWL_FACTORY` → `factory_request`。
  - `factory_request`（1161-1240）: opcode 0 destroy・1 create_buffer・2 `factory_fence`・3 `factory_alpha`。create_buffer は decode（1217、`buffer->layout` に書く）→
    `zwl_import_create(buffer, descriptor)`（1225）→ log `ZWL IMPORT client=%llu buffer=%u width=%u height=%u bytes=%llu`（1234）。
  - `factory_fence`（1619-1663）: surface の `acquire[]` に fd と generation を入れる。
  - `factory_alpha`（1671-1697）: `zwl_import_set_alpha`。
  - `commit_fence`（1557-1579）: commit で `acquire[]` を `fences[]` に移す。
- `import.c`: `zwl_import_create`（37-76）→ `import_image`（124-272）。`import_image` は前半（152-221）が **OS の物**（LINEAR の `vkCreateImage` と
  external memory の handle type、`vkGetImageMemoryRequirements`・`vkGetImageSubresourceLayout` と layout の照合、dedicated の `vkAllocateMemory` の import、
  `vkBindImageMemory`）、後半（226-270）が **共通の物**（`vkCreateImageView`、layout の移行の予約、descriptor set 2 つ）。
- `objects.c:186-187`: `zwl_buffer_size` が `buffer->layout.width/height` を読む（wl_shm でない buffer）。
- `zwl.h:291`: `struct zwl_object` の `struct zwl_buffer_layout layout;`。`zwl.h:43` が `"zwl-gpu.h"` を include。
- `compose.c`: `compose_device`（568-685）の拡張の一覧は固定。`compose_limits`（781-798）が `server->gpu_limits` を埋める。
  frame の fence を `vkGetFenceFdKHR(OPAQUE_FD)` で fd にする（1943-1954、`compose->fence_fd`）。

## 新しい境界（`zwl-gpu.h` の新しい中身）

`zwl-gpu.h` は「共通の code と OS の GPU の module の間の約束」になる。zedBSD の型（`zwl_buffer_layout`）はここから消える。

```c
/* The limits the compositor's device sets on client images (compose_limits fills them). */
struct zwl_gpu_limits { uint32_t max_dimension; uint32_t memory_type_count; };

/* The OS's Wayland global for client GPU buffers: its interface name and version (zedBSD: keiland_gpu_buffer_v1, 3). */
const char *zwl_gpu_global_interface(void);
uint32_t zwl_gpu_global_version(void);

/* A request on an object of the OS's GPU protocol (ZWL_FACTORY).  Returns 0, EAGAIN (the fd has not arrived; the
   frame is kept) or an errno value that ends the client (EPROTO ...), as factory_request does today. */
int zwl_gpu_request(struct zwl_object *object, uint32_t opcode, const unsigned char *bytes, size_t size);

/* A surface's commit attached a GPU buffer (called from commit_fence before the fences move).  zedBSD: nothing
   (its fences come with set_acquire_fence).  Linux (WS105): takes the buffer's implicit fence. */
void zwl_gpu_commit(struct zwl_object *surface, struct zwl_object *buffer);

/* The Vulkan extensions the OS module needs beyond the compositor's own: copies up to capacity names and returns how
   many there are.  zedBSD: none (0). */
uint32_t zwl_gpu_instance_extensions(const char **names, uint32_t capacity);
uint32_t zwl_gpu_device_extensions(const char **names, uint32_t capacity);

/* The handle type the compositor's own frame fence is exported as for the main loop's poll, or 0 for none (the loop
   then polls vkGetFenceStatus).  zedBSD: VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT. */
VkExternalFenceHandleTypeFlagBits zwl_gpu_frame_fence_type(void);
```

共通の側の新しい関数（`import.c`、`zwl.h` に宣言）:

```c
/* Makes buffer's import from an image and its bound memory the OS module made: the view, the layout change, the
   descriptor sets (what import_image's second half does today).  Takes the image and the memory: on failure they are
   destroyed.  Sets buffer->import.  Returns VK_SUCCESS or the error. */
VkResult zwl_import_adopt(struct zwl_object *buffer, VkImage image, VkDeviceMemory memory, uint32_t width, uint32_t height, VkFormat format);
```

- `zwl_import_create(buffer, descriptor)` は無くなる（zedBSD の module の中の関数になる）。`zwl_import_destroy`・`zwl_import_set_alpha` は共通のまま。
- `zwl_buffer_size` は GPU の buffer では `buffer->import->width/height` を読む（import は buffer の作成の成功と同時に必ず有る）。

## 新しい file（`userland/desktop/wayland/zedbsd/`）

| file | 中身 |
| --- | --- |
| `zedbsd/gpu-zedbsd.h` | zedBSD の module の内部の header: `struct zwl_buffer_layout`（`zwl-gpu.h` から移す）、`zwl_gpu_buffer_wire_bytes`・`zwl_gpu_buffer_decode`・`zwl_gpu_buffer_handle_type` の宣言（`zwl-gpu.h` から移す） |
| `zedbsd/gpu-zedbsd.c` | `gpu-zedbsd.c` を `git mv` した物。中身は include の行だけ変える（`#include "userland/desktop/wayland/zedbsd/gpu-zedbsd.h"`）。**`<uapi/gpu.h>` を読むのはこの file だけのまま** |
| `zedbsd/gpu-buffer-zedbsd.c` | `protocol.c` から移す `factory_request`（→ `zwl_gpu_request` に改名）・`factory_fence`・`factory_alpha`、`import.c` から移す `import_image` の前半（image の作成から `vkBindImageMemory` まで）と `zwl_import_create` の fd の `dup` の扱い。新しい hook（`zwl_gpu_global_interface` など）の zedBSD の実装。log の行 `ZWL IMPORT ...`・`ZWL IMPORT_ERROR ...`・`ZWL ACQUIRE_FENCE ...` は**今と同じ文字列**で、この file から出す |

## 手順

1. `mkdir userland/desktop/wayland/zedbsd`、`git mv userland/desktop/wayland/gpu-zedbsd.c userland/desktop/wayland/zedbsd/gpu-zedbsd.c`。
2. `zedbsd/gpu-zedbsd.h` を作り、`zwl-gpu.h` の `zwl_buffer_layout` と 3 つの宣言を移す。`zwl-gpu.h` を上の「新しい境界」の形にする（注釈は今の `zwl-gpu.h` の書き方に合わせ、
   「the boundary between the compositor and its OS's GPU module (WS103, raised by WS104 p004)」と書く）。
3. `import.c` を 2 つに分ける: 後半を `zwl_import_adopt` として残し、前半を `zedbsd/gpu-buffer-zedbsd.c` の static な関数 `buffer_image`（名前は任意、
   `import_image` の前半の中身そのもの）にする。error の時の後始末（作りかけの image・memory の破棄）が今と同じになるように、`import_release` が今やっている順を守る。
4. `protocol.c`:
   - `globals[]` の entry 3 の文字列と version は、表の中では NULL と 0 にし、`registry_events` と `bind_global` で kind が `ZWL_FACTORY` の時は
     `zwl_gpu_global_interface()`・`zwl_gpu_global_version()` を使う（他の entry の扱いは変えない）。名前 3 と version 3 は zedBSD では今と同じになる。
   - `zwl_dispatch` の `ZWL_FACTORY` を `zwl_gpu_request(...)` に。
   - `factory_request`・`factory_fence`・`factory_alpha` を `zedbsd/gpu-buffer-zedbsd.c` へ移す。これらが使う protocol.c の static な関数があれば、
     共通の関数として `zwl.h` に宣言して残すか（他の request も使う物）、一緒に移す（factory だけが使う物）。
   - `commit_fence` の、attach された buffer が GPU の buffer（`buffer->import != NULL` かつ `buffer->shm == NULL`）の時に `zwl_gpu_commit(surface, buffer)` を呼ぶ（fences を移す前）。
5. `objects.c` の `zwl_buffer_size` を import から読むように。`zwl.h` の `layout` の member を消す。
6. `compose.c`:
   - `compose_device` の instance・device の拡張の配列に、`zwl_gpu_instance_extensions`・`zwl_gpu_device_extensions` の名前を足す（配列の大きさに余裕を持たせる。zedBSD では 0 個）。
   - frame の fence の export の handle type を `zwl_gpu_frame_fence_type()` から取る。0 なら `compose->fence_fd = 0`（今の「fence の拡張が無い device」と同じ道）。
7. `Makefile`: `KEILAND_GPU_SOURCES` を `KEILAND_ZEDBSD_SOURCES := userland/desktop/wayland/zedbsd/gpu-zedbsd.c userland/desktop/wayland/zedbsd/gpu-buffer-zedbsd.c` に改名・変更
   （p005・p006 でここに足していく）。`KEILAND_SOURCES` の中の参照も直す。
8. 道具を直す（main の範囲の `plan/tools/` の物。subagent が実行するなら、直した差分を main に送って適用してもらう）:
   - `plan/tools/gpu-boundary/v1-check.sh`: `backend=$dir/zedbsd/gpu-zedbsd.c`。`$dir/*.c $dir/*.h` の glob を `$dir` 以下の全て（`find $dir -name '*.[ch]'`）に。
     `KEILAND_GPU_SOURCES` の名前を読んでいれば新しい名前に。
   - `plan/tools/gpu-boundary/run-gpu-zedbsd-host.sh`: compile する file を `userland/desktop/wayland/zedbsd/gpu-zedbsd.c` に、`-I.`（repo の root）を足す。
     `gpu-zedbsd-host.c` が `zwl-gpu.h` から layout を得ていれば `zedbsd/gpu-zedbsd.h` に。
9. `grep -rn zwl_buffer_layout userland/` が `wayland/zedbsd/` の中だけであること。

## 確かめ

1. build: `make -j16 BUILD=build/amd64 disk-image`、warning 0。
2. GPU の境界の試験（`plan/tools/gpu-boundary/`、各 script の先頭の使い方）:
   - `sh plan/tools/gpu-boundary/v1-check.sh` → `v1-check: PASS`
   - `sh plan/tools/gpu-boundary/run-gpu-zedbsd-host.sh`（17 件）、`sh plan/tools/gpu-boundary/run-dedicated-host.sh`（18 件）→ PASS
   - `sh plan/tools/gpu-boundary/build-forge-image.sh` で image（`build/ws103/p004-forge.img` か script の既定の出力）を作り、
     `plan/ws035/tests/zdesktop-guest.sh start <image>` の後 `forge-guest.sh` と `fence-guest.sh` → PASS
3. compositor の基準（Venus）: `plan/ws099/tests/build-criteria-image.sh` → `plan/ws099/tests/criteria.sh build/ws099-criteria.img build/ws104/p004-criteria C1 C2 C9` → 全て PASS。
4. boot test。

## 完了の条件

- `zwl_buffer_layout` が `wayland/zedbsd/` の中だけ、`protocol.c`・`import.c`・`objects.c`・`zwl.h` に keiland_gpu_buffer_v1 の request の処理と zedBSD の型が無い。
- 確かめ 1〜4 が PASS。log の行の形が変わっていない（fence-guest・forge-guest が行を読んで PASS することで確かめられる）。

## 結果

（実行の後に書く）
