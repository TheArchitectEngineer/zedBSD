# zedBSD GPU スタック 機能レビュー

対象: `src/drivers/gpu` (core + venus), `src/kern/{handle,fd-object,vm-device}.c` と unix-socket/filedesc 差分, `include/uapi/gpu*.h`, `userland/base/{libvulkan,libwayland,zwl,wltest,vkdemo}`, `userland/gpu/venus`。
方法: ソース読解のみ (実行検証はしていない)。セキュリティ観点は対象外。

---

## 1. 全体評価

設計の骨格は健全で、各層の責務分離が明確。

- **UAPI が Vulkan 直結**: DRM を経由せず、「blob 作成 / コマンド送出 / 表示 lease」の最小集合で Venus を動かせている。`gpu_info`/`gpu_capset`/`gpu_blob_create` など全構造体に version/size があり ABI 進化に耐える。
- **kernel handle fd**: `struct kernel_handle` (refcount + 型 + release ops) を `fd_object` として fd テーブルに載せ、SCM_RIGHTS で運ぶ設計は dma-buf の要点を小さく取り出せている。dup/fork/close-on-exec、`MSG_PEEK`、受信中断時の権利回収まで面倒を見ており、consumer 側は `GPU_RESOURCE_IMPORT` で自セッションの独立 resource として受け取る (producer session に触れない) のも正しい。
- **「受理 ≠ 完了」の一貫性**: Venus reply trailer (`vkSeekReplyCommandStreamMESA` + `vkEnumerateInstanceVersion`) の書き換え検出、fence でのみ完了判定、zwl は import 後の present を fenced にするなど、全層で守られている。
- **タイムアウトが全て有限**: transport 10 s、libvulkan 10 s、zwl 150 s、各テストの capture wait など。ハングしない。
- **ユーザランド Vulkan**: 1.0 core + `VK_KHR_surface/display/wayland_surface/swapchain/display_swapchain` が独立実装で通り、vkdemo (テクスチャ付き cuboid, depth) と wltest が実際にレンダリングできている。

以下は「動く」を前提にした機能・性能・拡張性の問題。影響の大きい順。

---

## 2. 問題点 (影響順)

### P1. 完了通知が UAPI に無く、全層がポーリング (設計上の欠落)

- `gpu_poll()` (gpu.c:888) はデバイス removal しか報告しない。`GPU_DISPLAY_WAIT` はバックエンド任せで、Venus 実装は未来 sequence に EINVAL、初回 present 前は EAGAIN を返し「待つ」機能を持たない。
- 結果、libvulkan の 1 トランザクション (context.c `vulkan_context_transaction`) は
  `ioctl(RESOURCE_WRITE: trailer clear)` → `ioctl(GPU_COMMAND)` → `ioctl(RESOURCE_READ: trailer)` を 1 ms `nanosleep` で繰り返し → `ioctl(RESOURCE_READ: 本文)`。
  最短でも 4 ioctl + 1 ms。
- `vkWaitForFences` (sync.c:409) は `vkGetFenceStatus` を `VULKAN_SYNC_POLL_NS` = 1 ms ごとに呼び、その各回が上記トランザクション。
- wsi-wayland は frame callback、zwl は 10 ms `poll()` でそれぞれ独立にポーリング。

**推奨**: virtio-gpu の fence 完了 (EVENT/ctrl queue の used ring) を割り込みで受け、
(a) `poll()` に POLLIN を追加、(b) `GPU_FENCE_WAIT` ないし「完了 sequence 読み出し」ioctl を追加。Venus の reply も同じ fence で通知できるので、libvulkan 側の 1 ms スピンが消える。P3/P5/P8 の根本原因でもある。

### P2. libvulkan: すべての `vkCmd*` が同期往復

