# WS103 設計: compositor を libvulkan だけにする

ws103-p001（q508）の成果。WS103 の決定 D1〜D3（[ws.md](ws.md)）の上に立つ。Linux・FreeBSD の構成は [F-065](../future/F-065-keiland-portable.md)。
code の行番号は 2026-09-30 の main（`c0ee1745`）のもの。

## 1. 今の姿（調べた事実）

### 1.1 compositor（`userland/desktop/wayland/`）の GPU の直の ioctl

| ioctl | 場所 | いつ | 何に使うか |
| --- | --- | --- | --- |
| `GPU_GET_INFO` | `display.c:55`（`zwl_gpu_open`） | 起動時 1 回 | `GPU_CAP_SHARE`・`GPU_CAP_DISPLAY` の確かめ、log の driver 名 |
| `GPU_DISPLAY_QUERY` | `display.c:67` | 起動時 1 回 | 大きさの既定（`preferred_*`、`--width`・`--height` が無いとき）、`CONNECTED`・`BLOB` の確かめ、`display_id`・`generation`（CLAIM だけが使う） |
| `GPU_DISPLAY_MODE`（VALIDATE） | `display.c:91` | 起動時 1 回 | 大きさの確かめと `server->refresh`（PRESENT だけが使う） |
| `GPU_RESOURCE_IMPORT` | `display.c:124`（`zwl_gpu_import`、`protocol.c:1218` から） | buffer ごと | client が wire で送った 64 byte の記述を kernel の値と `memcmp`（`display.c:129`）。kernel の記述を `buffer->image` に持ち、`import.c` がそれで Vulkan の image を作る |
| `GPU_RESOURCE_DESTROY` | `objects.c:594` | buffer の破棄 | 上の import の handle を捨てる |
| `GPU_FENCE_QUERY` | `protocol.c:1645`（`factory_fence`）、`display.c:316`（`zwl_fence_ready`） | commit ごと、schedule の pass ごと | fence が本物か（受け取り時）、世代 `generation` が済んだか（待たずに） |
| `GPU_DISPLAY_CLAIM`・`PRESENT`・`RELEASE` | `display.c:793`・`254`・`161` | `--direct` の道だけ | D2 で消す |

- window mode の表示は既に Vulkan だけ（`vkdemo/display.c` の VK_KHR_display と swapchain、`compose.c`）。画面の claim・release は libvulkan の swapchain の
  作成・破棄の中（`libvulkan/wsi-display.c:459-746`、`wsi-swapchain.c:749`・`1064`）。greeter と session の受け渡し（`handoff.c`）はこれに乗っている。
- `zwl.h:43-46` が `uapi/gpu.h`・`gpu-display.h`・`gpu-fence.h`・`input.h` を include し、`struct gpu_resource_import image`（`zwl.h:292`）、
  `struct gpu_display_info display`・`lease`・`gpu`（`zwl.h:598-608`）が構造体に入っている。`input.h`（evdev）は対象の外。
- compositor 自身の frame の fence は `vkGetFenceFdKHR`（OPAQUE_FD）で fd にして poll している（`compose.c:775`・`1746`、`main.c:767`）。ioctl ではない。

### 1.2 libvulkan の側

- 画面: `VkDisplayPropertiesKHR.physicalResolution` は `GPU_DISPLAY_QUERY` の `preferred_width/height`（`libvulkan/wsi.c:120`、`wsi-display.c:154`）。
- buffer の import: client の WSI は image の capability の fd（`GPU_RESOURCE_EXPORT`）を送る。compositor の `vkAllocateMemory`（OPAQUE_FD）は、
  Venus では `GPU_ALLOCATION_IMPORT` が EINVAL で、i915 では `GPU_CAP_ALLOCATION_SHARE` が無いので、どちらも `memory_import_image_fd`
  （`libvulkan/memory.c:820-900`）に入り、内部で `GPU_RESOURCE_IMPORT` を呼ぶ。確かめるのは memory type と大きさだけで、kernel の記述（幅・高さ・形式・stride・offset）は捨てている。
