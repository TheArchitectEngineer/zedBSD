<!-- awesome-plan project=zedbsd record=ws236-p012 -->

# ws236-p012: Pi の WSI の direct の swapchain（present の copy を無くす）

Parent: [WS236](../ws.md)
Status: in-progress（2026-10-16、P3 新世代。設計の第 2 版（design-reviewer の review-1 の major 8・minor 7 に対応）と実装・host 試験・build 済み、Q1 の merge と T2 の依頼 10 待ち）
Phase disposition: normal
実行者: P3（worktree `/home/awe/zedBSD-worktrees/p3`、branch `agent/p3-ws236g`）
依存: [ws236-p011](../phase011/phase.md)（A1 の全画面の copy、T1-688）。分けた経緯: 2026-10-16 Q1 の判断（review-1 m7「A3 は WS236 の独立の Phase に、規約の全文の見直しを含める」）。p011 の「A3 の設計」第 1 版（7879af2b0）からここへ移した。

## 目的と合格

- 目的: Pi 4（bcm2711 の V3D）で、Vulkan の app と compositor の swapchain の present ごとの GPU の copy（app が描く image → 共有の image）を無くし、1 表示 frame の GPU の pass と present の往復を減らす。製品の挙動（見え方・API の振る舞い）は変えない描画の最適化（Q1 の委任、2026-10-16）。
- 合格（Pi 4 実機、T2 の依頼 10）: 起動・desktop・app・GL の app・X11 の app・VT の切り替え・回転・動画の全画面と窓で画が出て崩れない、`execution failed`・`a pass writes a run the display reads`・decode の `came back unread`・`not answered` が無い、`v3d: perf` の image の pass が 1 表示 frame に 1 つ（A1 の copy だけ）に減る。5330（i915）・QEMU（Venus）・RTX（nvrtx）は `VK SWAPCHAIN direct=0 reason=renderer`（今の道のまま）。規約の全文の見直し（変えた C の file）。

## 今の形と問題

- libvulkan の WSI（`userland/desktop/libvulkan/wsi-swapchain.c`）は、app が描く swapchain の image（`group->images[i]`、OPTIMAL）と、compositor・display に渡す共有の image（`presentation[i].shared_image`、LINEAR、export した allocation を `platform->import_image` で native に登録）を分ける。`vkQueuePresentKHR` ごとに `present_record` が copy の command（`present_copy_shared`）を記録し（同じ image の 2 回目からは記録を使い回す）、`present_submit` が app の待ちの semaphore とともに submit し、present の worker が fence の後に native の present（Wayland の commit、display の `GPU_DISPLAY_PRESENT_SYNC`）をする。
- bcm2711 の kernel では OPTIMAL と LINEAR の image は同じ raster の backing（`vulkan-resource.c`「Optimal images use the same native raster backing」）で、layout と queue family の ownership は resource（VkImage）ごと（compositor・display が同じ memory を自分の image として import すると、その image の layout は別に持つ）。copy は同じ byte を写すだけの上乗せで、1 present に 240 tile の copy の pass（約 1.3〜2 ms）と、その pass の executor の順番待ちが足される（T1-688: 1 表示 frame に image の pass が 3 つ＝A1 の copy、videoplayer の present の copy、compositor の present の copy）。

## 約束（設計 第 2 版、2026-10-16）

### direct の chain とその条件

