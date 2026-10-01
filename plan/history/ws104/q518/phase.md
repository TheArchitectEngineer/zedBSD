<!-- awesome-plan project=zedbsd record=ws104-p004 -->

# ws104-p004: compositor の GPU の buffer の境界を引き上げる

Status: cleared
Disposition: normal
Parent: [WS104](../../../ws104/ws.md)
Queue: q518 / q518-i01
実行者: Codex Q1 / main（現在の Master N=0。委任せず実行した実績）

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
uint32_t zwl_gpu_device_extensions(VkPhysicalDevice physical, const char **names, uint32_t capacity);

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

## 手順（`<W>` は `ws104-p004`）

**正確な編集（行・code）は [edits-compositor.md](../design/edits-compositor.md) の「P004」にある。** その順に行う。上の「新しい境界」との差（survey で決めた物）:

- `zwl-gpu.h` に `struct zwl_object;` の前方宣言を足す。`#include <vulkan/vulkan_external.h>` を消す（zedBSD の `vulkan_core.h:5679` が既に include するので振る舞いは同じ。
  Linux の host の header には無い）。
- `zwl_gpu_device_extensions` は `VkPhysicalDevice physical` を最初の引数に取る（Linux の module が後段にある拡張だけを返すため）。
- `compose.c:1949` の `vkGetFenceFdKHR` の直接の呼び出しを、`vkGetDeviceProcAddr` で得た pointer（`compose->get_fence_fd`）に替える（Linux の loader は拡張の関数を export しない）。
- `objects.c` に `#include "compose.h"` を足す。
- 新しい header（`zwl-gpu.h`・`zedbsd/gpu-zedbsd.h`）は C89 で通る書き方（host の試験が `-std=c89` で compile する）。
- 道具の diff（`plan/tools/gpu-boundary/` の 3 file）は main が当てる（subagent は diff を main に送る）。

1. 編集する（edits-compositor.md の P004 の 1〜7 と「`vkGetFenceFdKHR` を直接呼ばない」「`<vulkan/vulkan_external.h>` を消す」）。
2. 残りが無いこと:
   ```
   grep -rn zwl_buffer_layout userland/desktop | grep -v '/wayland/zedbsd/' | wc -l          # 0
   grep -n 'factory_request\|factory_fence\|factory_alpha\|zwl_import_create' userland/desktop/wayland/*.c userland/desktop/wayland/*.h | wc -l   # 0
   grep -rn 'vulkan_external.h' userland/desktop/wayland | wc -l                                 # 0
   grep -n 'vkGetFenceFdKHR(' userland/desktop/wayland/*.c | wc -l                               # 0（pointer の呼び出しだけ）
   ```
3. build と warning の数え（[commands.md](../../../tools/keiland-linux/zedbsd-commands.md) §1、`build/ws104-p004/build.log`）。
4. GPU の境界の試験の全部（commands.md §6、`<W>` を `ws104-p004` に）: `v1-check: PASS`、`dedicated-host: PASS`×2、`gpu-zedbsd-host: PASS`×2、`forge-guest: PASS`、`fence-guest: PASS`。
5. compositor の基準（commands.md §5）: results.txt が全て PASS（C1 2 行・C2 1 行・C9 10 行）。
6. boot test（commands.md §4、`OUTPUT=build/ws104-p004/boot`）。
7. commit: `git commit -m WIP -- userland/desktop/wayland`（main は `plan/tools/gpu-boundary` も）。

## 完了の条件

- 手順 2 の 4 つが 0、手順 3〜6 が PASS。log の行の形が変わっていない（fence-guest・forge-guest・criteria が行を読んで PASS することで確かめられる）。

## 結果

q518-i01 cleared（2026-10-01T03:10:03.009057+00:00）。

GPU の wire layout・zedBSD GPU protocol/global・import を OS module に閉じ、共通 import は Vulkan image / memory の adopt だけにした。共通 layout、旧 factory、vulkan_external include、直接 vkGetFenceFdKHR は 0。device proc pointer で fence を呼ぶ。amd64 disk-image build exit 0、自前 warning 0。GPU v1 check（52 source）、dedicated host 18 cases × ordinary/sanitize、decode host 17 cases × ordinary/sanitize、forge guest、600 fence（全て generation 1）と 600 frames、C1/C2/C9 13/13、boot 全て PASS。p072 の全 6 PNG を目視し Wiseview・drag・復元・desktop 移動を確認。login PNG も目視・提示済み。証拠: `plan/history/ws104/q518/`。clang-format 19.1.7 と新 module / common import の style-check を実施（0）。全文規約の最終確認は p008。実機・Linux は未実施。旧 protocol/log と通常 path を保持し、import 失敗時の所有権を module で明示した。未達条件なし。


Validated checkout HEAD: `69f0f2c030becfa426fb98e2be9ad1c324488e4a`（WIP）。詳細 log: `build/ws104-p004/`（一時物）。実機・Linux は未実施。GitHub へは未公開。

Execution started UTC: 2026-10-01T02:34:59.246799+00:00。Approval: current user, 2026-10-01「お、いい調子ですね！その調子で、ws104の完了まで自律的に作業を進めてください。」。既存 WS104 p002〜p008 全範囲、依存順の 1 Phase Queue と検証・記録・WIP commit を承認。push / GitHub 公開は承認対象外。

### Checkpoint 2026-10-01T02:46:06.727450+00:00 / q518

実装 `5d413c08`（WIP）。境界 4 項目の旧参照 0、amd64 build exit 0・自前 warning 0、style-check（新しい GPU module/header・import）0、V1 52 source compile・host 18 case/17 case の ordinary と sanitizer・boot・forge guest・fence guest PASS（600/600 fence、generation 1、600 frames / 64 s）。証拠は `plan/history/ws104/q518/`。C1・C2・C9 を実行中で、p004 は in-progress。p008 で共通 source の既存の規約の差も全文確認する。

### 証拠のラベルの訂正（2026-10-01）

q518 の実装 commit は `5d413c08051c50ef856dd81989f15d1989e81438`（WIP）。終了時の HEAD `69f0f2c030becfa426fb98e2be9ad1c324488e4a` は人間の screenshot 追加を含む検証時の checkout であり、WS104 の実装 commit として記したラベルを訂正した。q518 の cleared、試験結果、承認範囲は変わらない。
