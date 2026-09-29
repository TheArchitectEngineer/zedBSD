<!-- awesome-plan project=zedbsd record=ws092-p005 -->

# ws092-p005: 受け入れの残りの確認と完了の準備

Status: cleared（2026-09-29、subagent の worktree `wt/ws092`）
Disposition: normal
Parent: [WS092](../ws.md)
Queue: main の依頼（2026-09-29「ws092-p005（受け入れの残りの確認と完了の準備）」。試験の plan/tools への移動は main の許可）
依存: p004（cleared）、WS093 の Files からの起動（main に merge 済み）

## 範囲と受け入れ

1. QEMU: 注入の touch（tap で caret、選択、慣性の scroll）、中 click の PRIMARY の貼り付け、Terminal との clipboard の往復（両向き）、
   Files の double click で txt を開いて編集・保存。直せる不具合は直し、直せないものは記録して main に報告する。
2. 試験を `plan/tools/` へ移し、path を直し、移した後にそのまま通ること。
3. 記録は ws.md の p005 の行と Resume point まで（phase の directory の削除、ws.md の完了の形、master の Tools 節は main）。実機は範囲外。

## 環境

- main の `build/main-pen/hdd-image.img` の複写（`build/ws092/pen/hdd-image.img`、注入の touch の kernel と `/bin/touchinject`）を
  `GUEST_RUNTIME=build/ws092/pen-run plan/ws035/tests/zdesktop-guest.sh start` で起動（Venus）。
- この worktree（main b6342bbe を merge した上）で build した `textedit`・`wayland`・`files`・`terminal` と `libkeiland`・`libtruetype`・`libwayland-client`・
  `libvulkan`・`libpng-compat`・`libz-compat` を置いた（build は `-Werror` で exit 0、warning 0）。zdesktop は `--glass --width=1280 --height=800`。
- 入力: touch は `/bin/touchinject` の台本（`build/ws092/touch.sh`、`size 1279 799 2`、`down`・`swipe`・`up`）、key と pointer は
  `plan/tools/textedit/qmp-keys.py`、Files の double click は `plan/ws035/tests/qmp-pointer.py`。判定は画面（VNC）と app の log（SSH）と保存した file。

## 結果（QEMU の証拠。実機は未実施）

| 確認 | 結果 | 証拠 |
| --- | --- | --- |
| touch の tap で caret | PASS: 3 行目の "brown" の tap で caret が Ln 3, Col 25 | log `TOUCH tap x=280 y=84 caught=0`、`p5-touch-tap.png`・`p5-touch-tap-chip.png` |
| touch の選択 | PASS: 1 行目の "gamma" の double tap で語を選択（5 selected） | log の 2 つの tap、`p5-touch-doubletap.png` |
| touch の drag と慣性の scroll | PASS: 300 px を 8 frame で上へ drag して離すと、約 1364 px（先頭が 53 行目）まで滑って止まった | log `TOUCH rest y=1364.4`、`p5-touch-fling-1.png`（離した直後の `-0` はまだ動く前） |
| touch の long press | PASS: context menu（Undo・Redo・Cut・Copy・Paste・Select All） | log `TOUCH long-press`・`MENU popup`、`p5-touch-longpress.png` |
| 中 click の PRIMARY | PASS: 選んだ "gamma" が 60 行目の行末に入った | log `PRIMARY set bytes=5`・`PRIMARY paste own bytes=5`、`p5-primary-line.png` |
| clipboard textedit → Terminal | PASS: 1 行目を Ctrl+C、Terminal で `echo '` Ctrl+Shift+V `' > /tmp/fromte.txt` → 中身 `alpha beta gamma delta` | log `CLIPBOARD set bytes=22`・`sent bytes=22`、`p5-clip-to-terminal.png` |
| clipboard Terminal → textedit | PASS: Terminal の `TERM-COPY-42` を drag で選んで Ctrl+Shift+C、textedit の末尾に Ctrl+V、Ctrl+S → file の最後の行 `TERM-COPY-42` | log `ZTERM COPY bytes=12`・`CLIPBOARD paste received bytes=12`・`SAVE`、`p5-clip-to-textedit.png` |
| Files から開いて編集・保存 | PASS: `/tmp/demo/notes.txt` の行の double click → Text Editor が開き、1 行足して Ctrl+S → file に `- edited from Files` | Files の log `OPEN … app=Text Editor error=0`・`LAUNCH … /bin/textedit '/tmp/demo/notes.txt'`、`p5-files-open.png`・`p5-files-saved.png` |

- zdesktop の log の ERROR は 0。画面は全て `build/ws092-shots/p5-*.png`（worktree）。
- 見つけた不具合: なし（直したもの・直せないものなし）。
- 設計の注記: 依頼の「drag で選択」について、textedit の touch は設計（design.md §9、J10）どおり 1 本指の drag は scroll で、選択は double tap の語。
  範囲を広げる handle や long press の後の drag での選択は作っていない（要るならユーザーの判断で別の Phase）。
- 手順の注記: pen の image は古いので、Files を動かすには `libpng-compat.so`・`libz-compat.so` もこの worktree の build を置く必要があった（最初は
  `ld.so: cannot open dependency` で Files が起動しなかった。image の側の問題で、製品の不具合ではない）。

## 試験の移動（main の許可）

- `plan/ws092/tests/host-chooser.c`・`.sh` → `plan/tools/keiland/`（出力 `build/keiland/host-chooser`、絵 `build/keiland-shots/`）
- `plan/ws092/tests/host-core.c`・`.sh` → `plan/tools/textedit/`（出力 `build/textedit/host-core`）
- `plan/ws092/tests/qmp-keys.py` → `plan/tools/textedit/`（`plan/tools/files/qmp-input.py` との統合は後）
- script と注釈の中の path を直した。移した後: `sh plan/tools/keiland/host-chooser.sh` → 75/75、`sh plan/tools/textedit/host-core.sh` → 34/34。
  `qmp-keys.py` はこの Phase の QEMU の確認で移した場所から使った。
- master の Tools 節への登録は main（候補の文: keiland/host-chooser.sh「libkeiland の file chooser の model と描画の host 試験（75 件）と絵」、
  textedit/host-core.sh「Text Editor の文書・undo・file・表示の行・検索・編集の host 試験（34 件）」、textedit/qmp-keys.py「QMP で文字列・key の組・
  pointer を guest に送る（US の配列）」）。

## 残り

- WS092 の完了の処理（phase の directory の削除、ws.md の完了の形、Tools 節への登録、`design.md` は残す）は main。
- 実機での確認（touch・慣性・clipboard）は main がユーザーに頼む。