- `commands-generated.inc` の各 `vkCmd*` は `command_record_begin` → `command_record_finish` (commands.c:523) で `vulkan_command_execute` を呼ぶ。つまり **記録コマンド 1 つ = カーネル往復 1 回 (≥4 ioctl + ≥1 ms)**。
- vkdemo の 1 フレームは barrier/beginRenderPass/bind*/draw/endRenderPass/copy で十数コマンド → 記録だけで CPU 側 10〜20 ms 級。Venus 本来の利点 (1 ストリームにまとめて 1 submit、reply が要るコマンドだけ reply stream を使う) を使っていない。
- README の "vkExecuteCommandStreamsMESA for streams > 64 KiB" は単一巨大コマンド向けで、バッチには使っていない。

**推奨**: `VkCommandBuffer_T` にエンコード済みバイト列を蓄積し、`vkEndCommandBuffer` (またはサイズ上限到達時) に 1 回の `GPU_COMMAND` で送る。戻り値の無い `vkCmd*` は reply 不要なので trailer も 1 回で済む。加えて reply blob は `GPU_BLOB_MAPPABLE` で作っているのに ioctl 読み書きしている (client.c/venus 診断も同様)。mmap して直接読めば trailer poll の ioctl も無くせる。

### P3. Venus 表示: vblank 待ちを controller mutex 保持のまま行う

- `display_present` (venus/display.c:749) は `mutex_lock(&controller->mutex)` のまま `display_frame`/`display_blob_frame` → `display_next_refresh` (display.c:1405) → `sched_sleep(target)` する。
- transport も同じ mutex 配下なので、present 中 (最大 1 リフレッシュ期間 = 60 Hz で 16.7 ms) は **他セッションの `GPU_COMMAND` が全て停止**。zwl が present している間、wltest/vkdemo の `vkCmd*` (P2 により 1 個ずつ往復) が毎回 1 フレーム待つことになる。

**推奨**: sleep を mutex 外へ。present は「次の境界 tick を計算して SET_SCANOUT を予約」し、待ちはユーザ側の `GPU_DISPLAY_WAIT` (P1 で実装) に移す。最低限でも `mutex_unlock → sched_sleep → mutex_lock` して generation を再検証する。

### P4. 表示 WSI (`VK_KHR_display`) が BLOB scanout を使わず CPU 経由 3 コピー

- `vkQueuePresentKHR` の display 経路: swapchain image → readback buffer (GPU copy) → fence 同期待ち → `readback_pixels` (uncached mmap) から `GPU_RESOURCE_WRITE` 64 KiB × N で storage resource へ (wsi-display.c:639) → `GPU_DISPLAY_PRESENT` (FIFO, 非 BLOB) → カーネルが `memcpy` (display.c:1174) → TRANSFER_TO_HOST → SET_SCANOUT。
- 640×480 で ioctl 約 19 回、1.2 MB × 3 回のコピー、うち 1 回は uncached 領域からの CPU 読み出し。
- 一方 zwl は `GPU_DISPLAY_PRESENT_FIFO | GPU_DISPLAY_PRESENT_BLOB` (zwl/display.c:232) で GPU 常駐 linear image を直接 scanout しており、カーネル側に経路は既にある。

**推奨**: display WSI も wsi-wayland と同じ「swapchain image → linear shared image に GPU copy → BLOB present」に統一する。readback/CPU コピーは `--output` のような明示的用途に限定。

### P5. `vkQueuePresentKHR` が同期完了待ちで、パイプライン化できない

- `present_submit` → `vkWaitForFences(job->fence)` (wsi-swapchain.c:1751) を `present_native` の前に行う。アプリの `vkQueueSubmit` 直後の present で CPU が GPU 完了までブロックし、swapchain 2〜3 枚の意味が無い。
- wltest/vkdemo はさらにその後 `vkWaitForFences` するので実質シングルバッファ、フレームレートは「GPU 時間 + P2 の記録時間 + P4 のコピー時間 + vblank」の直列和。
- Wayland 経路も同じ: fence 完了後に `wl_surface_commit` (wsi-wayland.c:700)。本来は semaphore 待ちを GPU/compositor 側に任せ、commit は即時で良い (zed_gpu_buffer_v1 に fence/sync fd を付けるか、zwl 側 present が fenced なので「GPU 順序が守られる」前提で attach して問題ない)。

