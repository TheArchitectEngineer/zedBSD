<!-- awesome-plan project=zedbsd record=ws122-p005 -->
# ws122-p005: 設計 — 動画の全画面と合成を通さない直接の scanout（game mode）

Parent: [WS122](../ws.md)
Status: planned（2026-10-06 Q1 の設計の第 1 版。実装に取りかかれる。compositor の側は WS099）
Disposition: normal
Related: [BUG-223](../../bugs/BUG-223.md)・[BUG-208](../../bugs/BUG-208.md)・[ws142-p007](../../ws142/phase007/phase.md)

## 由来（ユーザー、2026-10-06 UAT）

「動画プレーヤーでmp4ファイルが再生できました。フレームドロップもないようです。F11やAlt+Enterで全画面にできるようにしたいです。コンポジションの効かないゲームモード、直接スキャンアウトのことです。」

## 1. 今の事実

- zdesktop は全画面の窓も合成して描く（`zwl.h` の冒頭の注記: Vulkan で背景と全ての窓を VK_KHR_display の swapchain に。全画面の窓の image の直接の scanout は ws099-p015 で外した）。input は `zwl_glass_fullscreen_input()`（`shell.c` 1102 行）が全画面の窓に送る。
- player（`userland/desktop/videoplayer/`）は全画面の要求（xdg の set_fullscreen）を持たない見込み（実装の時に確かめる）。

## 2. 設計

### 2.1 player（WS122）

- F11 と Alt+Enter で全画面の切り替え（xdg_toplevel の set_fullscreen・unset_fullscreen）。double click で同じ。Esc でも全画面から出る。
- 全画面の時は control（再生・seek の bar）を隠し、pointer が動いた時に 2 秒出す。

### 2.2 compositor（WS099）: 直接の scanout の再導入（条件つき）

- **入る条件**（全部満たす時だけ、毎 frame に判定）: 一番上の窓が全画面、その上に何も描かれない（popup・OSK・通知・pointer の cursor 以外の overlay・App Home・WiseView が無い）、窓の buffer が output と同じ大きさ・scanout できる形式（XRGB/ARGB8888 の linear か i915 の対応する modifier の dmabuf）、不透明（alpha を使わない）。
- **入った時**: その buffer を primary plane に直接（KMS の page flip・VK_KHR_display の代わりの直接の plane の設定。i915 の display の口、zdesktop の今の display の道（`display.c`）に「外の buffer を scanout する」経路を足す）。cursor は cursor plane で（合成しない）。
- **出る条件**: 上の何かが崩れた frame で、合成の道に戻る（戻る時の 1 frame の黒・ずれが出ないように、戻る frame は合成した image を先に用意してから flip）。
- Venus（QEMU）と i915（実機）の両方で、直接の scanout が使えない時は今の合成のまま（条件の最後に「backend が直接の scanout をできる」）。
- log: `ZWL SCANOUT direct=1 surface=N` / `direct=0 reason=overlay|format|size|alpha|backend`。
- 全画面の出入りは [ws142-p007](../../ws142/phase007/phase.md) の `layout_mode` と下端・上端の gesture に従う（全画面 → 最大化）。

## 3. 試験

- host: 条件の判定（overlay・形式・大きさ・alpha の組み合わせ）。
- QEMU（T1・AAT）: player の F11 で `fullscreen=1`、Esc で戻る。Venus では `direct=0 reason=backend` でも全画面の表示が崩れない。
- 実機（ユーザー）: 5330 の i915 で `direct=1`、再生の frame の落ちと tearing が無い、OSK・通知が出た時に合成に戻る。

## 4. Phase の分け方（ベータ2）

| Phase | 内容 | 見積もり |
| --- | --- | --- |
| p005a | player の F11・Alt+Enter・Esc・double click の全画面と control の隠し（WS122） | 0.3 LW |
| p005b | compositor の直接の scanout の条件と経路（i915、WS099 の Phase として立てる） | 1.5 LW |
| p005c | AAT のシナリオ・T1・実機 | 0.3 LW |

## 5. 未決

無し（ユーザーの要求の範囲で決められる）。

## p005a の実装（2026-10-06 q797 P2）

Status（p005a）: test-wait（T1 依頼中。実装・build・AAT の host 試験まで）

