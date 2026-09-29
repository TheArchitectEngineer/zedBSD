<!-- awesome-plan project=zedbsd record=ws090-p003 -->

# ws090-p003: scroll view・入力の層・文字の view の touch

Status: cleared（2026-09-29、subagent の worktree `wt/ws090`、main d0a6f5e7 を merge した上）
Disposition: normal
Parent: [WS090](../ws.md)
Queue: main の依頼（2026-09-29「ws090-p003（scroll view・入力の層・`kui_text_touch`）」）
依存: p002（cleared）

## 範囲と受け入れ

design.md §4（入力の層）・§6（scroll view）・§6.1（文字の view の touch、ユーザーの判断「スクロールは2本指にするのと、共通部品にしましょう。」）。
受け入れ: `libkeiui.so` の build warning 0、host の試験、規約の checker 0。

**範囲の変更（実行中、design.md §10 に反映）**: 設計の案では p003 で Text Editor の本文の scroll を `kui_scroll` に替えて QEMU で確かめる予定だったが、
Text Editor が libkeiui を link するには image への登録（config の 3 file、main の許可は p004 で）が要るので、**Text Editor への組み込みと QEMU は p004**
（窓の移行と一緒に、`kui_text_touch` も）に移した。

## 実装（`KUI_VERSION` 2、`include/libc/keiui.h`）

- `scroll.c`・`struct kui_scroll`（app が持つ）: 軸（X・Y）、位置、内容と viewport の大きさ。
  - wheel と key の glide は、始めの位置と目標と始めた時刻からの `1 - e^(-t / 70 ms)`（Text Editor の `APP_GLIDE_MS`）。時刻だけで決まり、0.5 px の内で止まる。
    wheel を続けて回すと前の目標に足す。
  - 指: `kui_scroll_press`・`drag`（押してからの合計）・`fling`・`cancel` を libkeiland の `keiland_scroller` に渡し、指が離れて止まるまで位置は scroller のもの
    （rubber band を含む）。止まったら端の内に戻す。
  - `kui_scroll_move_to`（glide か即座）、`kui_scroll_reveal`（見える所へ）、`kui_scroll_key`（↑↓←→ は 1 行、PageUp/Down と Space は viewport − 1 行、
    Home・End）、`kui_scroll_limit_x/y`、`kui_scroll_draw_bars`（右と下の 4 px の bar、動いた後 1 秒で薄れる、薄れる間は 1 を返す）。
  - 描画の無い使い方（Terminal・Notes）は bar を描かずに同じ struct を使える。
- `input.c`: `kui_key_character`（US の配列の表を 1 つに。Files・Text Editor・Terminal・PDF Viewer・file chooser の写しを後で置き換える）と `KUI_KEY_*`・`KUI_MOD_*`。
- `text-touch.c`・`struct kui_text_touch`（§6.1）: view は 3 つの答え（点 → 位置、位置 → caret の矩形、位置 → 語）を渡す。tap で caret、double tap で語
  （つまみ付き）、1 本指の drag で選択（押した所から）、つまみ（直径 12 px、当たりは 44 px の円）の drag でその端、long press で menu の要求、
  `kui_text_touch_edge`（端から 24 px の内で最大 1200 px/s の自動 scroll）、`kui_text_touch_draw_handles`、`kui_text_touch_take`（変化の bit）。
  key・pointer で選択を変えた時は `kui_text_touch_set_selection` でつまみが消える。
