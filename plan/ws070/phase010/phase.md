<!-- awesome-plan project=zedbsd record=ws070p010 -->

# ws070-p010: CONTROLS の presentation

Phase ID: `ws070-p010`
Parent: [WS070](../ws.md)
Status: in-progress（2026-09-27）
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