- 前から有った物: View の Full Screen の項（`KL_MENU_ROLE_FULLSCREEN`、compositor の menu は F11 をこの項に当てる）、F の key、全画面の時の Esc、bar は再生中は pointer の後 3 秒で隠れる。
- 足した物（`userland/desktop/videoplayer/main.c`）: F11（menu の無い desktop でも）と Alt+Enter で切り替え、picture の double click（bar の上でない所、400 ms・8 px 以内の 2 回目の press）で切り替え、全画面の時の bar は pointer の後 2 秒（`VP_BAR_FULL_US`）。log `VIDEOPLAYER FULLSCREEN toggle via=f11|alt-enter|double-click`・`VIDEOPLAYER FULLSCREEN on=0|1`。全画面から F11 で出るのは compositor（BUG-194）。
- AAT: [apps.videoplayer.fullscreen](../../../tests/scenarios/apps/videoplayer/fullscreen.md)（helper つき、needs-person で撮影を見る）。
- 確認: videoplayer の build warning 0、style-check 0、check-scenarios PASS、aat run-host PASS。QEMU は T1。

## 2026-10-06 ユーザーとの確認（p005b）

P2 の調べ（compositor は VK_KHR_display の swapchain だけで出し、client の buffer を直に出す経路が無い）に対し、ユーザー「フルスクリーンモードではappのbufferをscanoutしているはずです。確認して教えてください。」→ Q1 の確認: 直の scanout の fullscreen mode は WS035 の D0 にあったが、2026-09-30 のユーザーの指示（「全画面でコンポジット無効のモードになっているなら、それは使わないように修正して、コンポジットを有効にした上で、スワイプ操作を可能にします。」）で ws099-p015 が削除した（`userland/desktop/wayland/display.c` の冒頭の注記）。削除の前の code は GitHub の `old` branch。
ユーザーの選択（クリック）「動画・game mode だけ戻す」: app が明示に頼む全画面（game mode、動画の player の F11・Alt+Enter など）の時だけ、old の fullscreen mode を読んで直の scanout を戻す。普通の全画面は合成のまま、端の swipe を保つ。game mode の間の端の操作（解除の swipe・Esc）をどう拾うかを p005b の設計に書く。Guardrail の「compositor は libvulkan だけ」の範囲で（old の経路がどの口を使っていたかを確かめ、直の ioctl なら止めて Q1 へ）。

## 2026-10-06 q802 P2: p005a の FAIL（T1-235）の直し

証拠: `/home/awe/zedBSD-worktrees/t1/build/t1-234/`（png・logs/apps.videoplayer.fullscreen.log）。

- 症状 1（f11.png が左上の 960x600、残りは壁紙）: 最初の F11 では大きさが 960x600 → 1280x800 に変わり、player は swapchain を作り直す（`ZWL IMPORT … 1280x800` が 3 つ）。撮影は `FULLSCREEN on=1` の 0.8 秒後。compositor は全画面の窓を「今の image の大きさ」で (0,0) に描く（`body_rect`）ので、撮影の時の image はまだ 960x600 だった。double click の時は大きさが変わらず（直前の窓がすでに 1280x800）、全画面に描けていた。→ 新しい swapchain の最初の frame が 0.8 秒より遅いと推定する（QEMU の lavapipe。frame は 100〜250 ms、vkDeviceWaitIdle と 3 つの import、1280x800 の CPU の描画と複写）。止まったのか遅いだけなのかは log からは分からない。
  - player（main.c）: 新しい大きさの最初の frame を出せた時に `VIDEOPLAYER PRESENTED width=W height=H after_ms=N`。frame を出せなかった時は `VIDEOPLAYER FAILED operation=frame error=E`（同じ error は 1 回だけ。今までは黙って捨てていた）。
  - helper: 切り替えのたびに、その窓の `ZWL CONFIGURE` の大きさの `PRESENTED`（か `FAILED`）を待ってから撮る（10 秒まで）。来なければ step にそう残る。
- 症状 2（log で見つけた。Esc の後の Alt+Enter → F11 で窓が 1280x800 の floating に、x=12 で戻った）: Esc で window の大きさ 960x600 を送ったが、player がまだ描かないうちに Alt+Enter が来た。`zwl_window_enter_fullscreen` が戻り先を「今の image の大きさ」（まだ全画面の 1280x800）で覚えていた。→ protocol.c: 今の image が出力と同じ大きさで、送った窓の大きさがある時は、送った大きさを戻り先にする。helper は全画面から出た `CONFIGURE` が出力の大きさでないことも確かめる。
- 確認: wayland・videoplayer の build warning 0、keiland-linux の build warning 0、style-check 0、aat run-host PASS、check-scenarios PASS。QEMU は T1（未実施）。

## 2026-10-06 q802 P2: old の fullscreen mode の調べ（p005b、止めて Q1 へ）

