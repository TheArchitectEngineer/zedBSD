<!-- awesome-plan project=zedbsd record=ws070p003 -->

# ws070-p003: zdesktop の描画と操作（浮いたタイトルバー、システムバー、popup、keyboard、activation）

Phase ID: `ws070-p003`
Parent: [WS070](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行）
承認: 2026-09-27 ユーザー「GLSLコンパイラとSystem Menu Extensionについて、サブエージェントで実装を進めてもらえますか。」
設計: [design.md](../design.md) §6

## 範囲

1. 浮いたタイトルバーの題名の右、docked のときはシステムバーの docked の題名の右に top-level の項目（入らない分は「...」）。
2. popup（submenu、checkbox の ✓、radio の丸、separator、shortcut、›）、pointer（開く、切り替え、選ぶ、外の press で閉じる）、
   keyboard（F10、↑↓←→、Enter・Space、Esc、文字）、focus の窓の shortcut の実行、activation と opened・closed の event。
3. 閉じる条件（前面の窓が変わる、dock・浮き・fullscreen・最小化、App Home・Wiseview、model の変化）。

## 受け入れ

1. 浮いたタイトルバーとシステムバーに menu が出て、pointer・keyboard・shortcut で選べ、client に activation が届く（Venus、画面と log）。
2. 他の窓に隠れた題名の bar の項目は押せない。
3. 既存の zdesktop の回帰（p059〜p072）が通る。build warning 0、style-check（新しい file 0、既存は悪化させない）。

## 結果（2026-09-27）

cleared。

### 実装

- `userland/base/zdesktop/menu-shell.c`（新規）: 項目の配置と描画（題名の幅は menu があれば使える幅の 2/5 まで）、最後に描いた
  frame の位置で hit を調べる表、popup の配置（出力の端で押し戻し、submenu は親の左へ反転）と描画、menu mode の pointer と
  keyboard、F10、shortcut（zdesktop の US 配列の表で evdev → keysym。Shift で記号が変わる key は Shift を外す）、閉じる条件の
  tick、object が消えるときの forget。
- `shell.c`: 題名の bar と docked の題名の後に `zwl_menu_draw_bar`、frame の始めに `zwl_menu_frame`、システムバーの後に
  `zwl_menu_draw_popups`、button・motion・tick の hook、`zwl_glass_raise`・`zwl_glass_window_at`（隠れた題名の bar の判定）。
- `seat.c`: key の順は App Home → 開いている menu（`zwl_menu_grab_key`）→ zdesktop の shortcut → F10 と窓の shortcut
  （`zwl_menu_key`）→ client。`glass.c`・`glass.h`: atlas に ✓（U+2713）と ›（U+203A）。font に無ければ四角と「>」で代える。

### 最終の確認（2026-09-27、QEMU・Venus、lean image `plan/ws070/tests/build-menu-image.sh` → build/amd64/hdd-image.img。i915 実機は未実施）

- `plan/ws070/tests/menu-p002.sh` PASS（build/ws070-p002.log）: menu-probe の 11 の server の case と library の case が全部 ok
  （error は interface と code が合う。`ZWL ERROR client=N object=M code=C` が 10 行）。error の後も zdesktop は動き、後の terminal は
  menu を出した。
- `plan/ws070/tests/menu-p003.sh` PASS（build/ws070-p003.log、33 の確認が ok）。画面は build/ws070-p003/ の floating・edit・selected・
  keyboard・paste・submenu・view-large・docked-edit・about・two.png（自分で見て判定した: 浮いたタイトルバーの「Terminal  Shell Edit
  View Session Help」、docked のシステムバーの「zedBSD | T Terminal Shell Edit View Session Help | — ▢ ×」、無効の行は薄い、
  ✓・丸・›・shortcut、選択の青い帯）。1 回目の最終の run は新しい窓の Close の行の click が Shell の項目の上の press と扱われて
  FAIL した（pointer の移動が press より遅れて届いた。menu は Shell の再 press として正しく閉じた）。試験の click を 2 段の移動にして
  2 回続けて PASS。
- `plan/ws070/tests/menu-occlude.sh` PASS（build/ws070-occlude/）: 他の窓の本体に隠れた Shell の項目の press は menu を開かず、窓を
  前にすると開く。
- 回帰 `plan/ws070/tests/menu-regress.sh build/ws070-regress p059 p062 p063 p064 p065 p068 p069 p070 p071 p072`: 全部 PASS。
  p068 は WS070 版（`plan/ws070/tests/zdesktop-p068-menu.sh`）: WS035 の p068 は題名の bar の x+150 を double click するが、そこは
  今 terminal の Shell の項目なので menu が開く。題名（x+80）の double click にした版で PASS（WS035 の試験の直しは main の session）。
- boot test PASS（build/ws070-boot/login.png）。
- build warning 0（zdesktop、libwayland-client、libzdesktop、zdesktop-terminal、menu-probe）。style-check: 新しい file 0、変えた既存の
  file は変更前と同数（`plan/ws070/tests/style-compare.sh 24b12a47 …`）。
