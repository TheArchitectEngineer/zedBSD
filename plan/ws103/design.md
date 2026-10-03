# WS103 設計: compositor を libvulkan だけにする

ws103-p001（q508）の成果。2026-09-30 夜に design-reviewer のレビュー（§5）を受けて改訂した（改訂 2、再レビューの指摘を改訂 3 で処理）。WS103 の決定 D1〜D3（[ws.md](ws.md)）の上に立つ。Linux・FreeBSD の構成は [F-065](../future/F-065-keiland-portable.md)。
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

- 大きさの既定: `--width`・`--height` が無いとき、`vkdemo_display_open` が実際に選ぶ display の `physicalResolution` を使う（display が複数のとき
  最初の display とは限らない。再レビュー m3。libvulkan は接続した display だけを並べる）。0（EDID の無い仮想の出力など）なら今と同じく既定の大きさのまま。
- 大きさの確かめ: `vkdemo_display_open` は display を順に試し（`vkdemo/display.c:71-100`）、同じ大きさの mode が無ければ `vkCreateDisplayModeKHR` で作り
  （`:384-396`）、libvulkan がその大きさを kernel で確かめる（`libvulkan/wsi-display.c:250-255`）。VALIDATE は要らない。
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
   さらに kernel の記述の側の `tiling` が linear（`GPU_IMAGE_LINEAR`）で、`usage`（export した側の `VkImageUsageFlags` が入る、`libvulkan/wsi-image.c:176`）が `VK_IMAGE_USAGE_SAMPLED_BIT` を含むこと
   （含まない export が今あれば p003 で export の側を直す）（再レビュー A2。今の `memcmp` は 64 byte の全て、
   `tiling`・`usage` を含めて比べている）。
   `allocationSize` がその image の memory の要求の大きさと等しいこと（部分の import は拒む）。合わなければ alias を捨てて `VK_ERROR_INVALID_EXTERNAL_HANDLE`。
3. dedicated の情報の無い image の capability の import は**拒む**（`VK_ERROR_INVALID_EXTERNAL_HANDLE`）。image の capability を import するのは compositor だけで
   （`userland/base/tests/gpu-share/main.c` は allocation の export の道 `vkGetMemoryFdKHR` を使い、この道を通らない。レビュー m1）、壊れる利用者は無い。
4. bind の守り（レビュー M7）: 照合した memory に、照合した image を記録する。`vkBindImageMemory`（`resource_bind`）で、その memory に別の image か offset 0 以外で
   bind しようとしたら bind せず error を返す（規格上は利用者の誤り（VUID）なので、守りとして拒む）。
5. 意味の注記（レビュー m2）: 規格の OPAQUE_FD の dedicated の import は export した側と同じ作りの image を求めるが、compositor の image（SAMPLED だけ）は
   client の物と同じではない。この照合は zedBSD の OPAQUE（image の capability）の独自の意味として書き、Linux の移植可能な道は DMA_BUF（F-065）とする。

compositor の変更: `import_image` が wire の記述から作った image を dedicated で import する。**Vulkan に渡す前に**、OS の backend（2.5）が wire の値を確かめる
（レビュー M4。今は kernel の `memcmp` が `vkCreateImage` の前にあるため、この確かめが無いと不正な値が host の renderer に届く）: 幅・高さが 0 でなく
`maxImageDimension2D` 以下、形式が 2 つのどちらか、stride が幅×4 以上で 4 の倍数、offset と大きさが溢れない、memory type が `memoryTypeCount` 未満
（`import.c:193` の `1U << image->memory_type` の未定義の shift も防ぐ）。`zwl_gpu_import`・`GPU_RESOURCE_DESTROY`・
`buffer->image`（`struct gpu_resource_import`）を消し、wire の記述を compositor の型（`struct zwl_buffer_layout`: 幅・高さ・VkFormat・stride・offset・
memory type・allocation の大きさ）に写して持つ。写すのは OS の backend（2.5）。
log の行（レビュー M6）: `ZWL IMPORT client= buffer=`（`display.c:139`）は `zwl_gpu_import` と一緒に消えるが、`plan/ws099` の import-launch（V4 の道具、
`import-launch.sh:59,74`）と `wayland-qemu.py:431,539` が数える。同じ形（`client=`・`buffer=`・`width=`・`height=`・`bytes=`）の行を import の成功で出す。
`resource=`・`handle=`（kernel の値）は出せなくなるので、compositor の code（`objects.c:222` の `ZWL RELEASE … resource=` の行、`objects.c:187` の大きさ）と、
それを読む試験を p004 で直す。