- `ui.c`・`struct kui_ui`（opaque）: frame ごとの部品の記録（`kui_ui_hit`・`kui_ui_scroll_region`・`kui_ui_text_region`、1 frame 512 まで）と、
  **画面にある frame の記録で入力を振り分ける**（最後に記録した物が上）。
  - pointer: hover の部品が変わった時だけ再描画を求める、press と release が同じ部品で click（400 ms の内の 2 回目は double）、何にも当たらない press・
    release は app の event（下にある領域の id 付き）。
  - wheel: pointer の下の scroll に、無ければ app の event。
  - touch（libkeiland の gesture 1 つ）: 最初の指が標的を決める（tap は上の部品、無ければ文字の領域、drag は下の scroll・文字の領域。list の行が上に
    あっても drag は scroll）。2 回目の tap の TAP と DOUBLE_TAP の組は double tap だけとして扱う。glide を捕まえた tap は click しない。
    文字の領域では drag の始まりの指の数が 1 なら選択、2 なら scroll。drag も tap もしなかった指（long press 等）が離れたら scroll を放す。
    何にも当たらない drag は app の event（`kui_ui_drag_offset` で量）。
  - `kui_ui_begin` は gesture（long press の時刻）・drag の追従・選択の端の自動 scroll・記録した scroll の時刻の進めを行い、`kui_ui_end` は
    記録を画面の物にして、指・drag・glide・飛行・bar の薄れの間は 1（次の frame が要る）を返す。
- `platform/amd64/vmunix.mk`: `libkeiui.so` の link に `libkeiland.so`（scroller と gesture）を足した（最小の追加、main の許可の範囲）。
  package の依存にも `desktop/libkeiland`。
- focus と Tab・key の部品への配送は部品と一緒に p005（design.md §4 のまま）。

## 試験

- build: `build/ws090-p002-build.sh`（worktree の中）→ exit 0、warning 0。export は `kui_*` の 83 個（`nm -D`）。
- host: `sh plan/ws090/tests/host-input.sh`（libkeiui の scroll・input・text-touch・ui と libkeiland の gesture・motion・scroller を Linux で build）→ **63/63**。
  - scroll: glide の 1 時定数で `100 × (1 − 1/e)`（±0.01）、止まる、2 回の wheel が足される、端、reveal の下と上、key の行・page・端・始め、内容が短く
    なった時に戻る、bar が動いた後に出て 1 秒後に消える、X だけの scroll は Y の wheel を無視、軸の無い scroll は拒否。
  - 指の scroll: drag と fling の後の位置が **libkeiland の scroller を直接動かした位置と frame ごとに同じ**、飛んで止まる。
  - pointer: hover の再描画の要否、click・1 frame だけ・double click・外で離すと click なし、scroll の上の wheel、何にも当たらない wheel・press、領域の id。
  - touch: 部品の tap、scroll の中の行の tap（scroll は動かない）、行の上の drag は scroll を動かして慣性で飛ぶ、飛んでいる時の tap は止めるだけ、
    何にも当たらない drag の event と量。
  - 文字の view: tap で caret、double tap で語とつまみ、1 本指の drag で選択（scroll は動かない）、caret のつまみ・anchor のつまみの drag、2 本指で scroll
    （選択は保つ）と止まる、long press の menu の要求と scroll の解放、下の端に指を置くと自動で scroll し選択が付いてくる、key の選択でつまみが消える、key の文字。
- `sh plan/ws090/tests/host-draw.sh` → 13/13（p002 の回帰）。
- 規約: `python3 plan/tools/style-check.py userland/desktop/libkeiui/*.c plan/ws090/tests/*.c` → 違反 0。
- 未実施: QEMU・実機（まだどの program も libkeiui を使わない。p004 で Text Editor に組み込んで確かめる）、boot test（image の中身は変わらない）。

## 残り

- p004: `kui_window`（Vulkan・shm・無し）、Text Editor の窓・present・touch（`kui_text_touch`: 1 本指で選択・2 本指で scroll・つまみ）・clipboard の移行、
  image への登録（`config/ci/config-amd64.mk`・`plan/ws035/tests/config-amd64-zdesktop.mk`・`plan/ws075/demo/config-demo-hdmi.mk` に `libkeiui`、main の許可済み）、
  QEMU（注入の touch で 1 本指の選択・2 本指の scroll・つまみ）。