Status（p005b）: 判断待ち（Guardrail「compositor は libvulkan だけ」に当たる。Q1・ユーザーの判断を待つ）

読んだもの: GitHub の `old`（`git fetch origin old`、読むだけ）の削除の commit `64da5e52f`（2026-09-30、ws099-p015）の親の `userland/desktop/wayland/display.c`。

- old の経路: 全画面の窓があると compositor は「fullscreen mode」に入る。VK_KHR_display の swapchain を閉じ（`zwl_compose_output_close`、`ZWL MODE fullscreen switch_ms=`）、kernel の GPU の UAPI を **compositor が直に ioctl** する。`GPU_DISPLAY_CLAIM` で表示の lease を取り、client の buffer の resource（`GPU_RESOURCE_IMPORT` で取った handle）を `GPU_DISPLAY_PRESENT`（`FIFO | BLOB`）で毎 frame 出す（`present_current`）。window mode に戻る時は `zwl_unscan`、swapchain の作り直し（`enter_window_mode`）。そのほかに `GPU_GET_INFO`・`GPU_DISPLAY_QUERY`・`GPU_DISPLAY_MODE`・`GPU_FENCE_QUERY`・`GPU_DISPLAY_RELEASE` も使う。
- Guardrail（2026-09-30 ユーザー、WS103 で移行済み）: compositor は GPU と表示を libvulkan だけで扱い、`include/uapi/gpu*.h` を ioctl で直に呼ばない。OS に固有の code は `libkeiland-backend-zedbsd/` だけ。old の経路はそのままでは戻せない。Q1 の指示どおりここで止める。
- 今の表示の道: libvulkan の `wsi-display.c` が swapchain の自分の image を `GPU_DISPLAY_PRESENT`（BLOB）で出している（ioctl は libvulkan の中）。

選択肢（P2 の案。決めるのは Q1・ユーザー）:

| 案 | 中身 | 規則との関係 | 費用の目安 |
| --- | --- | --- | --- |
| A | libvulkan に zedBSD の私的な拡張を足す（例 `VK_ZEDBSD_display_direct_image`）。compositor は client の dmabuf を import した VkImage（今の import.c）を、vkQueuePresentKHR の chain で表示の swapchain に「この image をそのまま出す」と渡す。libvulkan の wsi-display.c はその image の resource を `GPU_DISPLAY_PRESENT` する。swapchain は閉じないので、合成への戻りは次の present だけで済む（old の mode switch・作り直しが無い） | compositor は Vulkan だけのまま。libvulkan の API の追加（私的な拡張）が要る | 1〜1.5 LW。Venus（QEMU）では拡張を出さず、今の合成のまま |
| B | `libkeiland-backend-zedbsd/` に表示の direct の口（`kl_backend_display_scanout` など）を足し、old の ioctl をそこへ移す | OS の code の配置の規則は満たす。「compositor は libvulkan だけ」の例外になる（Guardrail の「最適化にどうしても要る直の ioctl は macro で」の扱い）。swapchain との表示の lease の取り合いを backend と libvulkan の間で調整する必要がある | 1.5 LW 以上 |
| C | 直の scanout はしない。game mode の全画面は合成のまま、glass の効果を全部外して 1 回の blit だけにする（今の `whole` の描き方にほぼ近い） | 規則に触れない | 0.2 LW。ユーザーの言う「直接の scanout」ではない |

どの案でも共通の設計（判断が出たらこのまま書き足す）:
- game mode の頼み方: app の明示の要求。標準の `wp_content_type_v1`（content type `video` / `game`）と xdg の set_fullscreen の両方がある時だけ。player は F11・Alt+Enter・double click の全画面でこれを付ける。普通の全画面（Terminal の F11 など）は合成のまま。
- 端の操作: input は direct の間も compositor が受けるので、端の swipe（解除・WiseView）は今のまま拾える。gesture が始まった frame で合成に戻り、gesture の絵を描く。Esc は app に届く（player は Esc で全画面を出る）。
- 合成に戻る条件: popup・OSK・通知・App Home・WiseView・電源の dialog・pointer の cursor 以外の overlay が出た時、buffer が出力の大きさでない、形式・modifier が出せない、alpha がある時。log `ZWL SCANOUT direct=1 surface=N` / `direct=0 reason=…`。

2026-10-06 ユーザー（クリック）: p005b は **B**（libkeiland-backend-zedbsd に direct の口、old の ioctl をそこへ移す）。Guardrail の例外の表に記録（game mode の時だけ、ioctl は backend の zedBSD の tree の中だけ）。