V3 の安全性: 他の client の画像は、fd が capability なので名指しできない（今と同じ）。記述を偽った buffer は libvulkan の照合で拒まれる。照合する項目は
今の `memcmp` の意味のある項目（幅・高さ・形式・stride・offset・大きさ・memory type・tiling・usage）を全て含む。`device_id` は libvulkan の import が device を確かめる。

### 2.4 fence（FENCE_QUERY）→ 送る fence を present ごとに新しくし、compositor は poll だけ（改訂 2）

**問題**: OPAQUE の fence の fd は再利用される kernel の fence への参照で、「この commit の世代が済んだか」は fd だけでは分からない。client の WSI は
queue の slot の fence を再利用し（`wsi-swapchain.c:1950-1972` の `vkResetFences`）、present のたびに今の世代を wire に載せる。compositor はその世代を
`GPU_FENCE_QUERY` で照らしている。

**設計**: client の WSI が、compositor へ送る fence を**present ごとに新しく作る**。対象は **Wayland の target を含む job だけ**で、VK_KHR_display だけの job
（compositor 自身の画面の出力。世代は libvulkan の中だけで使う）は今どおり slot の fence を再利用する（再レビュー A1。`shared_fence` は display の target でも真、
`wsi-swapchain.c:1902-1909`）。新しい fence の作成の直後の `vulkan_external_fence_prepare_locked` の host の `vkResetFences` は省く（再レビュー m2）。
所有: present ごとの VkFence の参照と `vkGetFenceFdKHR` の fd の 2 つを、`present_finish` で全ての道（fence の作成の後・submit の前の失敗、device の喪失の待ちの失敗、
`present_drain` による teardown）で閉じる。slot の cache の `completion->fd`・`completion->shared`（`wsi-swapchain.c:1916-1945`）の所有は Wayland の job では使わない形に
定め直す（再レビュー m1）。compositor が持つ fd（libwayland が送る時に dup する、`libwayland/wire.c:523`）と job の bind が kernel の fence を生かす
（`vkDestroyFence` は自分の job を待って参照を閉じるだけで、kernel の fence を reset・signal しない。再レビューで確認）。送った kernel の fence は以後 reset されないので、
fd は「この commit の完了」だけを表し、kernel の poll は完了で readable、失敗で POLLERR のまま（`gpu-fence.c:543-571`）。

- producer の session が閉じると、bind された fence は ENODEV で signal され（`gpu.c:5515-5535`）、poll は POLLIN|POLLERR になる（再レビューで確認）。
  bind されない fence は永久に pending で、その client の surface だけが止まる。WSI は submit の成功の後にだけ fd を送る（`wsi-swapchain.c:2130`・`2669`）。
- compositor: `factory_fence` は fd を受けて持つだけ。`zwl_fence_ready` は各 fd を `poll(POLLIN, timeout 0)` で見る。readable か POLLERR（か POLLHUP・POLLNVAL）なら
  済み（今の「読めない fence は待たない」と同じ）、どちらでもなければ待つ。main loop は今どおり fd の POLLIN で起きる（`main.c:711-782`）。
  **Vulkan の import も ioctl も使わない。** client の fence の失敗が compositor の VkDevice の喪失にならない（レビュー B1）。compositor は fence の pool も
  `vkResetFences` も持たない（レビュー M5）。
- protocol は変えない: revision 2 の `set_acquire_fence(surface, fd, generation)` のまま。compositor は世代を読まない（0 でない事だけ確かめる）。
  `userland/base/tests/acquire-fence`（新しい kernel の fence を作り、その 1 つ目の世代を送る。`main.c:433-475`）はそのまま通る見込み（レビュー M3）。
- libvulkan の内部: WSI の worker の完了の待ち（`wsi-swapchain.c:2673`）と、libvulkan 自身の画面の present の世代（`GPU_DISPLAY_PRESENT_SYNC`、
  `wsi-swapchain.c:2237-2245`、`wsi-display.c:900`）は、その job の fence と世代を今どおり使う（どちらも新しい fence の 1 つ目の世代になる）。fence の reset や
  payload の付け替えは無いので、レビュー B2・M1・M2 の問題は起きない。
- 移行の途中: p005（WSI）の前の compositor は今どおり世代を照らすので正しい。p006（compositor）の前の WSI（slot の再利用）を相手に poll だけにすると、
  次の frame の reset の後は 1 frame 遅く見えることがある（正しさは保つ）。zedBSD の base では client と compositor は同じ image で入れ替わるので、p005 の後に p006。
