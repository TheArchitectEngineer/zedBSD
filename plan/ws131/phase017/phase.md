<!-- awesome-plan project=zedbsd record=ws131-p017 -->

# ws131-p017: PDF Viewer・Image Viewer を新しい API へ

Status: test-wait（q807、P1、2026-10-06 実装済み・T1 の試験待ち。p016 の上で開始（Q1 の (1)）。下の「q807（P1）」）
Disposition: normal
Parent: [WS131](../ws.md)、計画の正本 [design.md](../design.md)
Queue: q807（P1）
依存: p016 cleared。D8 の単独走行で番号の順
目安: 3〜4h（1 Queue）。実行者: Q1 が割り当てる（high）
所有 path: `userland/desktop/pdfviewer/`・`imageview/`、API の不足の補い、`plan/ws131/`

## 目的と結果

2 app を新 API へ。Image Viewer の自前の present は `kl_window_vulkan_surface`。touch の結線は呼び方を変えず、共有の候補を記録。

## 範囲

1. 新名と `<keiland.h>`。menu（365・550）・titlebar（321・369）・glass（Image Viewer 191）を宣言的に。
2. PDF の password の card と inset を保つ。

## 受け入れ

- 3 OS の build（zedBSD の amd64 は exit 0・自前の warning 0、`make keiland-linux` の gcc と clang、FreeBSD の native build と `native-build-audit.py`）と `plan/tools/keiland-os-boundary/check.sh` PASS。
- 対象の app に旧名（`kui_`・`KUI_`・`keiland_`・`KEILAND_`）と自前の menu・titlebar・glass の結線・main loop が無い（grep）。結線の行数の減りを記録。
- zedBSD: `viewers-p008.sh`、`plan/tools/imageview/run-host.sh`・`imageview-guest.sh`・`touch-guest.sh`、`plan/ws079/tests/run-pdfviewer-host.sh`、`plan/ws081/tests/run-pdftouch.sh`、`demo-s8-s9.sh`、boot-test。Linux: 2 app の PNG。

## 検証の方法

[zedbsd-commands.md](../../tools/keiland-linux/zedbsd-commands.md)、[keiland-linux/README.md](../../tools/keiland-linux/README.md)、[keiland-freebsd/README.md](../../tools/keiland-freebsd/README.md)。QEMU の console・serial の log で判定しない（不具合は gdbstub・monitor・QMP）。build は自分の `BUILD=build/ws131-pNNN/`、共有の `build/` を消さない。FreeBSD の起動の確認は判断 D11 の範囲（passthrough なし）。QEMU と実機の証拠を分ける。

## 衝突・危険・rollback

- 衝突: WS128 と同じ file。 順は Q1 が決める（design.md §7.2）。
- rollback: 前へ直すのを基本にし、後ろに同じ file の変更が無い時だけ統合の commit を revert する（design.md §7.4）。

## Resume

依存の Phase の cleared と main への統合、関係する判断の決定の後に、Q1 が Queue を作る。

## q807（P1、2026-10-06）

### 変更（p016 と同じ形）

- **名前**: pdfviewer・imageview の旧名を全て新名に、`<keiui.h>` を除いた。残るのは API でない物（`KEILAND_DATADIR`・`KEILAND_BINDIR` の path、artwork の `KEILAND_MARK_LAYERS`、
  comment の `keiland_glass_v1`、build の Makefile の変数名）。
- **app**: 両方とも `kl_app_open`・`kl_app_window_create`、loop は `kl_app_dispatch`・`kl_app_take`、外観は `KL_APP_THEME`、選択は `KL_WINDOW_ACTION`（log `ACTION id= action=`）。
  `pv_window_action`・`iv_window_action`（`kl_window_post`）を除いた。chooser は `kl_app_display`。
- **宣言的**: menu は `kl_menu_entry` の表と `kl_window_set_menu`、状態は action の状態（menu と titlebar に同時）。PDF の titlebar の「Page 3 of 10」、Image Viewer の「3 / 12」は
  表の label を変えて送り直す（専用の action `PV_ACTION_PAGE_INFO`・`IV_ACTION_PLACE_INFO` で状態だけ持つ）。Image Viewer の Open With の slot は label を表に、使わない slot は
  action の HIDDEN、submenu は専用の action `IV_ACTION_OPEN_WITH_MENU` で灰色に。文脈の menu は `kl_window_popup_menu`（log `CONTEXT-MENU open` は保つ）。glass は `kl_window_set_glass`。
- **Image Viewer の present**: surface は `kl_window_vulkan_surface`（`vkCreateWaylandSurfaceKHR` を直に呼ばない）。`window.h` で Vulkan の header を先に include。
- **PDF の password の card と inset**: 変えていない（`kl_window_on_keyboard_inset` のまま）。
- **行数**: PDF menu.c 365→232、titlebar.c 321→163。Image Viewer menu.c 670→385、titlebar.c 369→159、glass.c 191→174。
- **振る舞いの差**: PDF の View の Fit Width・Fit Page は文書が無い時は menu でも灰色（titlebar と同じ action の状態）。

### 確認

zedBSD amd64 の pdfviewer・imageview の build（warning 0）、`make keiland-linux` の gcc と clang（exit 0、warning 0）、`plan/tools/imageview/run-host.sh` PASS、
`plan/ws079/tests/run-pdf-render.sh`・`run-pdfviewer-host.sh` ok。`plan/ws081/tests/run-pdftouch.sh` は既存の壊れ: 消えた `chooser.c` と `keiland-ui.h` の欠けを直したが、
`host-pdftouch.c` が古い touch の API（`PV_TOUCH_MOTION` など、今の `pv_touch_event` は `kl_window_event` を取る）で書かれていて build できない（この Phase の前から、別に直す要）。
未実施: FreeBSD の build（環境が無い）、`keiland-os-boundary/check.sh`（Q1 が main で流す）。QEMU は T1 に依頼する。