- **direct の chain**: app の image が共有の image そのもの（`vkGetSwapchainImagesKHR` が返すのは共有の image、LINEAR）。present は copy を記録・submit せず、app の待ちの semaphore と fence だけの、command buffer 0 個の submit（bare）をして、今と同じ native の present に進む。
- 条件（全てを満たす時だけ。判定は `swapchain_direct_eligible`、理由の語は log の `reason=`）:
  1. **renderer の約束**（review-1 M1、Q1 の判断）: kernel の capset の native の word（magic "TANZ"、offset 168・172）の **direct swapchain の bit（2）**。bcm2711 の kernel だけが全ての session に立てる（`render-runtime.c`、video の有無によらず native の word を出すようにした。H.264 の bit 1 は今まで通り）。libvulkan は `context->direct_swapchain`、かつ `context->copy_display == VK_FALSE`、かつ `GPU_CAP_SHARE`。vendor の ID では決めない（Venus の host が v3dv でも、Pi 5 でも、bit が無ければ今の道）。reason `renderer`。
  2. `vkCreateSwapchainKHR` で単独に作る chain（`swapchain_create` の明示の引数 `alone`。`vkCreateSharedSwapchainsKHR` の group は今のまま）。reason `shared`。
  3. cursor・overlay の plane の surface でない（cursor は前の copy を見せ直す、overlay は部分を見せる）。reason `plane`。
  4. `platform->import_image` がある（今の GPU の共有の道）。reason `route`。
  5. **VK_KHR_display の surface**（`display_mode` のある chain）は、作り手が `VULKAN_ZEDBSD_DIRECT_DISPLAY=1` で頼んだ時だけ（review-1 M2、Q1 の判断 (a)）。compositor は出力の swapchain の作成の前後だけこれを置いて外す（`wayland/compose.c`、head・client・子の process には継がれない）。第三者の display の app は今の copy の道のまま（`VkDisplayPresentInfoKHR` の矩形の合成も今まで通り）。reason `display`。
  6. `VK_SHARING_MODE_EXCLUSIVE`（review-1 m3）。reason `sharing`。
  7. LINEAR の image が app の usage（と共有の image 自身の TRANSFER_SRC・TRANSFER_DST・SAMPLED）と大きさを支える（`vkGetPhysicalDeviceImageFormatProperties(format, 2D, LINEAR, usage)`、review-1 m2）。reason `usage`。
  8. 診断の切り替え `VULKAN_ZEDBSD_DIRECT=0` で全て off（review-1 m6、`VULKAN_ZEDBSD_CURSOR_LOG` と同じ診断の interface）。reason `disabled`。
- **log**: chain の作成ごとに 1 行、`VK SWAPCHAIN direct=0|1 reason=… width= height= images=`（error stream、作成は稀）。

### 作成と失敗の戻り（review-1 M5）

1. 条件を満たせば `swapchain_direct_create`: images の表だけの group（`borrowed`、images を破棄しない）を作り、`swapchain_native_images_create(chain, info->imageUsage)` で app の usage 付きの共有の image と native の alias を作り、group の images に共有の image を入れ、`swapchain_owned` を立てる。
2. その作成を renderer か backend が断った時（`VK_ERROR_FORMAT_NOT_SUPPORTED`・`VK_ERROR_FEATURE_NOT_PRESENT`・`VK_ERROR_INITIALIZATION_FAILED`）だけ、`swapchain_native_images_free` で alias と共有の image を全て捨て、group を解放してから、今の道（`swapchain_group_create` → `swapchain_native_images_create(chain, 0)` → 駄目なら copied の道）へ戻る（reason `refused`）。それ以外の失敗（memory 不足など）は今の道と同じく作成の失敗として返す。
3. 破棄: `swapchain_native_images_free` が direct の時は `swapchain_owned` を外してから共有の image を破棄し、`swapchain_group_free` は borrowed の images を破棄しない。

### acquire の安全（review-1 m1）

- acquire は今と同じ: image i は app の状態が AVAILABLE で、かつ `platform->image_available(presentation[i].native_image)` の時だけ返る。今の copy の道でも copy の写し先 i が空いていないと acquire しない（同じ条件）ので、app が描く先が共有の image になっても、compositor・display が読む間に app が書くことは無い。
- 根拠: display の present（`GPU_DISPLAY_PRESENT_SYNC`）は kernel が adoption（新しい list の採用）まで返らず、`holds--` はその後（`wsi-display.c` の present_image_sync、`gpu.c:3566-3573`、`display-device.c:1528`）。Wayland は compositor が buffer の参照を手放すまで release しない（compositor の `objects.c:223-275`・`display.c:309`）。
- WS214 の B（窓を HVS の plane に載せる）が入る時の注意: plane が client の buffer を直に読む間の release の時期は WS214 の側で同じ規則（読む間は release しない）を守ること。

