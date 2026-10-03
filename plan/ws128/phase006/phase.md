<!-- awesome-plan project=zedbsd record=ws128-p006 -->

# ws128-p006: Terminal の改善

Status: in-progress（q666、P2、2026-10-04。実装と host 試験は済み、QEMU は T1 に依頼して結果待ち）
Disposition: normal
Parent: [WS128](../ws.md)
Queue: q666（Q1 の dispatch、2026-10-04。user「任せます」→ Q1 の採否: scrollback の検索・色と font の設定の保存）
依存: p001（2026-10-04 Q1 が委任で採用）。WS090 p015（Terminal の scroll の `kui_scroll` への移行）と同時に流さない
目安: 2h（1 Queue）。実行者の目安: phase-runner-mid
所有 path: `userland/desktop/terminal/`

## 範囲

候補: scrollback の検索（Ctrl+Shift+F、一致の強調）、色の theme と font の大きさの保存（`~/.config/keiland/terminal`）、URL の click で開く。p001 で選んだ物だけ。

## 受け入れ

選んだ項目ごとの guest の手順 PASS、既存の Terminal の試験（p001 で特定）PASS、boot test PASS。

## 検証の方法と範囲

QEMU の Venus。 やっていない確認は「未実施」と書く。

## 未決の判断

なし。URL の click で開くは Q1 の採否に入っていないので扱わない。設定の key の表（WS135 の `settings-keys.c`）に 2 行を足すのは
2026-10-04 Q1 の許可（WS135 design §2.1「新しい設定は表に行を足す」）。

## 実装（2026-10-04、q666）

- **scrollback の検索**（Edit > Find...（Ctrl+Shift+F）・Find Next（Ctrl+Shift+G、古い方へ）・Find Previous（Ctrl+Shift+H、新しい方へ））:
  新しい `terminal/search.c`（Wayland を知らない）: 文字列を code point に（ASCII は大文字小文字を区別しない）、scrollback と画面の行を
  通し番号で前後に探す（wide の文字は 1 文字、行をまたがない）、行の一致の cell の印、一致の行を窓に出す（窓の中ならそのまま、外なら真ん中へ）。
  `window.c`: 検索の bar が開いている間は key が shell でなく bar へ（Esc で閉じる、Enter で古い方・Shift+Enter で新しい方、Backspace、
  印字できる ASCII と IME の確定）。`main.c` の `main_search()`: 文字が変わったら窓の下端から古い方へ、step は今の一致から。log
  `ZTERM SEARCH run= query= found= line= column= cells= view=`、`closed`。`render.c`: 窓の最後の行に bar（`Find: <text>`、block、見つからなければ
  `not found`）、見える行の全ての一致に印、今の一致は強い色。tab を替えると閉じる。
- **色の theme**（View > Theme: Dark（今の色）・Light・High Contrast、radio）: cell は既定の色を Dark の値で持ち、描く時に theme の色に置き換える
  （padding の clear・選択・IME の組み立て中の文字も）。log `ZTERM THEME run= theme= saved=`。
- **設定の保存**（WS135 の `kl_settings_*` の app の項目、`~/.config/keiland/terminal.conf`）: `terminal.font-size`（8〜32、既定 16）と
  `terminal.theme`（0〜2、既定 0）を `settings-keys.c` の表に足した。Zoom・Text Size で変えた大きさと theme を保存し、次の起動で読む
  （`--font-size=` を command line で与えた時はそれが優先）。既定の値（16・Dark）は行を残さない（reset）。log `ZTERM SETTINGS ... font_size= theme=`。
- 試験の直し: menu の項目が 31 → 39 になったので `plan/tools/titlebar/menu-p003.sh`・`plan/ws128/tests/terminal-p009-guest.sh` の
  `MENU ready items=` を 39 に。menu-p003 は zoom を保存するので、始めと終わりに terminal.conf を消す（後の試験が別の大きさで始まらないように）。
- 新しい試験: host [terminal-p006.sh](../tests/terminal-p006.sh)（検索 11 件、設定 7 件）、guest [terminal-p006-guest.sh](../tests/terminal-p006-guest.sh)。

## 確認（host。QEMU は T1 待ち、実機は未実施）

- `sh plan/ws128/tests/terminal-p006.sh` → PASS（18 件）。`sh plan/ws128/tests/terminal-p009.sh` → PASS（設定の変更の後も）。
  `plan/tools/settings/host-settings.sh`（38）・`host-store.sh`（43）PASS、`sh plan/ws089/tests/host-build.sh` rc=0（表を変えたため）。
- zedBSD amd64 の build（`BUILD=build/p2-files`、terminal と libkeiland.so）rc=0、warning 0。Linux・FreeBSD の build は未実施（Makefile に search.c を足した）。
- `plan/tools/style-check.py` は search.c・settings.c・main.c・window.c・menu.c・render.c で 0（render.c の既存の 3 件も直した）。
- 未実施（T1 へ）: `terminal-p006-guest.sh`、`menu-p003.sh`・`terminal-p009-guest.sh`（項目の数を直した）、boot test。

## Event

2026-10-02 / ws128-beta1-plan: fg019 の計画で新設。
