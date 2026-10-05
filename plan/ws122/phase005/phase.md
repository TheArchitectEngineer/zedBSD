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
