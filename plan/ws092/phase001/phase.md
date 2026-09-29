<!-- awesome-plan project=zedbsd record=ws092-p001 -->

# ws092-p001: text editor の設計

Status: cleared（2026-09-29。判断の点 J1〜J14 は既定を選び、報告でユーザーに挙げた）
Disposition: normal
Parent: [WS092](../ws.md)
Queue: main の依頼（2026-09-29、worktree `.claude/worktrees/ws092-editor`、branch `wt/ws092`、main 6e195d07 から）
Approval: main の依頼（デモに必須、ユーザーの決定）「p001（設計）→ p002（実装）→ p003（規約・回帰）」。

## 範囲と受け入れ

UTF-8 の plain text の開く・編集・保存・別名で保存、undo・redo、選択・copy・paste（clipboard と PRIMARY）、検索、行番号、慣性の scroll の設計。
Kei の見た目（すりガラス、浮いた titlebar）。画面に Keiland・libkeiland を出さない。受け入れ: 設計の文書（p002 を迷わず実装できる粒度）。

## 成果物

[design.md](../design.md): 名前と範囲、file の構成（PDF Viewer の型、Files の text・glass、Terminal の clipboard・PRIMARY・keys）、command line と log、
見た目、文書（gap buffer と行の表）、file（BOM・CRLF・不正な UTF-8・NUL、安全な保存）、undo・redo（まとめ・保存の位置）、表示の行（折り返し・全角・tab）、
編集と key・pointer・touch、clipboard と PRIMARY、検索（titlebar の field）、IME（WS095）の差し込み口、menu と titlebar、登録の差分、試験、判断の点 J1〜J14。

## 調べた事実（2026-09-29）

- desktop の app は共有の UI の library を持たず（WS090 は planning）、PDF Viewer の型（window・present・canvas・text・titlebar・menu・touch、約 8800 行）を写す。
- Files の `text.c` は glyph の cache（4096）と fallback の font（`keiland-fallback.ttf`）を持つ。PDF Viewer の `text.c` は cache も fallback も無い。
- Terminal の `clipboard.c`（wl_data_device、drag and drop を含む）と `primary.c`（primary selection）、`keys.c`（US の配列の表。zdesktop は keymap を送らない）。
- titlebar の CONTROLS に `KEILAND_CONTROL_SEARCH` の field（`text_changed`・`text_done`・`keiland_titlebar_focus_control`）がある → 検索の field に使う。
- libkeiland の `keiland_gesture`・`keiland_scroller`（`include/libc/keiland.h`）。IME（WS095）・Files からの起動（WS093）・widget の library（WS090）は planning。
- 登録の場所: `userland/desktop/wayland/home.c`（組み込みの一覧）・`icons.c`/`icons.h`（tile の絵）、`plan/ws035/demo/apps.conf`、
  `config/ci/config-amd64.mk` と `plan/ws035/tests/config-amd64-zdesktop.mk` の `ZEDBSD_USER_PROGRAMS`、`platform/amd64/vmunix.mk` の link の規則。
  main の指示（2026-09-29）で、これらは WS091・WS089 と同じ形の最小の差分をこの branch に入れる。

## Resume point

設計まで完了。次は p002（実装）。
