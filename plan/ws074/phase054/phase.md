<!-- awesome-plan project=zedbsd record=ws074p054 -->

# ws074-p054: 部品化 1 — view の型

Phase ID: `ws074-p054`（2026-09-28 main が割り当て、[p053](../phase053/phase.md) の手順 1）
Parent: [WS074](../ws.md)
Status: in-progress（2026-09-28、前半の shell まで済み。後半の main の headless の mode が残る）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p053、p050（p058 の後）

## 範囲

view の型を作り、page と今の shell の state のうち engine の持つべきもの（scroll、履歴、timer の epoch、題名の変化の検出、
読み込み中の page、loader）をその中へ移す。shell は view の API だけを呼ぶ。main の headless の mode も同じ API に（CPU の描画、dump）。
GPU の描画の先（`browser_target`）は p055、DOM の key の形の入力と既定の動作の engine への移動は p056。

## 前半（2026-09-28 済み）: view と shell

- `view/view.h`・`view/view.c`（新、engine 側。Wayland・shell の header を include しない）: `struct browser_view`（不透明）が
  page・location・履歴（64 まで）・scroll・大きさ・font・stack の base・page の epoch・loader・取得中の page・題名・callback を持つ。
  API: `browser_view_create`・`_destroy`・`_load`（作業 directory から解決、最初の page はその場で読む）・`_follow`（page から解決）・
  `_go`（0 は再読み込み）・`_can_go`・`_stop`・`_resize`・`_url`・`_title`・`_document_height`・`_poll_fds`・`_timeout`・`_process`
  （network、timer、layout のやり直し、題名の変化）・`_scroll_by`・`_scroll_pages`・`_scroll_to`・`_scroll_y`・`_click`（scripts の
  click、その後 link の callback が許せば follow）・`_display`（p055 までの橋: display list・text・scroll）。callback: redraw・title・
  committed・load（STARTED・STOPPED・FAILED と理由）・link（policy）・console・script_error。
- draft（[browser_view.h](../phase053/browser_view.h)）との違い: 時計は view が monotonic の clock を自分で読み、`_timeout` は poll の
  ms で返す（draft の `_deadline`・`now_ms` でなく）。`_process` は poll の結果を受ける。`_draw`・`_record`・`_draw_pixels`・入力の
  関数は p055・p056。
- `shell/shell.c`: view の上に書き直した（1125 → 約 560 行）。shell に残るのは窓・titlebar・presenter、evdev の key と button を view の
  呼び出しに変えること（click の押しと離しの距離の判定を含む）、ZBROWSER の行（callback から）。最初の page の NAVIGATE は今までどおり
  窓が開いてから（`ready`）。
- 確認: host の build（-Werror）、amd64 の image の build（warning 0）、style-check（view.c・view.h・shell.c）0。Venus の guest（QEMU）で
  `browser-p045.sh`（titlebar・link・履歴・場所の欄・再読み込み）status 0（1 回目は guest の起動の直後で zdesktop が起動せず失敗、
  変更なしの再実行で status 0）、`browser-p014.sh`（scroll の key と wheel）status 0、`browser-p030.sh`（scripts の console・timer・
  click）status 0、`browser-p050.sh`（LOADING・Esc の STOPPED・http の画像、guest の HTTP 16/16）status 0、`browser-p017.sh`（https、
  別名の証明書の ERROR）status 0。写真 `/home/awe/zedBSD-rpi4/build/ws074-shots/p054-20260928-window-view-titlebar.png`・
  `p054-20260928-window-view-scripts.png`。

## 後半（残り）: main の headless の mode

再開の手順:

1. view に headless 用の API を足す: `browser_view_settle(view, budget)`（`page_settle` と、非同期の画像を待つ `main_settle_network` の
   中身を view の loader で）、`browser_view_draw_pixels`（`paint/software.c` で CPU の描画）、dump（`browser_view_dump(view, kind, out)`、
   dom・style・layout・paint）。
2. `main.c` の `main_prepare`・`main_load_async`・`main_document_arrived`・`main_settle_network`・`main_dump`・`main_render`・
   `main_run_page` を view の API に置き換え、`main_loader` を無くす。`--async` は view の `_load` の非同期の経路に（最初の page も
   非同期に読む option が要る）。
3. 確認: host の build、golden の dump 28/28、`run-http-tests.py`（同期 14/14・`--async` 16/16、ASan も）、`run-loader-tests.py` 11/11、
   render-compare（Chromium との比較）が下がらない、image の build、guest の窓の試験（上の 5 本）、boot test。