### display の印（review-1 M3、Q1 の判断）

- kernel の executor は「display が持つ run を使う execution」（`vulkan-queue.c` `owner_displayed`、buffer の `displayed`＝display の resource として import された数）を先に回し、display と他を交互にする（`render-worker.c` `select_session`）。今の copy の道では、その印は present の copy（display に出す image への書き）に付く。
- direct では compositor の出力の frame の pass が display に出す image へ直に書くので、その pass に印が付く。**印の意味（display に出す image への書き＝present の時点の仕事）は今と同じ**で、付く execution が copy から frame に変わる（frame は copy より大きい）。交互の規則で他の session は 1 step 以上待たない。T2 の合否に `worker=`、decode の健全さ（`came back unread`・`not answered` 無し）、30p・60p の late の比較を入れる。
- Wayland の app の共有の image は display の resource ではない（compositor が import する）ので、app の pass に印は付かない（今と同じ）。

### 期待値と効かなかった時（review-1 M4）

- 期待: 1 表示 frame で GPU の copy の pass 2 つ（各 1.3〜2 ms、videoplayer と compositor）が消え、image の pass は A1 の copy の 1 つになる。present の CPU 側は copy の記録の使い回しの確かめと command buffer の submit が軽くなる。
- bare の submit も 1 回の kernel の要求で、executor の順番（他の session の step の後ろ）を待つので、present の往復の待ちは残りうる。往復の待ちが律速のまま（T1-689: 1 往復 1.4〜4 ms、kernel の処理は約 0.25 ms、再生中の context switch が静止時の 20 倍）なら、次の手は (a) action 0（待ちと signal だけ）の execution を executor の順番を待たずに終える fast path（kernel、別 Phase）、(b) 再生中の switch の嵐の出所の特定（依頼 10 の `ps`・`kern: perf`）。

### 確かめの診断（review-1 M7）

- kernel: V3D の pass の出力先の run を display が今読んでいる（list か記録された primary が名指す、`bcm2711_display_flip_reads` → `bcm2711_display_flip_frame_use`）なら `v3d: a pass writes a run the display reads (run …, … bytes, draws …)` を最大 8 行（`vulkan-native-job.c` `check_output`、window を開いた後＝controller の mutex 無しで見る）。T2 の合否はこの行 0。
- direct の作成の失敗の戻りは host 試験で（stub の renderer が app の usage 付きの共有の image を断る、`plan/ws214/tests/vulkan-overlay-host.c` (x)）。実機は `VULKAN_ZEDBSD_DIRECT=0` で copy の道を 1 回（試験の為だけの強制失敗の env は製品の code に入れない、規約 §12）。
- 窓↔全画面の作り直し（videoplayer の起動と終了を全画面・窓で約 100 回）の前後で `v3d: memory kib=`・`v3d: alloc`（perf の行）と display の resource の数（`bcm2711: flip perf` の後の行）を比べる。

### 変えない物

- i915（Intel）・Venus（QEMU）・nvrtx（RTX、0x10de）・Linux・FreeBSD（Mesa、この libvulkan を使わない）、cursor・overlay の plane、`vkCreateSharedSwapchainsKHR`、copied の道（readback）、第三者の VK_KHR_display の app。

## 回帰の範囲（Pi の全ての Vulkan の client、review-1 M8）

- compositor の出力の swapchain（direct）: A1 の copy、合成の pass、scene の capture と restore（TRANSFER_SRC・DST）、shot の copy、turn（回転、WS217。Pi は既定で turn を出さないので試験の policy で 1 回）、heads（mirror。head の swapchain は opt-in しないので copy のまま）、VT の切り替えと console の復帰（出力の閉じ開き）、display の off/on・mode の変更・lock。
- Wayland の app（全て direct）: videoplayer、toolkit の app（Settings・Files・Terminal・Notes・Monitor・mview・Image Viewer）、browser、libegl の GL の app（glxtest・zgears・egltest）、xserver（TRANSFER_DST だけの chain、部分の region）、display-events、vkdemo・wltest。窓の resize（oldSwapchain）、全画面の切り替え、最小化・復帰。
- 変わらない: cursor plane（今のまま）、HVS の overlay の plane（WS214）。

