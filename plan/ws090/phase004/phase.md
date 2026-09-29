<!-- awesome-plan project=zedbsd record=ws090-p004 -->

# ws090-p004: 窓の土台と Text Editor の移行（文字の編集の touch）

Status: cleared（2026-09-30、subagent の worktree `wt/ws090`、main 67ace742 を merge した上）
Disposition: normal
Parent: [WS090](../ws.md)
Queue: main の依頼（2026-09-30「ws090-p004（窓の土台と Text Editor の移行、文字の編集の touch）」、ユーザー不在の自走）
依存: p003（cleared）

## 範囲と受け入れ

- libkeiui の窓の土台（design.md §5）: `kui_window`（Wayland の toplevel と seat の入力、Vulkan と wl_shm の見せ方、clipboard と PRIMARY）。
- Text Editor の窓・present・touch・clipboard を libkeiui に移す。文字の編集の touch はユーザーの判断（1 本指の drag で選択、2 本指で scroll、つまみ、
  design.md §6.1）。image への登録（config の 3 file、main の許可済み）。
- 受け入れ: QEMU の Venus で開く・編集・保存が今までどおり、注入の touch で 1 本指の選択・2 本指の scroll・つまみ・double tap、host の試験、boot test。

## 実装

### libkeiui（`KUI_VERSION` 3）

- `window.c`（Text Editor の `window.c` を移した）: `kui_window_open`（options: display・題・app_id・大きさ・見せ方）、`kui_window_dispatch`、
  **event の queue**（`kui_window_take`: pointer・button・wheel・key・focus・指（時刻は CLOCK_MONOTONIC の μs に直す）・大きさの変更・close）、
  key の repeat（`kui_window_repeat` を dispatch の後に呼ぶ、BUG-111）、自分の surface 以外の入力を捨てる（ws092-p003 の教訓）、
  `kui_window_post`（app が他の object から聞いた入力を同じ queue に順に積む）、accessor（display・surface・toplevel・seat・serial・press の serial）、
  `kui_window_set_serial`、`kui_window_set_title`、`kui_window_size`、`kui_clock_us`。
- `present.c`（Text Editor の `present.c` と shader を移した）: Vulkan の swapchain（see-through の時は premultiplied）。`kui_window_present_resize`・
  `kui_window_present`（古い swapchain は EAGAIN）・`kui_window_see_through`。
- `present-shm.c`（libkeiland の file chooser の buffer の部分）: wl_shm の ARGB8888 の 2 枚。`KUI_PRESENT_NONE` は app が自分で描く。
- `clipboard.c`・`primary.c`（Text Editor の同名を移した）: `kui_window_copy`・`paste`・`can_paste`・`select`・`paste_primary`。library は log を出さない。
- `text-touch.c`: つまみを握った時の指と caret の中ほどのずれ（`grip_x`・`grip_y`）を覚え、drag の間はそれを引いて位置を求める（QEMU で、つまみの
  drag が 1 行下に入る誤りを見つけて直した）。
- design.md §5 に記録: 入力は listener ではなく event の queue（既存の app の型と BUG-111 の順序）。

### Text Editor

- 消した: `window.c`・`present.c`・`clipboard.c`・`primary.c`・`keys.c`・`touch.c`・`touch.h`・`shaders.h`・`shaders/`（libkeiui へ）。
  足した: `queue.c`（editor の入力の queue。menu・titlebar の action は `kui_window_post` で window の queue に積み、key との順を保つ）。
- `main.c`: `kui_window` で開き、window の event を editor の入力に直し、指は `kui_ui` へ。文字の領域（本文の矩形）を毎回記録し、
  指の選択・menu（`te_app_touch`）と tap（dialog の button）を editor に渡す。つまみは `kui_canvas` で本文の上に描く。
- `app.c`・`edit.c`・`textedit.h`: 本文の scroll を `struct kui_scroll` に（wheel の glide は libkeiui の式、自前の glide を除いた）。描く位置
  `scroll_x`・`scroll_y` は scroll から写し、editor が位置を決める所（reveal・選択の drag・新規・relayout）は `te_app_clamp` で scroll を動かす。
  文字の view の答え（`te_edit_position_at`・`te_layout_place`・`te_edit_word`）、つまみの表示が変わった時の描き直し、key・pointer で選択が
  変わった時につまみを消す。
- `menu.c`・`titlebar.c`・`glass.c`: window の object は accessor で。
- 実行の log（Text Editor の stderr）: `TOUCH down/up`、`TOUCH changes=…`、`CLIPBOARD set/paste received`、`PRIMARY set/paste`（呼び出し口で）。
- 大きさ: `/bin/textedit` 137 KB → 88 KB（NEEDED に `libkeiui.so`、libkeiui の関数 47 個を使う）。

### build と image