- 自分の fence を偽る client: 読めない fd や永久に pending の fence を送った client は、自分の surface が進まないだけで、他の client と compositor には影響しない
  （fd の数は `ZWL_FENCE_MAX` で抑える）。今の「本物の fence か」の確かめ（`protocol.c:1645`）が無くなる代わりの守りはこれで足りる。
- Linux・FreeBSD（F-065）: sync_file の fd も poll で同じ意味を持つので、compositor の fence の code は OS 共通になる。

- main loop の poll の集合から fatal の client の surface の fence を外す（再レビュー m5。今は `dead` だけを見る `main.c:772-781` のため、常に readable な fd
  （poll だけにすると普通の file も受け入れる）が、client の回収まで loop を起こし続けうる）。

費用: Wayland の target の present ごとに `vkCreateFence`・`vkGetFenceFdKHR`・`vkDestroyFence`（host の reset は省く。今の `vkResetFences` の代わりで、Venus の wire の
往復は差し引き 1 回ほど増える見込み）。compositor 自身の画面の出力は変わらない。
compositor は commit ごとの `GPU_FENCE_QUERY` 2 回（受け取りと pass ごと）が `poll` に替わる。V4 で client の present の時間を測る。

採らなかった案: (a) 標準の SYNC_FD の export（payload の付け替え）と compositor の Vulkan の import。export の reset が WSI の worker と compositor の
`vkWaitForFences`（`compose.c:402`）を永久に待たせ（B2）、pending の native の fence の再利用の順序（M1）、世代を内部で使う画面の present（M2）、取り込んだ fence の
失敗の sticky な device の喪失（B1）、fence の pool の費用（M5）の問題がある。Linux の移植で要るときは F-065 で改めて設計する。(b) 世代を wire に残し
`GPU_FENCE_QUERY` を macro で残す。移せる道があるので採らない。(c) kernel に snapshot の fd の UAPI を足す。不要。

### 2.5 OS の backend の境界

compositor の OS 固有の部分を 1 つの source の module に集める（macro の分岐を共通の code に散らさない。D1、F-065 の決定 5 の「module の入れ替え」）。

- `userland/desktop/wayland/gpu-zedbsd.c`（新規）: `keiland_gpu_buffer_v1` の payload の解読（64 byte の `gpu_image_descriptor` → `struct zwl_buffer_layout`、
  `GPU_PIXEL_*` → `VkFormat`）、import に使う handle の種類（`VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT`）と pNext の追加の組み立て（zedBSD では無し。
  Linux では `VkImageDrmFormatModifierExplicitCreateInfoEXT` などが入る所）。fence は OS 共通（poll だけ、2.4）。`uapi/gpu*.h` を include するのはこの file だけ。
- `zwl-gpu.h`（新規、OS 共通）: 上の境界の関数の宣言と `struct zwl_buffer_layout`。
- Makefile が OS で module を選ぶ（今は zedBSD だけなので `gpu-zedbsd.c` を常に入れる）。Linux・FreeBSD の module（dma-buf、sync_file）は F-065 で作る。
- compositor 自身の frame の fence（`compose.c` の OPAQUE_FD の export と poll）は ioctl ではなく、WS103 では変えない。OPAQUE の fd を poll するのは zedBSD の
  意味なので、Linux・FreeBSD の module では SYNC_FD（sync_file）にする（F-065 の範囲）。

### 2.6 macro を外した build の確かめ方（V1）

Linux の build はまだ無いので、次の script（`plan/tools/gpu-boundary/v1-check.sh`）で確かめる。

1. grep: `userland/desktop/wayland/` の中で `uapi/gpu` の include と、`ioctl(` の第 2 引数が `GPU_` で始まる呼び出しが無い（文字列の中の `"ZWL GPU_ERROR"` などの
   log は数えない。`<sys/ioctl.h>` は evdev の `input.c`・`tablet.c`・`touch.c` に残る）。`gpu-zedbsd.c` は UAPI の型を読むが ioctl は呼ばない。
2. 毒の header: `uapi/gpu.h`・`gpu-display.h`・`gpu-fence.h` を `#error` だけの file にした include の directory を先頭に置き、`gpu-zedbsd.c` 以外の全ての
   `.c` を host の clang で `-fsyntax-only` にかける（sysroot の include は zedBSD の build と同じ）。1 つでも GPU の UAPI を読めば失敗する。
3. `/dev/gpu0` の open と `--gpu` の option が無い（compositor は GPU の fd を持たない。repository に `--gpu` を渡す script・sessiond は無い）。

