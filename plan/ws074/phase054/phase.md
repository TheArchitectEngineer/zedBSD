<!-- awesome-plan project=zedbsd record=ws074p054 -->

# ws074-p054: 部品化 1 — view の型

Phase ID: `ws074-p054`（2026-09-28 main が割り当て、[p053](../phase053/phase.md) の手順 1）
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28。前半 view と shell、後半 main の headless の mode）
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
- `shell/shell.c`: view の上に書き直した（1125 → 651 行）。shell に残るのは窓・titlebar・presenter、evdev の key と button を view の
  呼び出しに変えること（click の押しと離しの距離の判定を含む）、ZBROWSER の行（callback から）。最初の page の NAVIGATE は今までどおり
  窓が開いてから（`ready`）。
- 確認: host の build（-Werror）、amd64 の image の build（warning 0）、style-check（view.c・view.h・shell.c）0。Venus の guest（QEMU）で
  `browser-p045.sh`（titlebar・link・履歴・場所の欄・再読み込み）status 0（1 回目は guest の起動の直後で zdesktop が起動せず失敗、
  変更なしの再実行で status 0）、`browser-p014.sh`（scroll の key と wheel）status 0、`browser-p030.sh`（scripts の console・timer・
  click）status 0、`browser-p050.sh`（LOADING・Esc の STOPPED・http の画像、guest の HTTP 16/16）status 0、`browser-p017.sh`（https、
  別名の証明書の ERROR）status 0。写真 `/home/awe/zedBSD-rpi4/build/ws074-shots/p054-20260928-window-view-titlebar.png`・
  `p054-20260928-window-view-scripts.png`。この時点の boot test PASS（`p054-20260928-boot-login.png`）。実機は未実施。

## 後半（2026-09-28 済み）: main の headless の mode

計画した手順（view に headless の API、main をその上に、確認）のとおりに行った。commit `526eeb71`（main の merge は `7f6959d6`）。

- view（`view/view.h`・`view.c`）に足したもの:
  - `browser_view_options.fetch`（`enum browser_fetch`）: `BROWSER_FETCH_DEFAULT`（0、窓: 最初の page はその場で、以後の http・https の
    page と画像は block せず）、`BROWSER_FETCH_AT_ONCE`（全部その場で、loader を作らない。headless の既定）、`BROWSER_FETCH_BACKGROUND`
    （最初の page も loader で。`--async`）。
  - `browser_view_settle(view, budget, flags)`: 取得中の page の到着（loader が空になるまで poll）、`page_settle` の仮想の時計の timer、
    `BROWSER_SETTLE_LAYOUT` なら layout、画像の到着、画像の後の layout。page が無ければ（読み込みの失敗、load の callback が聞いた）ENOENT。
  - `browser_view_draw_pixels(view, pixels, width, height, stride)`: CPU の描画（`paint_software`）。行が詰まっていればその場に、
    そうでなければ詰めた bitmap に描いて行ごとに写す。
  - `browser_view_dump(view, kind, &text, &length)`: `BROWSER_DUMP_DOM`・`_STYLE`・`_LAYOUT`・`_PAINT`、text は呼ぶ側が free する。
  - `browser_add_ca_file`（draft の process 全体の設定。main は `net/net.h` を include しなくなった）。
  - **layout を必要な時に**: view は page が変わった時でなく、box が要る時（描画・layout と paint の dump・scroll・click・文書の高さ）に
    layout する（`view_update`: `page_needs_layout` か大きさの変更のときだけ、font は最初の layout で開く）。DOM の dump は font を開かず
    layout もしない（今までの main と同じ。host の font の無い DOM の dump が通る）。`_display` と `_document_height` は const でなくなり、
    `_display` は int を返す（layout の失敗）。shell は失敗を `ZBROWSER ERROR layout` と書いてその frame を描かない。
- `main.c`: `main_prepare`・`main_load_async`・`main_document_arrived`・`main_settle_network`・`main_loader`・`struct main_fetch` を消し、
  `main_open`（view を作り、load、settle）と view の callback（load の失敗を「cannot load … (TLS: …)」で、console を stderr に
  「console: …」、`--run` は stdout に）に。dump・`--render`（`draw_pixels`、PPM は main が書く）・`--run` は view の API だけ。
  `--render-gpu` だけ p055 までの橋（`browser_view_display` と `paint_gpu_render`）。main は `page/page.h`・`net/net.h` を include しない。
- 振る舞い: 読み込みの失敗の文言は今までと同じ（「cannot load URL: …」と TLS の理由）。scripts・layout の失敗は「cannot run …」に
  まとめた（試験が読むのは「cannot load」だけ）。

確認（host は Debian の cc、guest は QEMU）:

- host の build（-Werror、plain と ASan）warning 0。style-check（`view.c`・`view.h`・`main.c`・`shell.c`）0、`git diff --check` 0。
- golden の dump 28/28。変更の前後で、試験の page 7 つと画像の page 3 つの `--render`・`--render-gpu`（lavapipe）の PPM、`--run` の出力、
  4 種の dump と各 stderr の 126 file が byte で同じ（`build/p054/compare.sh`）。ASan でも同じ（GPU の描画は lavapipe の中の leak の
  報告だけ。`detect_leaks=0` で同じ PPM）。
- `run-http-tests.py` 同期 14/14、`--async` 16/16、ASan の同期 14/14・`--async` 16/16。`run-loader-tests.py` 11/11。
- render-compare（Chromium 153）: CPU の描画が変更の前と byte で同じなので一致率も同じ（blocks 81.31%、first 90.98%、floats 73.83%、
  overflow 90.82%、position 96.55%、script 89.63%、second 95.54%、backgrounds 94.49%、images 87.01%）。
- amd64 の image の build（`build-browser-image.sh`、browser の warning 0。warning は perl・openssh・openssl の package のもの）。
  Venus の guest で `browser-p045.sh`・`browser-p014.sh`・`browser-p030.sh`・`browser-p050.sh`（guest の HTTP 16/16 を含む）・
  `browser-p017.sh` すべて status 0（1 回目で）。写真 `/home/awe/zedBSD-rpi4/build/ws074-shots/p054-20260928-headless-window-link.png`・
  `p054-20260928-headless-window-http-images.png`・`p054-20260928-headless-window-scripts.png`。
- main の merge の後の boot test PASS（`/home/awe/zedBSD-rpi4/build/ws074-shots/p054-20260928-boot-login.png`、上書き）。実機は未実施。

## 残り・移管

- `--render-gpu` の `browser_view_display` の橋と shell の `_display` は p055（`browser_target`）で消す。
- 入力の DOM の key の形と既定の動作は p056。