## 実装（2026-10-16、P3 新世代）

- kernel: `render-runtime.c`（native の word を常に出し、direct swapchain の bit 2）、`display-flip.c`・`.h`（`bcm2711_display_flip_reads`、flip の状態の持ち主の static の参照）、`vulkan-native-job.c`（`check_output`）。
- libvulkan: `internal.h`（`context->direct_swapchain`）、`context.c`（bit 2 を読む）、`wsi-image.c`・`wsi-internal.h`（`vulkan_wsi_shared_image_create` に追加の usage）、`wsi-swapchain.c`（`WSI_DIRECT_VARIABLE`・`WSI_DIRECT_DISPLAY_VARIABLE`、group の `borrowed`、chain の `direct`、job の `bare`、`swapchain_create(…, alone, …)`、`swapchain_direct_eligible`（reason）、`swapchain_direct_create`、戻りの規則、作成の log、`swapchain_group_free`・`swapchain_native_images_free` の direct の扱い、`present_prepare`（direct の矩形は全体だけ。opt-in の compositor の出力だけが対象）、`present_bare`・`present_record`（bare は slot の fence だけ、slot の recording を 0 に）・`present_recording_key`（bare は key 0）・`present_submit`（bare は command buffer 0）・`present_slots_forget`（command の無い slot を飛ばす））。
- compositor: `wayland/compose.c`（出力の swapchain の作成の前後だけ `VULKAN_ZEDBSD_DIRECT_DISPLAY=1`）。
- 試験: `plan/ws214/tests/vulkan-overlay-host.c` の (x)（bit が無い前の scenario は全て direct=0、bit があっても opt-in が無い display の chain は direct=0、opt-in で direct＝`vkGetSwapchainImagesKHR` が共有の image・present は submit 1・copy 0・native present 1、全体でない矩形は拒否、renderer が共有の image を断ると copy の道に戻り、bare の後の slot で copy の present が command buffer を作って copy する）。`vulkan-overlay-host.sh`・`plan/ws141/tests/vulkan-cursor-host.sh`（stderr の検査から `VK SWAPCHAIN` の行を外す）、`vulkan-cursor-host.c`（stub の引数）、`plan/ws141/tests/vulkan-device-host-test.c`（capset が 176 byte・native の word・bit 2）、`vulkan-device-link-stubs.c`・`v3d-hardware-host-test.c`（`bcm2711_display_flip_reads` の stub）、`userland/tests/gpu-share/main.c`（新しい引数）。

## 確かめ（host と build、2026-10-16）

| 確かめ | 結果 |
| --- | --- |
| rpi4: `vmunix`・`dynamic/libvulkan.so`・`bin/wayland` | rc 0、warning 0、`arm64 vmunix check: PASS` |
| amd64: `dynamic/libvulkan.so`・`bin/wayland`・`bin/gpu-share-test` | rc 0、warning 0 |
| `sh plan/ws214/tests/vulkan-overlay-host.sh`（ordinary・ASan/UBSan、(x) を含む） | PASS |
| `sh plan/ws141/tests/vulkan-cursor-host.sh` | PASS |
| `sh plan/ws141/tests/vulkan-device-host-test.sh` | 33 行 PASS（`ws236 p012 native word with the direct swapchain bit: PASS` を含む） |
| `sh plan/ws141/tests/v3d-hardware-host-test.sh`・`display-host-test.sh`・`native-state-host-test.sh`、`sh plan/ws214/tests/display-p003a-host.sh` | PASS |
| style-check（変えた C の file、f909da7d5 との差） | 新しい指摘は `wsi-swapchain.c` の `goto cleanup` 1 つ（共有の後始末の label への前方の 1 回の jump、規約が許す形） |