V1・V4 の言い回しとの差: ws.md の V1 は「macro を外した build」、V4 は「macro の有り・無しの差」と書いている。この設計は macro でなく module の入れ替えで、
V4 は p002 の前と p006 の後の比較になる。基準の言い回しの改訂はユーザーの確認を得てから ws.md に書く（レビュー m7）。

### 2.7 V4（性能）の見込みと測り方

- buffer ごと: compositor の `GPU_RESOURCE_IMPORT`・`DESTROY` の 2 回が消え、libvulkan の照合に `vkGetImageSubresourceLayout` の往復が 1 回増える
  （Venus では wire の往復。compositor も同じ問い合わせを既にしているので、libvulkan が image ごとに結果を覚えて 1 回にできる）。app の起動の時の数回だけで、frame には無い。
- commit ごと: compositor の ioctl 2 回が poll に替わる。client の present ごとに fence の作成・破棄が増える（2.4）。
- 測り方: WS099 の import-launch（app の最初の frame）と WS075 の measure-apps（C6）を、p002 の前（基準）と p006 の後で QEMU の Venus と 5330 の passthrough で比べる。
  Venus では client の present の間隔（fence の作成の増分）と、compositor の frame の間隔（変わらない事の確かめ）も測る。import-launch が数える `ZWL IMPORT` の行は p004 で同じ形を保つ（2.3）。
  差が誤差を超えたら p007 で原因を分ける。

## 3. Phase の分け方

| Phase | 範囲 | 依存 | 確かめ |
| --- | --- | --- | --- |
| p002 | compositor: 2.1（起動の問い合わせを VK_KHR_display へ）と 2.2（`--direct` の削除）。`GET_INFO`・`DISPLAY_QUERY`・`DISPLAY_MODE`・`CLAIM`・`PRESENT`・`RELEASE` が消える。GPU の fd は import のためにまだ残る。`ZWL GPU` の行と、直の道だけが出す `ZWL PRESENT` を読む試験（`wayland-qemu.py:540`）を直すか退役させる（再レビュー m4） | — | build（warning 0）、greeter → login → Log Out → greeter（C1 の QEMU の部分）、`--width` なしの大きさ、boot test、**5330 の passthrough で起動と login の smoke**（大きさの出所と受け渡しに触れるため。レビュー m10） |
| p003 | libvulkan: 2.3 の 1〜5（dedicated allocation、get_memory_requirements2、import の照合、bind の守り）。拡張の追加に伴う `dispatch-table.inc`・`exports.map`・`api-commands.tsv`・`instance.c` の `available[]` | — | host の試験（偽の記述を拒む: 幅・高さ・形式・stride・offset・大きさのそれぞれ、正しい物は通る、別の image の bind を拒む。V3）。image の capability の作り手は `wsi-image.c` の道（`GPU_RESOURCE_EXPORT`）で作る（gpu-share は使えない） |
| p004 | compositor: 2.3 の compositor の側（wire の値の確かめ、dedicated で import、`zwl_gpu_import`・`RESOURCE_DESTROY`・`buffer->image` を消す、`ZWL IMPORT` の行を保つ）と 2.5 の `gpu-zedbsd.c`・`zwl-gpu.h` の buffer の部分 | p002、p003 | app の起動と窓の操作（C9 の一部）、偽の記述の client、import-launch、`wayland-qemu.py` の行の読み |
| p005 | libvulkan: 2.4 の WSI の側（Wayland の target の present ごとに新しい fence、所有と後始末、新しい fence の reset を省く） | — | host の試験（fd が present ごとに別の kernel の fence、前の fd の poll が次の present の後も readable のまま、失敗の道で fd と fence が漏れない）、Venus で窓の app（compositor は今の世代の照合のまま）と compositor の frame の間隔 |
| p006 | compositor: 2.4 の compositor の側（poll だけ、`FENCE_QUERY` を消す、fatal の client の fence を poll の集合から外す）、`/dev/gpu0` と `--gpu` を消す、UAPI の型を `gpu-zedbsd.c` に閉じる、2.6 の `v1-check.sh` | p004、p005 | `v1-check.sh`（V1）、Venus で窓の app と Notes（WS079-p010）、acquire-fence の試験、boot test |
| p007 | 規約の全文（coding-style.md）で WS の全 source の変更を見直し、回帰（C1・C9・WS079-p010・boot test）、5330 の passthrough（V2）、V4 の計測 | p002〜p006 | 規約・build・試験・計測の記録 |