**推奨**: P1 の通知があれば、present は「submit 後に fence を登録して即 return、完了時にワーカーまたは次の `vkAcquireNextImageKHR` で native present」にできる。

### P6. UAPI のセッション排他が EBUSY 即時返却

- `gpu_session_enter` (gpu.c:743) は同一 fd に対する 2 つ目の ioctl を待たずに `EBUSY`。libvulkan は `context->mutex` で直列化しているので今は表面化しないが、Vulkan の queue/fence はスレッドセーフが仕様なので、別スレッドで `vkWaitForFences` と `vkQueueSubmit` を同時に呼ぶアプリを将来サポートするならカーネル側で sleep lock 待ちにすべき。
- ENODEV (removal) と EBUSY の 2 種類しか無く、呼び手はリトライ可否を判断しづらい。

### P7. 共有 (export/import) の制限

- `GPU_RESOURCE_EXPORT` は linear `gpu_image_descriptor` 必須 (`gpu_image_validate`, gpu.c:2217)。**VkBuffer や optimal tiling の image は共有できない**。compute 出力や texture の共有、ゼロコピーの動画フレーム受け渡しなどが不可能。
- import は同一 device のみ (`EXDEV`, gpu.c:2548)。`GPU_BLOB_CROSS_DEVICE` フラグの意味が現状ほぼ無い。
- 転送 ioctl (`GPU_COPY_MAX` 64 KiB) はバルク用途には小さく、mmap を促す設計だが、mappable blob の mmap は vm-device.c:151 で `HAL_SPACE_DEVICE | HAL_SPACE_NOCACHE` (PCI shared-memory BAR 上) なので CPU 読み出し (readback) が遅い。書き込み専用なら write-combining 相当のヒントがあると良い。
- 良い点: import 先は session-local の独立 resource、ライフタイムは `kernel_handle` の refcount で producer が close しても生き続ける。

### P8. Venus transport: 単一 in-flight とフェイル固定

- control queue 1 本、同時 1 コマンド、busy-poll 最大 10 s (`VENUS_WAIT_MILLISECONDS`, transport.c:24) を controller mutex 保持で行う → ホストが遅いと **カーネル全体の GPU 利用が最大 10 s 停止**。
- タイムアウト後は `transport->failed = 1` で恒久隔離、リセット手段が無い (再起動のみ)。
- 割り込み駆動 (P1) にすれば in-flight 複数化と、他セッションを止めない待ちが自然に得られる。

### P9. zwl の構造上の制約 (「スタブ」宣言どおりだが列挙)

- present がイベントループ内で同期 (P3 の sleep を含む) で、1 パスにつき 1 surface しか present しない → 2 クライアント同時なら交互に 1 vblank ずつ、各 30 fps。frame callback も present 後に done。
- client image の extent == server extent 強制 (zwl/display.c:115、既定 320×240)、attach offset 0 のみ、transform 0 / scale 1 固定、region は無視。合成 (複数 surface の重ね合わせ) は無い。
- 入力 (wl_seat)、wl_shm、subcompositor、popup/positioner 無し。既存ツールキットは接続できない。
- `--timeout` は正の有限値が必須 (main.c:185) で、150 s 既定 → デーモンとして常駐できない。常駐用途なら `--timeout=0` を許可すべき。
- `wl_buffer.release` は holds が 0 になったときだけ。front を保持し続けるため、FIFO で release が 1 フレーム遅れる。wltest は `minImageCount+1` で吸収している。

### P10. libwayland の互換性上限