- 未実施: Pi 実機（依頼 10）、5330・Venus・RTX の `direct=0` の log（T1 の起動の確かめに含める）、規約の全文の見直し（WS の最後）。

## T2 への依頼 10（Pi 4 実機、QEMU 不要、約 60 分）

1. build（この commit か merge 後の main）: `make -j16 BUILD=build/<T>/rpi4 ZEDBSD_CONFIG=plan/ws236/tests/config-rpi4-shot.mk build/<T>/rpi4/vmunix build/<T>/rpi4/dynamic/libvulkan.so build/<T>/rpi4/bin/wayland build/<T>/rpi4/bin/keiland-shot`（warning 0）。libmedia・videoplayer・他の app は今のまま（libvulkan は共有の library なので全 app が新しい物を使う）。
2. Pi: `/boot/boot0/vmunix`・`/lib/libvulkan.so`・compositor・`keiland-shot` を差し替え（前の物は退避）、boot の行に `v3d.perf=1 kern.perf=1`、reboot。
3. 流す（順に、reboot なし）:
   (a) desktop が出る。compositor の stderr（session の log）の `VK SWAPCHAIN direct=1 reason=none`（出力）と cursor の `direct=0 reason=plane`。
   (b) videoplayer: `high-1080p60-10s.mp4 --fullscreen --timeout-s=15`、同じく窓、`high-1080p30-10s.mp4 --fullscreen`（5 秒目に keiland-shot）。各の直前と直後に `ps -e -o pid,time,comm` と `sysctl hw.cputimes` を保存。
   (c) app: Settings・Terminal・Files を開いて窓の大きさを変えて閉じる、browser（あれば）、GL の app 1 つ（`zgears` か `glxtest`）、X11 の app 1 つ（xserver の上の何か 1 つ、なければ xserver の起動だけ）。
   (d) VT の切り替え 1 回（console へ行って戻る）、display の off/on か lock と解除 1 回。回転は capture 入りの compositor の試験の option（`--rotate-policy=any --rotate=180`、shot.c）で起動して 1 回（無理なら省いて報告）。
   (e) 作り直し: `for i in $(seq 50); do videoplayer --fullscreen --timeout-s=2 …; videoplayer --timeout-s=2 …; done`（約 4 分）の前後の dmesg の `v3d: memory kib=` と `v3d: alloc` の行。
   (f) 対照: `VULKAN_ZEDBSD_DIRECT=0 videoplayer --fullscreen --timeout-s=15 high-1080p60-10s.mp4`（copy の道、`direct=0 reason=disabled`）。
4. 報告: videoplayer の `FRAMES`・`DRAW`・`PLAYBACK`・`VK SWAPCHAIN`、compositor の `KWL PERF compose`・`rec`・`KWL COPY`・`VK SWAPCHAIN`、dmesg の `v3d: perf`（`passes=`・`worker=`）・`v3d: gpu tiles`・`kern: perf`・`execution failed`・`a pass writes a run the display reads`・`rpi4-mmal: (came back unread|not answered)`、PNG、(c)(d) の見え方、(b) の `ps`・`hw.cputimes` の差。
5. 合否: (a)〜(f) で画が出て崩れない・落ちない、`execution failed`・`a pass writes a run the display reads`・`came back unread`・`not answered` が 0、PNG の色・画が正しい、`v3d: perf` の 2 秒の `passes=R/0/I` の I が R と同じ程度（1 表示 frame に image の pass 1 つ）、(e) の前後で `v3d: memory kib=` が戻る（増え続けない）。記録: 60p・30p 全画面と窓の shown・late（T1-688 と比べる）、compose frame_ms、acquire_us、`worker=`。
6. 戻し: 画が出ない・落ちる時は最初に `/lib/libvulkan.so` を退避の物に戻して reboot（全 app が影響を受ける）。Pi が応答しなくなったら止めて報告（Q1 が SwitchBot で入れ直す）。
7. T1（別の機会、起動の確かめに含める）: 5330（i915）・QEMU（Venus）・RTX の desktop の起動の log に `VK SWAPCHAIN direct=0 reason=renderer` が出る（direct にならない）。