- `vkBindImageMemory`（`resources.c:147`）は wire に投げるだけ。image の作成の値は local に持っている（`struct vulkan_image`、`internal.h:173`）。
- VK_KHR_dedicated_allocation・VK_KHR_get_memory_requirements2 は無い（Vulkan 1.0 と少数の KHR、`instance.c:468-505`）。
- fence: OPAQUE_FD だけ（`external-fence.c:124-128`）。OPAQUE の fd は再利用される kernel の fence（世代つき）への参照。client の swapchain は完了の fence を
  queue の slot ごとに再利用し（`wsi-swapchain.c:1960-2020`、`vkResetFences`）、present のたびに今の世代を `GPU_FENCE_QUERY` で読んで wire に載せる。
- **i915 の native には `GPU_CAP_FENCE` が無い**（`src/drivers/gpu/i915/session.h:38-47`）。fence を export できないので `present_early` が偽になり
  （`wsi-swapchain.c:2728`）、client の WSI が完了を待ってから commit する。**5330 では `set_acquire_fence` は送られない。** fence の道は Venus（QEMU、Windows の WINQ-EMU）だけ。
- kernel の fence の fd の poll は、payload が reset されるまで level で readable（`src/drivers/gpu/gpu-fence.c:543-571`）。

## 2. 置き換えの設計

### 2.1 起動の問い合わせ（GET_INFO・DISPLAY_QUERY・DISPLAY_MODE）→ VK_KHR_display

- 大きさの既定: `--width`・`--height` が無いとき、選んだ display の `VkDisplayPropertiesKHR.physicalResolution` を使う。
  `vkdemo_display_open` が選ぶのと同じ display（最初の display）から取る。取れない（0）なら起動を失敗させる。
- 大きさの確かめ: `vkdemo_display_open` の `choose_mode`（同じ大きさの mode が無ければ失敗）が既に行っている。VALIDATE は要らない。
- `GPU_CAP_SHARE` の確かめ: device の拡張 `VK_KHR_external_memory_fd` の有無（`compose.c` が既に要求）で代わる。`GPU_CAP_DISPLAY`・`BLOB` の確かめは
  `--direct` の削除で理由が無くなる。
- log の driver 名は `VkPhysicalDeviceProperties.deviceName` にする（`ZWL GPU` の行の形は変わる。行を読む試験は p002 で直す）。
- 順序の変更: 今は `zwl_gpu_open`（`main.c:134`）が `zwl_compose_open` の前で大きさを決める。大きさの問い合わせは Vulkan の instance が要るので、
  `zwl_compose_open` の中（physical device を選んだ直後、壁紙と glyph の前）で大きさを決める。大きさを先に使う処理（pointer の初期位置 `main.c:137-139`、
  `zwl_glass_prefetch`）の順を p002 で確かめて直す。

### 2.2 `--direct` の削除（D2）

消すもの: `--direct` の option と usage、`server.direct`、`schedule_direct`・`zwl_present`・`claim_display`・`zwl_unscan`、`server->lease`・`front`・`refresh`・`display`、
`zwl_compose_open` が失敗したときに直の表示へ落ちる道（`main.c:142-149`。失敗は起動の失敗にする）、`zwl_import_create` の「compose が無ければ何もしない」の分岐（`import.c:49`）。
`enter_window_mode`（`display.c:745`）と `zwl_handoff_release`（`handoff.c`）の `zwl_unscan` の呼び出しは、window mode では何もしていない（lease が常に 0）ので消す。
`zwl_handoff_release` の本体（swapchain を壊して lease を返し `RELEASED` を書く）は残す。

### 2.3 buffer の記述の照合（RESOURCE_IMPORT・DESTROY）→ libvulkan の中（D3）

**設計の修正（D3 の具体化）**: D3 は「image を bind するとき libvulkan が照らす」とした。しかし `vkBindImageMemory` が返してよい error は規格で
OOM 系などに限られ、「記述が合わない」を正しく返せない。そこで **標準の VK_KHR_dedicated_allocation を使い、import の `vkAllocateMemory` の時に照らす**
（`VkMemoryDedicatedAllocateInfo.image` で import する image を名指しし、合わなければ `VK_ERROR_INVALID_EXTERNAL_HANDLE`。これは `vkAllocateMemory` の正しい戻り値）。
Linux の dma-buf の import も dedicated で行うのが普通で、F-065 の形と揃う。照らす主体が libvulkan である点は D3 のまま。

libvulkan の変更:

1. device の拡張 `VK_KHR_get_memory_requirements2`（`vkGetImageMemoryRequirements2KHR` などを 1.0 の問い合わせで local に包む）と
   `VK_KHR_dedicated_allocation`（`VkMemoryDedicatedRequirementsKHR` を返す。dedicated の情報は wire に載せず local で消費する）を足す。
2. `memory_import_image_fd` は、pNext に `VkMemoryDedicatedAllocateInfo`（image が非 NULL）があるとき、kernel が返した記述（`request.image`）と
   その image の local の値を照らす: 2D、mip 1、layer 1、sample 1、`VK_IMAGE_TILING_LINEAR`、extent が記述の幅・高さ、format が記述の形式
   （`GPU_PIXEL_BGRA8888` ↔ `VK_FORMAT_B8G8R8A8_UNORM`、`GPU_PIXEL_RGBA8888` ↔ `VK_FORMAT_R8G8B8A8_UNORM`。今の `import.c` の対応と同じ）、
   `vkGetImageSubresourceLayout` の offset と rowPitch が記述の offset と stride、image の memory の大きさが allocation の中に収まる。
   合わなければ alias を捨てて `VK_ERROR_INVALID_EXTERNAL_HANDLE`。
3. dedicated の情報の無い image の capability の import の扱い: **拒む**（`VK_ERROR_INVALID_EXTERNAL_HANDLE`）のが安全。ただし既存の利用者
   （`userland/base/tests/gpu-share/main.c`、他に import する物が無いことは p003 で grep で確かめる）を dedicated に直す。拒むか許すかは p003 の試験で決め、phase.md に書く。

compositor の変更: `import_image` が wire の記述から作った image を dedicated で import する。`zwl_gpu_import`・`GPU_RESOURCE_DESTROY`・
`buffer->image`（`struct gpu_resource_import`）を消し、wire の記述を compositor の型（`struct zwl_buffer_layout`: 幅・高さ・VkFormat・stride・offset・
memory type・allocation の大きさ）に写して持つ。写すのは OS の backend（2.5）。

V3 の安全性: 他の client の画像は、fd が capability なので名指しできない（今と同じ）。記述を偽った buffer は libvulkan の照合で拒まれる（今の `memcmp` と同じ強さ）。

### 2.4 fence（FENCE_QUERY）→ 標準の SYNC_FD の fence

**問題**: OPAQUE の fence の fd は再利用される kernel の fence への参照で、「この commit の世代が済んだか」は標準の Vulkan で聞けない。
compositor が OPAQUE で import して `vkGetFenceStatus` を見ると、client が次の frame で fence を reset した後は、前の commit が済んでいても未完了に見える。
世代を wire で運ぶ今の形は、この参照の意味のためにある。

**設計**: libvulkan に標準の `VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT` を足す（Linux の sync_file と同じ「複写」の意味。F-065 の Linux の組とも揃う）。

- export（`vkGetFenceFdKHR`、SYNC_FD）: fence の今の kernel の payload（この submit に bind した kernel の fence object）の fd をそのまま渡し、VkFence には
  新しい kernel の fence（`GPU_FENCE_CREATE`）を付け直す。規格の「SYNC_FD の export は fence の reset と同じ副作用」に一致する。渡した kernel の fence は以後
  reset されないので、その fd は「この submit の完了」だけを表し、poll は完了で readable のまま（`gpu-fence.c:543`）。kernel の変更は要らない。
  submit されていない fence の export は規格どおり（signal 済みなら signal 済みを表す fd、未 submit の未 signal は不可）にする。
- import（`vkImportFenceFdKHR`、SYNC_FD）: 規格どおり一時の import だけ。状態は `GPU_FENCE_QUERY`（libvulkan の内部）で読む。
- `VkExternalFenceProperties` で SYNC_FD の export・import を示す（`GPU_CAP_FENCE` のある device だけ。i915 の native は今のまま無し）。
- client の WSI（`wsi-swapchain.c`）: present の完了の fence を SYNC_FD で export し、`set_acquire_fence` で送る。世代は要らなくなる。
  protocol は `keiland_gpu_buffer_v1` の revision 4 で世代の無い request（例 `set_acquire_sync(surface, fd)`）を足す。revision 2 の request は旧い client のために
  compositor が受け続けるか、同時に更新する（zedBSD の base の中で client と compositor は一緒に入れ替わる）かを p005 で決める。推奨は同時の更新で、旧 request は受けない（protocol の error）。

