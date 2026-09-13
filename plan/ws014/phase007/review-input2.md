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

---

# 追補: 設計に関する議論 (レビュー後)

前提: P9 (zwl) と P10 (libwayland) はテストドライバのため対象外。P1〜P8 は修正する方針。
設計方針の確認: dma-buf と DRM/KMS/GBM は導入しない。POSIX に無い概念を増やさず、
共有は handle fd、表示制御は Vulkan のオプション API (`VK_KHR_display`) に集約する。

## A. 修正後の設計評価

方向性は正しい。dma-buf と KMS を持たないことによる機能上の欠落は、以下の 3 点を
加えれば無くなり、「アプリから見えるのは POSIX fd と Vulkan 拡張だけ」が達成できる。

1. fence を handle fd にする (B 節)
2. 共有メモリと fence を Khronos 標準拡張の形で公開する (C 節)
3. 表示専用デバイスへの import を許す (D 節)

### handle fd は dma-buf の代替として成立するか

成立する。dma-buf が handle 以外に提供しているものは 3 つで、いずれも handle fd の枠内で吸収できる。

- **他ドライバからの物理バッキング参照** (attach / map_dma_buf)。カメラや動画デコーダが
  同じページを DMA する場合に必要。SoC の共有メモリなら「同じページを別 IOMMU から見る」
  だけなので、`kernel_handle_ops` にカーネル内部向けの「バッキング取得」を 1 つ足せば済み、
  ユーザランド ABI は変わらない。今は不要だが、型付きオブジェクトのまま拡張できる形を保つ。
- **同期** (dma-fence / sync_file)。現状唯一の本質的な欠落。B 節で解決する。
- **CPU アクセスとキャッシュ管理** (begin/end_cpu_access)。mmap トークンと NOCACHE 属性で
  既に代替されている。将来 write-combining 相当の属性を選べると良い。

## B. fence を handle fd にする

P1 でカーネル fence を入れるとき、fence を `KERNEL_HANDLE_FENCE` 型の handle fd にする。
poll で POLLIN が signaled を意味する fd は POSIX の範囲であり、Linux の sync_file と同じ。

得られるもの:

- `vkQueuePresentKHR` が CPU で待たなくなる。`GPU_DISPLAY_PRESENT` と `GPU_COMMAND` に
  「待つ fence」「signal する fence」を渡し、カーネル側で順序付けする。
- `zed_gpu_buffer_v1` に fence fd を SCM_RIGHTS で添付でき、コンポジタは fence を
  present の依存として渡すだけで済む。クライアントとコンポジタ間の CPU 同期待ちが消える。
- `VK_KHR_external_fence_fd` / `VK_KHR_external_semaphore_fd` をそのまま実装できる。

## C. 共有メモリを Khronos 標準の形に寄せる

現在は `VkImportMemoryResourceInfoMESA` 系の Venus 由来構造で import しているが、
handle fd を `VK_KHR_external_memory_fd` の OPAQUE_FD として扱えば、コンポジタも
クライアントも Vulkan 拡張だけで完結する。zed 固有で残るのは Wayland プロトコルの
`zed_gpu_buffer_v1` だけになる。これは linux-dmabuf-v1 に相当する役割で避けられないため、
将来ツールキットを移植しやすいよう create_params に近い形 (format、stride、offset、
modifier 相当) にしておく。

GBM は不要。コンポジタが Vulkan でイメージを確保し external memory で export すれば済む。
GL が必要になっても Zink は Vulkan WSI の上に載るので GBM を要求しない。

## D. KMS を Vulkan に置き換えることの評価

`VK_KHR_display` を唯一の表示 API にするのは、SDL や GLFW が既に対応しているので
実用上も通る。KMS が担っている役割のうち、Vulkan 拡張で埋める必要があるものは 3 つ。

- **ホットプラグと vblank 計数**: `VK_EXT_display_control` (display event 登録、
  swapchain counter) が対応する拡張。P1 の poll 通知を gpu fd に足すとき、hotplug も
  同じ経路で流せるようにしておく。現在の generation と ESTALE は OUT_OF_DATE への
  対応として正しい。
