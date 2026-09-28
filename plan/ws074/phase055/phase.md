<!-- awesome-plan project=zedbsd record=ws074p055 -->

# ws074-p055: 部品化 2 — 呼ぶ側の描画の先（`browser_target`）

Phase ID: `ws074-p055`（2026-09-28 main が割り当て、[p053](../phase053/phase.md) の手順 2）
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p054

## 範囲

GPU の描画を呼ぶ側の描画の先の形に: engine（view）は呼ぶ側の VkImage に描いて自分で submit するか、呼ぶ側の command buffer に
記録する。framebuffer は view の中で image view ごとに作る。shell の present.c は swapchain と同期だけを持ち、窓の描画はその経路を
使う（見える結果は同じ）。p054 までの橋（`browser_view_display`）を消す。入力の DOM の key の形は p056。

## 設計

- `view/view.h`（部品の API、draft [browser_view.h](../phase053/browser_view.h) に沿う）:
  - `struct browser_gpu`（instance・physical・device・queue family）、`struct browser_target`（image・image view・format・`new_layout`・
    幅・高さ）、`struct browser_gpu_failure`（失敗した Vulkan の呼び出しと VkResult）。`browser_view_options.gpu`（NULL 可）。
  - `browser_view_set_gpu(view, gpu)`: 描く device を渡す（view は窓より先に作られ page を読むので、作成の後に渡せるようにした）。
    NULL で古い device の上の物（renderer・framebuffer）を放す。呼ぶ側はその後に device を壊してよい。
  - `browser_view_release_targets(view)`: 呼ぶ側の image view に作った framebuffer を忘れる（swapchain の作り直しの前）。
  - `browser_view_prepare(view)`: layout を今（frame を始めた後に layout で失敗しないため）。
  - `browser_view_draw(view, target, wait, signal)`: view が submit して fence を待つ。
  - `browser_view_record(view, target, commands)`: 呼ぶ側が begin した command buffer に render pass ごと記録。次の draw・record の前に
    記録した仕事が終わっていること（呼ぶ側が自分の fence を待つ）が約束。
  - `browser_view_gpu_failure(view, &failure)`、draw・record は 0・ENOENT（page 無し）・ENODEV（GPU 無し）・layout の誤り・EIO。
  - `browser_offscreen_create`・`_target`・`_read`・`_destroy`: 窓の無い呼ぶ側（`--render-gpu`、試験）のための engine 自身の device と
    image。読み戻しは 0xAARRGGBB と stride。
- draft との違い: `browser_target` に `old_layout` を置かない（view は image 全体を clear するので pass の initial layout は UNDEFINED で
  どの layout でもよい、と header に書いた）。device は作成の option と `browser_view_set_gpu` の両方。framebuffer を忘れる呼び出しと
  失敗の詳細の呼び出しを足した。
- `view/view.c`: view が renderer（`struct paint_gpu`）を持つ。最初の描画で target の format と final layout の pass で作り、違う種類の
  target が来たら作り直す。framebuffer は image view と大きさで 8 個まで（一杯なら最古を壊す。記録の約束で使用中でない）。新しい page を
  見せる時に atlas の glyph を忘れる（`paint_gpu_forget_glyphs`）: atlas は glyph を bitmap の pointer で引くが、古い page の text system が
  閉じると bitmap の memory は再利用されうるので、今までの shell の renderer（page をまたいで 1 つ）には違う glyph を描く恐れがあった。
- `paint/vulkan.c`・`gpu.h`: `paint_gpu_draw` を `paint_gpu_prepare`（instance と atlas の host の書き込み）と `paint_gpu_record`
  （呼ぶ側の command buffer に barrier と pass）に分け、draw はその 2 つと自分の submit。`paint_gpu_render` と renderer の
  「device を自分で持つ」経路（`owns_device`）を消し、`paint_offscreen_open`・`_read`・`_close`（device・image・読み戻しの command buffer）に。
  `gpu_memory` は device の handle を引数に。
- `shell/present.c`: renderer と framebuffer を持たない。instance・surface・device・swapchain・image view・present の semaphore・
  acquire の semaphore・frame の command buffer と fence。frame: 前の frame の fence を待つ → acquire → begin → `browser_view_record`
  （target は swapchain の image、`PRESENT_SRC_KHR`）→ end → submit（acquire を待ち、present の semaphore と fence を signal）→ present。
  open で `browser_view_set_gpu`、resize で `browser_view_release_targets`（device の idle の後、image view を壊す前）、close で
  `browser_view_set_gpu(NULL)`。今までの `paint_gpu_draw` は frame ごとに fence を CPU で待ったが、今は次の frame の始めに待つ。
- `shell/shell.c`: frame の前に `browser_view_prepare`（失敗は `ZBROWSER ERROR layout`、その frame は描かない）。
- `main.c`: `--render-gpu` は `browser_offscreen` に `browser_view_draw` で描いて読み戻す（host の lavapipe の試験が新しい経路を通る）。
  main は `paint/gpu.h` を include しない。

## 確認（host は Debian の cc と lavapipe、guest は QEMU の Venus）

- host の build（-Werror、plain と ASan）warning 0。style-check（`view.c`・`view.h`・`main.c`・`shell.c`・`present.c`・`internal.h`・
  `vulkan.c`・`gpu.h`）0、`git diff --check` 0。
- p054 の前の基準と比べて、試験の page 7 つと画像の page 3 つの `--render`・`--render-gpu`（新しい `browser_view_draw` と offscreen の
  経路）の PPM、`--run`、4 種の dump と stderr の 126 file が byte で同じ。ASan（`detect_leaks=0`、leak は lavapipe の中）でも同じ。
  golden の dump 28/28。`run-http-tests.py` 同期 14/14、`--async` 16/16、ASan の `--async` 16/16。
- amd64 の image の build（browser の warning 0）。Venus の guest で `browser-p045.sh`・`browser-p014.sh`（GPU と CPU の描画の比較
  first・blocks agree）・`browser-p030.sh`・`browser-p050.sh`（guest の HTTP 16/16）・`browser-p017.sh`・`browser-p021.sh`（images の
  GPU と CPU agree）・`browser-p052.sh`（backgrounds agree）すべて status 0。`browser-p030.sh` は 1 回目に zdesktop の socket が出来る前に
  browser が起動して「cannot open a window: No such file or directory」（試験の起動の待ちの時間の揺れ、p054 の前半でも同じ種類の失敗）、
  変更なしの再実行で status 0。
- 写真: `/home/awe/zedBSD-rpi4/build/ws074-shots/p055-20260928-target-window-images.png`・`p055-20260928-target-window-scrolled.png`・
  `p055-20260928-target-window-link.png`・`p055-20260928-target-gpu-vs-cpu-images.png`。
- main の merge（`abe34b39`）の後の image の build（warning 0）と boot test PASS（`p055-20260928-boot-login.png`）。実機（i915）は未実施。

## 残り・移管

- 入力の DOM の key・code・text の形と、scroll・link・focus の既定の動作の engine への移動は p056。
- `browser_view_draw` の自分の submit は fence を CPU で待つ（`--render-gpu` と試験向け）。窓は `_record`。
- view を複数の thread で使うこと、1 つの device に複数の view（queue の共有は呼ぶ側の同期）は範囲外のまま。