compositor の変更: `factory_fence` は fd を受け、組の VkFence（pool）に SYNC_FD で一時の import をする（import の失敗は protocol の error。今の「本物の fence か」の確かめに当たる）。
`zwl_fence_ready` は `vkGetFenceStatus` で見る（`VK_SUCCESS` で済み、`VK_NOT_READY` で待つ、`VK_ERROR_DEVICE_LOST` は今の「読めない fence」と同じく待たない）。
main loop の poll は今どおり fd の POLLIN で起きる（fd は import の後も compositor が dup して持つ。SYNC_FD の import は fd を消費するため）。

費用: client は present ごとに `GPU_FENCE_CREATE` 1 回（今の `vkResetFences` の代わり）と、今の世代の `GPU_FENCE_QUERY` が無くなる。compositor は commit ごとに
import の `GPU_FENCE_QUERY` 1 回（今の `factory_fence` の 1 回と同じ）と、pass ごとの `vkGetFenceStatus`（今の `zwl_fence_ready` の 1 回と同じ）。増減はほぼ 0 の見込み。V4 で測る。

代わりの案（採らない）: (b) 世代を wire に残し、compositor の zedBSD の backend に `GPU_FENCE_QUERY` を macro で残す。V1 の「移せない物だけ macro」には入るが、
移せる道があるので採らない。(c) kernel に「世代の snapshot の fd」を作る UAPI を足す。上の設計で kernel の変更なしにできるので採らない。

### 2.5 OS の backend の境界

compositor の OS 固有の部分を 1 つの source の module に集める（macro の分岐を共通の code に散らさない。D1、F-065 の決定 5 の「module の入れ替え」）。

- `userland/desktop/wayland/gpu-zedbsd.c`（新規）: `keiland_gpu_buffer_v1` の payload の解読（64 byte の `gpu_image_descriptor` → `struct zwl_buffer_layout`、
  `GPU_PIXEL_*` → `VkFormat`）、import に使う handle の種類（`VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT`）と pNext の追加の組み立て（zedBSD では無し。
  Linux では `VkImageDrmFormatModifierExplicitCreateInfoEXT` などが入る所）、fence の handle の種類（SYNC_FD）。`uapi/gpu*.h` を include するのはこの file だけ。
- `zwl-gpu.h`（新規、OS 共通）: 上の境界の関数の宣言と `struct zwl_buffer_layout`。
- Makefile が OS で module を選ぶ（今は zedBSD だけなので `gpu-zedbsd.c` を常に入れる）。Linux・FreeBSD の module（dma-buf、sync_file）は F-065 で作る。
- compositor 自身の frame の fence（`compose.c` の OPAQUE_FD の export と poll）は ioctl ではないが、OPAQUE の fd を poll するのは zedBSD の意味なので、
  2.4 の後は SYNC_FD の export に替える（標準で、Linux でも poll できる）。p006 で行う。

### 2.6 macro を外した build の確かめ方（V1）

Linux の build はまだ無いので、次の script（`plan/ws103/tests/v1-check.sh`）で確かめる。

1. grep: `userland/desktop/wayland/` の中で `uapi/gpu`・`GPU_[A-Z_]*` の ioctl 名・`<sys/ioctl.h>` の GPU 用の利用が `gpu-zedbsd.c` の外に無い
   （`<sys/ioctl.h>` は evdev の `input.c`・`tablet.c`・`touch.c` に残る）。
2. 毒の header: `uapi/gpu.h`・`gpu-display.h`・`gpu-fence.h` を `#error` だけの file にした include の directory を先頭に置き、`gpu-zedbsd.c` 以外の全ての
   `.c` を host の clang で `-fsyntax-only` にかける（sysroot の include は zedBSD の build と同じ）。1 つでも GPU の UAPI を読めば失敗する。
3. `/dev/gpu0` の open と `--gpu` の option が無い（compositor は GPU の fd を持たない）。

### 2.7 V4（性能）の見込みと測り方

