<!-- awesome-plan project=zedbsd record=ws070p010 -->

# ws070-p010: CONTROLS の presentation

Phase ID: `ws070-p010`
Parent: [WS070](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示で WS071 のサブエージェントが計画・実行。main の Queue への反映は main の session）

## 範囲

[titlebar-design.md](../titlebar-design.md) §6、§7、§9、§10: CONTROLS mode の窓の Presentation の場所（浮いたタイトルバーの題名の後ろ、
docked ではシステムバーの Application Zone）に control を配置・縮退・描画し、pointer と keyboard で操作する。

- `titlebar-shell.c`（新規）: 左（back・forward・up・home）、伸びる breadcrumb、右（search、segment の組、他の button、progress、
  primary・generic）、右端の overflow「…」。縮退: breadcrumb の先頭の段を「…」に → search を虫眼鏡に → secondary を右から
  overflow へ → normal → breadcrumb → primary だけ。hit と log（`ZWL TITLEBAR control ...`）、press と release で活性化、search と
  breadcrumb（edit）の欄（UTF-8、←→ Home End Backspace Delete、Ctrl+A、Enter・Esc・Tab・外の click・focus の喪失）、client の
  `focus_control`。
- `menu-shell.c`: overflow の popup に隠れた control の行（行の ID の上位 bit）と窓の menu の top-level を入れる（ユーザー決定 A）。
  menu の無い窓でも隠れた control があれば開く。F10 は CONTROLS の窓で overflow を開く。
- `shell.c`: 題名の幅（CONTROLS は 20%）、浮いた bar と docked の bar の描画を titlebar に、button を titlebar に。`seat.c`: 欄の key。
- 題名の bar の animation（既存）: control は bar と一緒に動き、`fade` で薄れる。

## 受け入れ

1. Venus（QEMU）で titlebar-probe `--mode=controls` の窓: 浮いた bar と docked の bar の control の画面、click・breadcrumb の段・
   search の入力・Enter・Esc の event（probe の log）、狭い窓での縮退と overflow の popup の画面。
2. MENU の窓（terminal、files）が変わらない（回帰）。
3. warning 0、style: 新しい file 0、既存の file は悪化させない。

## 結果（2026-09-27）

cleared。

- 実装: `titlebar-shell.c`（新規: CONTROLS の配置（左: back・forward・up・home、伸びるパンくず、右: 検索・segment の組・他の
  button・進みの輪・primary・generic、右端の「…」）、縮退（パンくずの先頭の段を「…」に → 検索を虫眼鏡に → secondary を右から →
  normal → パンくず）、button・segment・欄・輪の描画（`fade` で bar と一緒に薄れる）、press と release で活性化、検索とパンくず
  （edit）の欄（UTF-8、←→ Home End Backspace Delete、Ctrl+A、Enter・Esc・Tab・外の press で `text_done`）、client の
  `focus_control`、log `ZWL TITLEBAR control ...`（「…」は id=0）・`ZWL TITLEBAR focus ...`）、`menu-shell.c`（「…」の popup に
  隠れた control の行（ID は 0xf0000000 | control の ID）、separator、窓の menu の top-level（ユーザー決定 A）、
  `zwl_menu_add_overflow`・`zwl_menu_keysym`）、`menu.h`、`titlebar.h`・`titlebar.c`（object が消えるとき presentation が忘れる）、
  `shell.c`（浮いた bar と docked の bar の描画・題名の幅・button を titlebar に）、`seat.c`（欄の key）、`objects.c`、`Makefile`。
  titlebar-probe に `--width=N`。
- 実行中の設計の決定（技術的な委任の範囲）: 畳んだパンくずの先頭の「…」の click は、見えていない段のうち一番近いもの
  （見えている最初の段の 1 つ前）へ行く（設計に定めが無かった。初回の試験で「…」に hit が無いのが分かった）。
- WS035 の引き継ぎ（2026-09-27、起動の仕事が Venus で READY を遅らせ p059・p072 を壊した）に合わせ、p009 の fallback font
  （約 4 MB）の読み込みを起動から「第一の font に無い文字を初めて描くとき」へ移した（`glass.c`、log `ZWL GLASS fallback font:
  path=... faces=2`）。`titlebar-p009.sh` の期待（atlas は faces=1、fallback の行）を合わせた。
- **QEMU（Venus）**: `plan/ws070/tests/titlebar-p010.sh` **PASS**（浮いた bar の control、Back（id 1）、パンくずの「…」
  （detail 0/1）と最後の段（detail 2）、検索の focus・"abc" の text event・Enter（how=0）・Esc（how=1）、List（id 7）、docked の
  control と List、狭い（380 px）窓の縮退（shown=0）と「…」の popup、隠れた List の行の選択（via=overflow））。画面
  build/ws070-p010/floating.png・search.png・docked.png・narrow.png・overflow.png を見た（浮いた bar に ‹ › ⌂ … › 日本語・Search・
  ▦ ≡ ◫、docked のシステムバーに同じ並び、狭い窓は ‹ › ⌂ と「…」、popup に Location・Search・Icons・List・Preview）。
- 回帰（同じ image）: `titlebar-p009.sh`・`titlebar-p008.sh`・`menu-p003.sh`・WS035 `menu-regress.sh`（p059・p064・p068・p072）
  **PASS**、WS071 `files-regress.sh` は p002・p004・p006・p007・p012・p008 **PASS**、p003・p005 FAIL → 再実行で p003 PASS、p005 は
  再び FAIL。原因は zdesktop-files の側: Ctrl+A（menu の shortcut → action）と直後の「/」の key が同じ dispatch に来ると、main loop
  が窓の key を menu の action より先に処理し、「/」の後に全選択されて "tmp/fhome/Projects" になる（log `No folder at
  tmp/fhome/Projects`）。p010 の変更（zdesktop）の不具合ではない。ws071-p014 で menu の action を窓の入力の列に入れて順序を直す
  （同時に path の欄が zdesktop に移る）。
- build warning 0、style: 新しい file 0、既存の file（glass.c・shell.c・seat.c・menu-shell.c・objects.c・titlebar.c）0→0。
- 実機（i915）: 未実施。
- 画面の写し: `build/ws070-shots/p010-20260927-{floating,search,docked,narrow,overflow}.png`。
- 制限: F10 は CONTROLS の窓で「…」を開く（menu のある窓）が、試験の probe は menu を持たないので未試験（ws071-p014 の
  zdesktop-files で確かめる）。menu の無い CONTROLS の窓では F10 は何もしない。