- 型付きリスナの dispatch (event.c) はライブラリが知っている固定インターフェース群 (wl_display/registry/callback/compositor/surface/region/buffer/output, xdg_wm_base/positioner/surface/toplevel/popup, zed_gpu_buffer_v1) の signature に対する分岐。**未知のプロトコル (wp_presentation, xdg_decoration, wp_linux_dmabuf など) は `wl_proxy_add_dispatcher` 経由でしか使えない**。libffi 相当の汎用呼び出しが無いのが将来の互換性ボトルネック。
- wl_shm / wl_seat / wl_data_device 系の interface 定義が無いので、既存 Wayland アプリのビルドがまず通らない (ヘッダ `wayland-client-protocol.h` の網羅範囲次第)。
- `wlc_proxy_lookup` がリスト線形走査 → オブジェクト数が増えると O(n²)。今は無害。
- 良い点: prepare_read/read_events/cancel_read のリーダ調停、SCM_RIGHTS のフレーム境界跨ぎ処理、private queue、wrapper は本家と同じ意味論で実装されており、libvulkan の private queue と wltest の default queue が正しく分離できている。

### P11. デモ/診断プログラム

- `userland/gpu/venus` (venus-frame) は libvulkan と別の Venus コーデック (client.c) を持つ**二重実装**。libvulkan が動く今、診断専用として残すなら明記が要る (README は既にそう書いている)。
- vkdemo は通常アニメーション時も毎フレーム readback + SHA-256 をしている (`vkdemo_render` → `read_pixels` → `hash_pixels`)。性能評価に使うなら `--no-readback` 相当が欲しい。
- wltest は acquire → submit → present → `vkWaitForFences` で毎フレーム同期。テストとして妥当。
- `userland/base/tests/gpu-share` は Makefile だけで、ソースは `plan/ws014/tests/gpu-share-client.c` を参照している (バンドルに含まれていないため未読)。

---

## 3. 細かい所見

- `GPU_GET_CAPSET` の上限 256 B は Venus capset (156 B + 拡張) には足りているが、virgl capset v2 等を将来返すには不足。
- `GPU_COMMAND_MAX` 64 KiB: バッチ化 (P2) をすると 1 コマンドバッファが超えやすい。`vkExecuteCommandStreamsMESA` で分割するか、上限を blob サイズに合わせる。
- venus display のコンソールワーカーはテキスト再描画のため `VENUS_CONSOLE_POLL_TICKS` ごとに起きる (display.c:462)。lease 保持中は停止しているか要確認 (読解では lease 中は skip している様子だが、ワーカー自体は起き続ける)。
- libvulkan の `vkEnumerateInstanceVersion` は 1.0 を返す (instance.c:930) が、ホストへは 1.1 で create している。1.1 機能 (`vkGetDeviceQueue2` は内部使用) をアプリに見せていないのは一貫している。
- Surface format は R8G8B8A8/B8G8R8A8 UNORM + SRGB_NONLINEAR のみ。sRGB フォーマットが無いので、ガンマ正しい描画をしたいアプリは自前で変換が必要。
- present mode: display は FIFO のみ、Wayland は FIFO + MAILBOX。IMMEDIATE が無いのはこの構造では妥当。

---

## 4. 優先順位の提案

| 順 | 施策 | 解決する問題 |
|---|---|---|
| 1 | virtio-gpu 割り込み + fence 完了通知 (poll / wait ioctl) | P1, P3, P5, P8 の基盤 |
| 2 | `vkCmd*` のコマンドバッファ内バッチ化、reply blob の mmap 直読 | P2 |
| 3 | 表示 WSI を BLOB scanout 経路へ統一 | P4 |
| 4 | present の非同期化 (fence 登録 → 完了時 native present) | P5 |
| 5 | Venus display の vblank sleep を mutex 外へ | P3 (1 を待たずに単独でも可能) |
| 6 | export を VkBuffer / optimal image に拡張 | P7 |
| 7 | zwl の常駐化と複数 surface 合成、libwayland の汎用 dispatcher | P9, P10 |

1 と 5 は独立して着手でき、5 は小さい変更で効果が大きい。
