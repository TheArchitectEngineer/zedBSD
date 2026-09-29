<!-- awesome-plan project=zedbsd record=ws089-p006 -->

# ws089-p006: 規約の全文との照合、回帰、デモの通し

Status: cleared（2026-09-29、QEMU の Venus の guest。実機は未実施）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: なし（2026-09-29 main の依頼。サブエージェントが worktree の branch `wt/ws089` で実行）

## 目的と受け入れ

- WS089 が書いた source の全体を `plan/coding-style.md` の全文と照合し、範囲の中の違反を直す。
- 回帰: WS089 の guest の試験を全部流す。デモの通し: App Home から Settings を起動し、全頁を巡り、検索と履歴を使う。
- 起動の確認は `plan/tools/boot-test.sh`。

## 規約の照合

- 範囲: `userland/desktop/settings/`（全 file）、`userland/desktop/libkeiland/preferences.c`・`network-link.c`、`userland/desktop/wayland/preferences.c`、
  WS089 が他の file に足した行（`wayland/glass.c`・`input.c`・`seat.c`・`main.c`・`icons.c`・`libkeiland/network.c`・`network-probe/main.c`）。
- 機械の検査: `python3 plan/tools/style-check.py <上の全 file> --summary` → **0**（照合の前も 0。WS089 の前の版の他の file も 0 だった）。
- 手の照合（style-check が見ない規則を script と目で）:
  - 3 つ以上の節の条件を一行に書いていた 12 か所（look.c 6・page-look.c 1・page-home.c 1・libkeiland/preferences.c 4 と wayland/input.c 1 のうち
    WS089 の行）を、節ごとの行に分けた（§6）。input.c・glass.c の他の該当は WS089 の前からの行で範囲の外。
  - 準備の代入と loop が一つの comment の下で別の判断を兼ねていた network.c の 1 か所を、二つの段落に分けた（§5・§7）。
  - public 関数の一行の comment（window.c の `se_window_push`）を複数行の形に（§3）。
  - main.c の成功の return（loop の中）に comment が無かった 1 か所に足した（§11）。comment だけで動作は同じ。
  - 他に確かめたこと（違反なし）: 前方宣言、宣言は関数の頭、初期化子で関数を呼ばない、条件の中で関数を呼ばない、goto なし、file-scope の
    変数と型の comment（main.c の 4 つの変数は一つの comment の下の群）、`_local`・`function_result` の名前なし、条件演算子は短い対称の選択だけ。
- 照合の途中で見つけた不具合（直した）: Wallpaper の頁の最初の表示が遅い（1920x1080 の壁紙 6 枚の縮小画像で最初の frame が 3.2 秒、
  `SLOW-FRAME draw=3249`）。`fread` で 6 MB を読み全体を縮めていたのを、縮小に要る行だけを `pread` で一行ずつ読むように直した。1 枚
  約 200 ms → 約 115 ms、最初の frame は 648 ms（guest の `ZSETTINGS LOOK picture ... ms=`、`SLOW-FRAME draw=648`）。その過程で
  -Werror の未初期化（width 等）を一度出し、初期化して直した。

## 回帰とデモの通し

- 新しい `plan/ws089/tests/settings-regress.sh`（guest の SSH を待ってから p006・p002・p003・p004・p005・p007・p008・p009 を順に流す）と、
  新しい `settings-p006.sh`（デモの通し）。
- **QEMU（Venus の guest、起動の直後に開始）**: `settings-regress.sh` → p006・p003・p004・p005・p007・p009 PASS、p002・p008 FAIL。
  - p002 の FAIL: 試験の期待が古かった（p008 で titlebar に Search を足し、control は 6 つ、Sidebar の ID は 5 → 6）。試験を直して流し直し → PASS。
  - p008 の FAIL: 「wi」の結果が 3 → 4（p004 の設定「Window opacity」の語 `windows` が語頭で当たる、正しい動き。最初の結果は Wi-Fi の
    まま）。試験の期待を直して流し直し → PASS。
  - 結果: 8 つの guest の試験がすべて PASS（p002・p008 は直した試験で流し直した結果）。
- デモの通し（`settings-p006.sh`、PASS）: App Home に Settings の tile（cog）、click で起動（`ZWL HOME launch name=Settings`）、Home から
  Down で 23 頁を全部巡り（`ZSETTINGS PAGE` が 24 行、準備中の頁も枠と注記）、Ctrl+F「wall」→ 2 件 → Enter で Wallpaper、Alt+Left で About に
  戻る。zdesktop の log に ERROR なし。画面 `build/ws089-shots/regress/p006/`（`grid.png` は 9 枚、目で確かめた）。
- build（worktree の `build/amd64`、-Werror）: `build-settings-image.sh` → exit 0、desktop の warning 0。host: `host-build.sh` 成功、
  `host-preferences.sh` PASS。`git diff --check` 0。
- 起動の確認: `OUTPUT=build/ws089-boot-test plan/tools/boot-test.sh build/amd64/hdd-image.img` → PASS。
- 注記: 最後の main.c の comment の追加は、試験した image の後の変更（comment だけ、build の object は同じ意味）。

## 未実施

- 実機（i915・HDMI での全頁、透明度 85% の frame の率、壁紙の差し替えの時間、Wi-Fi の join、相対の mouse での速さ）。
- デモの image（`plan/ws075/demo/build-demo-image.sh`）の実際の build（p009 で `make -n` だけ）。