- **表示コントローラが GPU と別デバイスの場合**: SoC では珍しくない。E 節。
- **GOP や単純フレームバッファの統一的な扱い**: 「display ops だけ実装し、present は
  CPU コピー (非 BLOB 経路)」のフレームバッファドライバを書けば同じ UAPI で扱える。
  venus-frame の 2d 経路が既にその形。GPU の無い機械で Vulkan アプリを動かすには
  ソフトウェア Vulkan が別途必要だが、これは KMS の有無と無関係。

### 実ハードウェアへの拡張余地

現状の UAPI は `GPU_COMMAND` の中身が Venus ストリームである点で仮想化専用の形をしている。
実 GPU 向けに Mesa 系ドライバを移植する場合、ユーザランドの GPU 仮想アドレス管理
(VA map/unmap)、複数キュー、依存関係付き submit が要る。ただしこれは DRM でも
ドライバ固有 ioctl の領域なので、「コア (blob、handle、fence、display) + ドライバ私有の
コマンドペイロード」という今の分割を明示しておけば、必要になったときに足せる。
先回りして設計する必要はない。

## E. 描画デバイスと表示デバイスが別の場合

### 基本方針

描画と表示は別の `/dev/gpuN` でよい。SoC なら共有メモリなので、**表示デバイスが
描画デバイスのページをそのままスキャンアウトする** のが基本経路。CPU コピーは表示
デバイスが DMA でそのメモリに届かない場合 (USB ディスプレイ、BAR 上のフレームバッファしか
持たない Cirrus 型 VGA) の退避経路で、現在の非 BLOB present がそのまま使える。

「どちらをスキャンアウトデバイスに固定するか」はコンポジタが選ぶのではなく、
**libvulkan (ドライバ) が組にする**。Mesa の v3dv (v3d = 描画、vc4 = 表示) も同様に
`VK_KHR_display` 実装の内部で表示ノードを別に開き、描画バッファを dmabuf で渡している。
アプリからは 1 つの VkPhysicalDevice にしか見えない。

### カーネル側

- `/dev/gpuN` は 1 コントローラ 1 ノード。`gpu_info.capabilities` に描画可 / 表示可の
  ビットを持たせる。両方持つノード (virtio-gpu) も表示専用ノードもあり得る。
- 表示専用ノードには組になる描画デバイスのヒント (デバイスツリーやプラットフォーム
  コード由来の companion id) を `gpu_info` に載せる。無ければ libvulkan が「最初の
  描画デバイスに孤立した表示デバイスを全部付ける」既定則で組にする。
- デバイス跨ぎ import は「linear image のみ、スキャンアウト目的」に限定して許可する
  (P7 の修正範囲)。

### libvulkan 側

- VkPhysicalDevice = 描画ノード + 0 個以上の表示ノード。
  `vkGetPhysicalDeviceDisplayPropertiesKHR` は組になった表示ノードの出力を列挙する。
- `VkDisplaySurfaceKHR` は VkDisplayKHR に、swapchain は VkDevice に紐付くので、
  どのデバイスがスキャンアウトするかは Vulkan のオブジェクトグラフに自然に固定される。
  別の仕組みは要らない。
- swapchain image は描画ノードで linear に確保し、handle fd を表示ノードへ import して
  BLOB present する。表示ノードが DMA で届かなければ非 BLOB present にフォールバック。
  この判断は libvulkan 内で閉じる。

### コンポジタ側

- Vulkan ベースのコンポジタは、物理デバイスを列挙して表示を持つものを選び、
  `VK_KHR_display` で surface を作るだけ。デバイスが 2 つあることを知る必要はない。
- 別 GPU (マルチ GPU) でクライアントが描いたイメージは、コンポジタが自デバイスへ import
  する。Linux の PRIME と同じで、コンポジタが選ぶのは「自分がどの物理デバイスで動くか」だけ。
- 現在の zwl は UAPI を直接叩いているので、この構成では「import 先は表示ノード」という
  規則を自分で守る必要がある。長期的には zwl も Vulkan 経由 (external memory import +
  `VK_KHR_display`) にし、組合せの知識をカーネルと libvulkan に閉じ込める。

## F. DMA 共有か CPU コピーかの判別