- `platform/amd64/vmunix.mk`: `libkeiui.so` の link に `libwayland-client.so`・`libvulkan.so`、Text Editor の link に `libkeiui.so`（最小の追加）。
- image への登録（main の許可 2026-09-29）: `config/ci/config-amd64.mk`・`plan/ws035/tests/config-amd64-zdesktop.mk`・`plan/ws075/demo/config-demo-hdmi.mk` に `libkeiui`。
- `plan/tools/textedit/host-core.sh`: libkeiui の scroll・text-touch・input・canvas と libkeiland の scroller も compile する（editor が使うため）。

## 試験

### QEMU（Venus、main の `build/main-pen/hdd-image.img` の複写 `build/ws090/pen/`、注入の touch）

この worktree の `textedit`・`wayland`・`terminal` と `libkeiui`・`libkeiland`・`libtruetype`・`libwayland-client`・`libvulkan` を置いた。入力は QMP
（`plan/tools/textedit/qmp-keys.py`、wheel は `plan/ws035/tests/qmp-pointer.py`）と `/bin/touchinject`、判定は画面（VNC）・Text Editor の log・SSH で読んだ file。
画面は worktree の `build/ws090-shots/p4-*.png`。

| 確かめ | 結果 |
| --- | --- |
| 開く・見た目 | PASS: 200 行の file、glass・行番号・配置は移行の前（ws092-p005）と同じ（`p4-start.png`） |
| 編集・保存 | PASS: End・文字・Ctrl+S → file に ` epsilon`。**最初は最後の `n` が保存の後に処理された**（menu の shortcut が key と別の queue に入り順が逆に）→ `kui_window_post` で直した |
| 1 本指の drag の選択 | PASS: 3 行目の先頭から右へ drag → 「Line 003: the q」を選択、両端につまみ（`p4-touch-select-zoom.png`）。最初はつまみが出なかった（drag の終わりに描き直さない）→ 直した |
| つまみ | PASS: caret のつまみを右へ 72 px → 同じ行の「…brown」まで（`p4-touch-handle-zoom.png`）。最初は 1 行下に入った（握りのずれ）→ 直した |
| 2 本指の scroll | PASS: 2 本の指を上へ 300 px → 先頭が 32 行目まで（慣性、`p4-touch-scroll2-top.png`）。その後 X を打つと 3 行目の選択が X に置き換わり、選択は保たれていた（file で確認） |
| double tap | PASS: 1 行目の "gamma" を語で選択しつまみ（`p4-touch-doubletap-zoom.png`）、Z に置き換えて保存（file で確認） |
| long press | PASS: context menu（log `MENU popup`） |
| tap | PASS: 5 行目に caret（Ln 5, Col 5、`p4-touch-tap-chip.png`） |
| wheel | PASS: 5 回で先頭が 12〜13 行目へ滑らかに（`p4-wheel-top.png`） |
| clipboard | PASS: Text Editor → Terminal（`/tmp/fromte.txt` = `alpha beta Z delta`）、Terminal → Text Editor（file の最後の行 `TERM-COPY-42`） |
| PRIMARY | PASS: 語の double click の後の中 click で 2 行目の行末に `alpha`（file で確認） |

- 再現しなかった観察: 1 度、打ち替えと保存の直後の long press と、その次の tap が効かず（Text Editor の log に指の down も無かった）、同じ手順を
  2 回繰り返しても再現しなかった。注入の touch の device は touchinject の実行ごとに作り直される（zdesktop の log の `TOUCH added/removed`）ので、
  その切り替わりと重なった可能性がある（未確認）。
- 未実施: 実機、touch の Files・Image Viewer（この Phase の対象外）、drag and drop（Text Editor は持たない）。

### host・build・boot（`build/ws090/p004-final.sh`、出力 `build/ws090/p004-final.out`）

- build（`-Werror`）: `libkeiui.so`・`/bin/textedit`・`wayland`・`terminal` 等 exit 0、warning 0。
- host: `plan/tools/textedit/host-core.sh` 34/34、`plan/ws090/tests/host-input.sh` 63/63（つまみの握りのずれに合わせて 2 つの期待を直した）、
  `plan/ws090/tests/host-draw.sh` 13/13、`plan/tools/keiland/host-chooser.sh` 75/75。
- 規約: `style-check.py`（libkeiui・textedit・試験）違反 0、`git diff --check` 0。
- boot test: `build-ssh-image.sh build/amd64`（exit 0、log の warning に libkeiui・textedit・libkeiland の file は 0）→ `boot-test.sh` **PASS**
  （`build/ws090/boot-p004/login.png`）。

## 残り

- 移行で見つけて直した 3 件（保存の順、つまみの描き直し、つまみの握り）は全て QEMU で直ったことを確かめた。
- 再現しなかった観察（long press と tap が 1 度効かなかった）は、touch の注入の device の作り直しとの重なりが疑わしい。再び見たら Bug にする。
- 次: p005（部品: button・switch・slider・field・list・sidebar・card・row・header・dialog・chip・progress と見本の program）。
- `kui_window` は shm の見せ方も持つが、QEMU で使ったのは Vulkan だけ（shm は p006 の file chooser の移行で使う）。