- buffer ごと: compositor の `GPU_RESOURCE_IMPORT`・`DESTROY` の 2 回が消え、libvulkan の照合に `vkGetImageSubresourceLayout` の往復が 1 回増える
  （Venus では wire の往復。compositor も同じ問い合わせを既にしているので、libvulkan が image ごとに結果を覚えて 1 回にできる）。app の起動の時の数回だけで、frame には無い。
- commit ごと・frame ごと: 2.4 のとおりほぼ同じ。
- 測り方: WS099 の import-launch（app の最初の frame）と WS075 の measure-apps（C6）を、p002 の前（基準）と p006 の後で QEMU の Venus と 5330 の passthrough で比べる。
  差が誤差を超えたら p007 で原因を分ける。

## 3. Phase の分け方

| Phase | 範囲 | 依存 | 確かめ |
| --- | --- | --- | --- |
| p002 | compositor: 2.1（起動の問い合わせを VK_KHR_display へ）と 2.2（`--direct` の削除）。`GET_INFO`・`DISPLAY_QUERY`・`DISPLAY_MODE`・`CLAIM`・`PRESENT`・`RELEASE` が消える。GPU の fd は import のためにまだ残る | — | build（warning 0）、greeter → login → Log Out → greeter（C1 の QEMU の部分）、`--width` なしの大きさ、boot test |
| p003 | libvulkan: 2.3 の 1〜3（dedicated allocation、get_memory_requirements2、import の照合）。gpu-share の試験を dedicated に | — | host の試験（偽の記述の buffer を拒む: 幅・高さ・形式・stride・offset のそれぞれ、正しい物は通る。V3）、Venus と i915 の import の試験 |
| p004 | compositor: 2.3 の compositor の側（dedicated で import、`zwl_gpu_import`・`RESOURCE_DESTROY`・`buffer->image` を消す）と 2.5 の `gpu-zedbsd.c`・`zwl-gpu.h` の buffer の部分 | p003 | app の起動と窓の操作（C9 の一部）、偽の記述の client（p003 の試験を compositor に向ける）、import-launch |
| p005 | libvulkan: 2.4 の SYNC_FD の export・import と WSI の切り替え、protocol の revision 4 | — | host の試験（export の後の reset、別 process の import、poll、未 submit の export）、Venus で窓の app（fence の道は Venus だけ） |
| p006 | compositor: 2.4 の compositor の側（SYNC_FD の import と `vkGetFenceStatus`、`FENCE_QUERY` を消す）、frame の fence を SYNC_FD に、`/dev/gpu0` と `--gpu` を消す、UAPI の型を `gpu-zedbsd.c` に閉じる、2.6 の `v1-check.sh` | p004、p005 | `v1-check.sh`（V1）、Venus で窓の app と Notes（WS079-p010）、boot test |
| p007 | 規約の全文（coding-style.md）で WS の全 source の変更を見直し、回帰（C1・C9・WS079-p010・boot test）、5330 の passthrough（V2）、V4 の計測 | p002〜p006 | 規約・build・試験・計測の記録 |

- p002 と p003・p005 は互いに独立。p004 は p003 の、p006 は p004 と p005 の後（どちらも `zwl.h` と `protocol.c` に触れる）。
- デモ（10/17、新規の実装は 10/10 ごろまで）との関係: fence の道（p005・p006）は 5330 では通らないが、Windows の QEMU（WINQ-EMU、Venus）の touch のデモで通る。
  p005・p006 は Venus の試験を必ず通す。p002 は greeter と session の受け渡しに触れるので、C1 の確かめを必ず行う。
- HAL（`hal.h`）・toolchain・kernel の変更は無い。libvulkan の拡張の追加（p003・p005）は WS103 の範囲の中（D1・D3）。

## 4. 危険と未確認

- `physicalResolution` が 0 の display（EDID の無い仮想の出力など）: 今は `preferred_*` が 0 なら既定の大きさのまま。同じ扱い（既定の大きさ）にする。p002 で確かめる。
- 大きさを決める時点が `zwl_compose_open` の中へ移ることで、壁紙・glass の prefetch の大きさが変わらないか（p002）。
- libvulkan の SYNC_FD の export で、queue の slot の再利用（`wsi-swapchain.c` の completion の cache）と、新しい kernel の fence を付け直す順序の競合（p005 の設計の細部）。
- dedicated の情報の無い import を拒むことで、他の利用者（X11 の server、EGL）が壊れないか（p003 の grep）。