コンポジタは判別しない。判別はカーネルの表示ドライバが import 時に行い、その結果を見て
libvulkan が経路を一度だけ決める。コンポジタから見えるのは「import が成功したか」だけ。

### 判別の主体はカーネルの表示ドライバ

`GPU_RESOURCE_IMPORT` を表示ノードに対して行うと、コアは handle のバッキング (ページ列、
物理連続か、キャッシュ属性、置かれている場所) を表示ドライバの import op に渡す。表示
ドライバは自分のスキャンアウトエンジンが読めるか (連続性、アドレス範囲、IOMMU の有無、
stride 整列) で判定し、読めれば 0、読めなければ ENOTSUP を返す。

### 事前に制約を問い合わせて、割り当て時に合わせる

import で失敗してから対処するより、割り当て時に合わせる方が確実。`GPU_DISPLAY_QUERY` に
スキャンアウト制約 (連続メモリ必須か、整列、対応フォーマット、外部バッキングを受け付けるか)
を追加し、描画ノード側の `GPU_BLOB_CREATE` にその制約を満たす配置フラグ (物理連続など) を
渡せるようにする。DRM の scanout placement と modifier ネゴシエーションに相当し、KMS を
持たなくてもこの情報だけは要る。

### libvulkan が swapchain 作成時に一度決める

`VK_KHR_display` の surface に swapchain を作るとき、libvulkan は描画ノードと表示ノードの
組を知っている。表示制約を問い合わせ、描画ノードでそれを満たす linear image を確保できれば
表示ノードへ import して BLOB present に固定する。確保できない、または import が ENOTSUP
なら、readback と表示ノード上の storage resource を確保して非 BLOB present に固定する。
フレームごとには判断しない。

### Wayland コンポジタの立場

- **自分の出力**は `VK_KHR_display` の swapchain なので、判断は libvulkan の中に閉じる。
  コンポジタは CPU コピーを一切書かない。
- **クライアントのバッファ**は常に自分の描画デバイスへ import して合成する。合成は GPU 上の
  コピーなので、クライアントが別 GPU で描いていても、メモリが連続でなくても成功する。
  表示デバイスの制約はここに関係しない。
- **直接スキャンアウト** (フルスクリーンのクライアントバッファを合成せずに出す最適化) を
  やるなら、クライアントのバッファを表示ノードへ import してみて、失敗したら合成に戻す。
  Linux のコンポジタが `drmModeAddFB2` の失敗で合成にフォールバックするのと同じ形。
- クライアントに最初から制約を満たす確保をさせたい場合は、`zed_gpu_buffer_v1` に
  「推奨デバイスと配置制約」を伝えるイベントを足す。linux-dmabuf-v1 の feedback
  (main_device と tranche) に相当し、クライアント側の libvulkan が Wayland surface 用
  swapchain を確保するときに使う。任意の最適化で、無くても動作は変わらない。

まとめ: 判別はカーネルの表示ドライバ、決定は libvulkan の swapchain 作成時、コンポジタは
自分の描画デバイスに import して合成するだけ。CPU コピーの経路は libvulkan の display
present の中にしか存在しない。

## G. 修正後の UAPI 追加項目 (まとめ)

| 追加 | 目的 | 関連 |
|---|---|---|
| fence handle fd (`KERNEL_HANDLE_FENCE`)、poll で POLLIN | 完了通知、プロセス間同期 | P1, P5, B |
| `GPU_COMMAND` / `GPU_DISPLAY_PRESENT` に wait/signal fence | GPU 側順序付け、present 非同期化 | P3, P5 |
| gpu fd の poll に hotplug / 完了通知 | `VK_EXT_display_control` | P1, D |
| `gpu_info.capabilities` に描画可 / 表示可、companion id | 表示専用デバイス | E |
| デバイス跨ぎ import (linear image、scanout 目的) | 表示専用デバイス、PRIME 相当 | P7, E |
| `GPU_DISPLAY_QUERY` にスキャンアウト制約 | 割り当て時の配置決定 | F |
| `GPU_BLOB_CREATE` に配置フラグ (物理連続など) | 同上 | F |
| export を VkBuffer / optimal image に拡張 | compute 出力やテクスチャ共有 | P7 |
