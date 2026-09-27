<!-- awesome-plan project=zedbsd record=ws071p012 -->

# ws071-p012: Get Info・開く・別のアプリで開く

Phase ID: `ws071-p012`
Parent: [WS071](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main の Queue への反映は main の session）

## 範囲

2026-09-27 に元の p007 から分けた後半。[design.md](../design.md) §7（開く・別のアプリで開く）、§9（Get Info）、spec §14、§21:

- 関連付け（`apps.c`）: 利用者の `$XDG_CONFIG_HOME/zdesktop/open-with`、system の `/etc/zdesktop/open-with`、内蔵の表の順に
  `PATTERNS<TAB>NAME<TAB>COMMAND`。`%f` は shell の quote をした path。`@terminal CMD` は新しい zdesktop-terminal の窓で、
  `@quicklook` は Quick Look。実行できる file は最初に「Run in Terminal」。一覧は開くたびに読む。
- 起動: fork を 2 段にして孫が `setsid` の後に `/bin/sh -c` か `zdesktop-terminal --command=`（zombie を残さない）。
- 開く（double click・Enter）: folder はこの tab、file は既定（最初の一致）。開いた file は recent へ。
- Get Info（Ctrl+I、`ui-info.c`・`info.c`）: 窓の中の card。Name、Where（full path）、Kind、MIME type、Size（bytes 付き）、Created
  「—」、Modified、Changed、Accessed、Owner・Group（名前と数）、Permissions（`-rwxr-xr-x (755)`、setuid 等も）、Link の先、Tags、
  Extended attributes（名前と大きさ）、Checksum（SHA-256、Compute で main loop の中で少しずつ）、Open with の pill（押すと起動）。
  選択が無ければ今の folder。Esc・Ctrl+I・外の click・× で閉じる。

## 受け入れ

1. host の model 試験（一覧の解釈・優先・quote・`%f` の無い command・実行 file・SHA-256 の既知の値）と host の画面（Info の card）。
2. Venus（QEMU）で: 利用者の一覧の command が走る（file に書く）、内蔵の既定で text が terminal で開く（zdesktop に 2 つ目の
   client の窓）、画像が Quick Look で開く、Get Info の card と checksum。
3. warning 0、`style-check.py` 0、回帰（p002〜p007）PASS。

## 結果（2026-09-27）

cleared。

- 実装: `apps.c`（新規: 一覧の読み込みと `fnmatch`、優先、内蔵の表、`%f` の quote、fork 2 段の起動、孫は `/dev/null` と
  descriptor 3〜1023 を閉じる = compositor への接続を持ち越さない）、`info.c`（新規: lstat・readlink・種類・タグ・xattr の名前と
  大きさ・openers、SHA-256 を libc の `SHA256*` で main loop の中で 10 ms ずつ、`fm_mode_text`）、`ui-info.c`（新規: card、
  checksum の行、Open with の pill、`fm_open_entry`・`fm_open_with`）、`ui.c`（file は既定で開く、card の描画・tick・wait・解放）、
  `ui-input.c`（Ctrl+I、card の key と click、pill と button）、`files.h`、`Makefile`。design.md §7 に決定の変更（`@terminal`・
  `@quicklook`、優先、内蔵の表から `mview`・`vi` を外した理由）を書いた。
- 試験: `host-build.sh` に libc の `openbsd-sha2.c`。`host-model.c` 17 節（利用者の一覧が先、実行 file は Run in Terminal が先、
  画像は Quick Look、一覧の行が内蔵より先、`it's a file.txt` の quote で command が path を受ける、setuid の `s`・sticky の `t`、
  xattr の名前と大きさ、SHA-256("abc") の既知の値、無い path は ENOENT）。`make-home.sh` に `~/.config/zdesktop/open-with`
  （PDF と text/plain を「Record」= `~/.opened` に path を書く、試験で窓を出さないため）。`files-p012.sh`（新規）。
- host: `files-model` **PASS**（69 項目）。画面 build/ws071-host/p012-info.png（checksum の後）・p012-info-folder.png を見た。
  host の `sha256sum` と card の値が一致。
- **QEMU（Venus）**: `files-p012.sh` **PASS**（Report.pdf が利用者の一覧で開き `~/.opened` に path、Get Info の card と
  checksum が host の値と一致、Esc で閉じる、Sunset.ppm が Quick Look、Budget.csv が内蔵の Terminal (less) で
  zdesktop-terminal の窓（ZWL MAP client=2）に less）。画面 build/ws071-p012/info.png・info-sum.png・terminal.png を見た。
  回帰 `files-regress.sh`（p002〜p007）**PASS**（build/ws071-p012-reg/）。
- build warning 0（guest の image）、host の build 0、`style-check.py` 0（zdesktop-files 全部）。
- 実機（i915）: 未実施。
- 制限: 作成日時は UFS の `struct stat` に無いので「—」。folder の大きさは項目数だけ（再帰の合計は無い）。Open With の
  submenu は p008（menubar）と p009（context menu）で、それまでは Get Info の pill から選ぶ。