- p003 と p005 は他と独立（libvulkan）。p004 は p002（同じ `display.c`・`protocol.c`・`zwl.h`・`main.c` に触れる。レビュー m5）と p003 の後、p006 は p004 と p005 の後。
- デモ（10/17、新規の実装は 10/10 ごろまで）との関係: fence の道（p005・p006）は 5330 では通らないが、Windows の QEMU（WINQ-EMU、Venus）の touch のデモで通る。
  p005・p006 は Venus の試験を必ず通す。p002 は greeter と session の受け渡しに触れるので、C1 の確かめと 5330 の smoke を必ず行う。
- HAL（`hal.h`）・toolchain・kernel・kernel の UAPI の変更は無い。libvulkan の拡張の追加（p003）と WSI の変更（p005）は WS103 の範囲の中（D1・D3）。

## 4. 危険と未確認

- 大きさを決める時点が `zwl_compose_open` の中へ移ることで、壁紙・glass の prefetch と pointer の初期位置の大きさが変わらないか（p002）。
- present ごとの fence の作成の費用（Venus の wire の往復）が frame の間隔に見えるか（p005 で測る。見えたら、compositor が fd を閉じた事を知る手段が無いので、
  fence の ring を使い回す案は採れない。作成を worker の外へ先に出すなどを考える）。
- 規格の読みは記憶による（VUID の番号を含む）。p003 で規格の本文を確かめる。
- dedicated の照合の意味は zedBSD の独自（2.3 の 5）。Linux の DMA_BUF の import の設計は F-065 で改めて行う。

## 5. レビューの記録（2026-09-30 design-reviewer、改訂 2 で処理）

| 指摘 | 重さ | 処理 |
| --- | --- | --- |
| B1 取り込んだ fence の失敗で compositor の device が失われる | blocker | 2.4 を poll だけに替えた |
| B2 SYNC_FD の export の reset で WSI の worker と compositor が永久に待つ | blocker | SYNC_FD をやめ、present ごとに新しい fence（reset しない） |
| M1 pending の export の順序・native の fence の再利用 | major | 付け替えが無くなり該当しない |
| M2 画面の present が世代を使う | major | libvulkan の内部で今どおり使う（2.4） |
| M3 p005・p006 の間の壊れた状態、acquire-fence の試験 | major | protocol を変えない。p005 の後も compositor は今のまま正しい |
| M4 wire の値が確かめ無しに Vulkan へ | major | backend で確かめてから `vkCreateImage`（2.3） |
| M5 fence の pool の費用 | major | pool が無くなった |
| M6 `ZWL IMPORT` などの log の行を試験が数える | major | 同じ形の行を保ち、読む試験を p004 で直す（2.3） |
| M7 bind で照合が守られない | major | bind の守りと大きさの一致（2.3 の 2・4） |
| m1 gpu-share は image の capability を import しない | minor | 2.3 の 3 と p003 の試験を直した |
| m2 dedicated の規格上の根拠が弱い | minor | 独自の意味として注記（2.3 の 5） |
| m3 `choose_mode` の読み違い | minor | 2.1 を直した |
| m4 0 の大きさの矛盾 | minor | 既定の大きさのままに統一（2.1） |
| m5 p002 と p004 の衝突 | minor | 依存に p002 → p004 |
| m6 拡張の追加に要る file | minor | p003 の範囲に書いた（protocol は変えないので libwayland は触れない） |
| m7 V1 の grep、V1・V4 の言い回し | minor | 2.6 を直し、基準の改訂はユーザーの確認へ |
| m8・m9 SYNC_FD の import・複写の意味 | minor | SYNC_FD を使わないので該当しない |
| m10 5330 の確かめが p007 だけ | minor | p002 に 5330 の smoke を足した |

再レビュー（同日、改訂 2 に対して。blocker なし）の処理（改訂 3）:

| 指摘 | 重さ | 処理 |
| --- | --- | --- |
| A1 新しい fence が compositor 自身の画面の出力にも掛かる | major | Wayland の target を含む job だけに限った（2.4）、compositor の frame の間隔も測る（2.7） |
| A2 照合が `tiling`・`usage` を見ず `memcmp` と同じ強さでない | major | kernel の記述の `tiling`・`usage` も照らす（2.3） |
| m1 present ごとの 2 つの参照の後始末 | minor | 2.4 に所有と全ての失敗の道を書いた |
| m2 新しい fence の host の reset | minor | 省く（2.4） |
| m3 大きさを取る display | minor | 実際に選ぶ display から（2.1） |
| m4 compositor の code を試験と呼んだ、`ZWL PRESENT` を読む試験 | minor | 言い回しを直し、p002 で直すか退役（2.3、§3） |
| m5 fatal の client の fence で loop が回り続ける | minor | poll の集合から外す（2.4、p006） |

