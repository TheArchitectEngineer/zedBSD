<!-- awesome-plan project=zedbsd record=ws092-p002 -->

# ws092-p002: text editor の実装（file chooser を除く）

Status: cleared（2026-09-29、subagent の worktree `wt/ws092`）
Disposition: normal
Parent: [WS092](../ws.md)
Queue: main の依頼（2026-09-29「p002（実装）を始めてください」、J1〜J14 は既定を採用）

## 範囲の変更（2026-09-29、実行中）

ユーザーの方針「テキストエディタのファイルピッカーは、KeiのUIライブラリに入れるのがいいと思いました。」（main 経由）を受け、Open・Save As の
file chooser は app の中に作らず、共有の library（libkeiland）の部品として **新しい Phase ws092-p003** に分けた（大きいので、main の指示の
「大きければ別の Phase に分けて」による）。途中まで書いた app の中の chooser（`chooser.c` と app・draw の部分、置き換えの確認の dialog）は削除した。
editor は `te_host.choose(data, saving, folder, name)` を呼び、答えを `TE_EVENT_CHOSEN`（path、取り消しは空）で受ける形にした（p003 が libkeiland の
chooser でこれを満たす）。p002 の間は `choose` が NULL で、Open・Save As・Untitled の Save は「No file chooser is available.」の message になる。

## 実装（`userland/desktop/textedit/`、設計は [design.md](../design.md)）

- `buffer.c`（gap buffer と行の表）、`undo.c`（まとめ・保存の位置・上限）、`file.c`（BOM・CRLF・不正な UTF-8・NUL の拒否、一時 file と fsync と rename、
  権限の保持、link の先を保存、mtime と大きさでの変更の検出）、`layout.c`（cell・全角・tab 4・^X、単語での折り返し、行と表示の行の対応）、
  `edit.c`（移動・語・選択・自動の字下げ・複数行の Tab と Shift+Tab・Backspace と Delete のまとめ・copy・cut・paste・undo・redo・検索）、
  `find.c`、`app.c`（開く・新規・保存・dialog・pointer（click・double・triple・drag・中 click・右 click）・wheel の滑らかな scroll・blink・message）、
  `draw.c`、`canvas.c`（PDF Viewer の canvas に clip を足した）、`text.c`（Files の text.c: glyph の cache と fallback）、`keys.c`（Terminal の US の表）。
- Wayland の側: `window.c`（PDF Viewer の window に focus・serial・press の serial・横の wheel）、`clipboard.c`・`primary.c`（Terminal のものから、
  drag and drop を除き、window が自分の複写を持つ）、`menu.c`（File・Edit・View・Help と context menu）、`titlebar.c`（Open・Save・Undo・Redo・
  検索の field）、`glass.c`（card 1 枚）、`touch.c`（gesture と scroller: scroll と慣性、tap・double tap・long press）、`present.c`・`shaders/`（PDF Viewer のもの）、
  `main.c`、`Makefile`。
- 登録（main の指示どおりこの branch に最小の差分）: `userland/desktop/wayland/home.c`（1 行）、`icons.c`・`icons.h`（`GLASS_ICON_APP_TEXT`、紙に 3 行と cursor）、
  `plan/ws035/demo/apps.conf`（1 行）、`config/ci/config-amd64.mk`・`plan/ws035/tests/config-amd64-zdesktop.mk`（`textedit`）、
  `platform/amd64/vmunix.mk`（link の規則と basic の命令の除外）。**WS091・WS089 も同じ場所に足すので、merge で衝突しうる**（home.c の一覧、
  icons の enum・表・名前、apps.conf、2 つの config、vmunix.mk の除外の表と link の規則）。
- 設計からの違い: paste の受け取りは同期（Terminal と同じ 2 秒の上限の pipe の読み。design.md §10 の「非同期」はしない）。status の chip は本文の右下に重なる。

## 試験の結果

- host（`plan/ws092/tests/host-core.sh`、Linux で editor の source と libtruetype を build）: **34/34**。buffer（乱数の編集 10 万回を素朴な文字列と
  行の表と比べる、UTF-8 の前後、不正な byte）、undo（乱数の 300 の変更を全て undo で保存した text・未変更、全て redo で最後の text、語のまとめと空白）、
  file（LF、BOM と CR LF の往復、不正な UTF-8 の往復、NUL の拒否、無い file、無い folder への保存の失敗、権限の保持）、layout（折り返し・全角・tab・
  place と position の往復・折り返し無しの幅）、find（大文字小文字・回り込み・後ろ向き・無し）、edit（Enter の字下げ・Home・複数行の Tab と Shift+Tab・Ctrl+Right）。
- build（amd64、`make ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/amd64 build/amd64/bin/textedit`、`-Werror`）: exit 0、warning 0。
  `build/amd64/bin/wayland`（tile の絵）も build。
- 規約: `plan/tools/style-check.py userland/desktop/textedit/*.c` → 違反 0（最初の 36 件を直した）。全文との照合は p004。
- QEMU（Venus の desktop、main の `build/ws035-sq/hdd-image.img` の複写に自分の `textedit` と `wayland` を置いた。画面は worktree の `build/ws092/`）:
  - 開く（`/root/sample.txt`: 英語・日本語（全角 2 cell）・tab・折り返す長い行・最後の改行なし）: 行番号、折り返し、glass の card、浮いた titlebar（`shot1.png`）。
  - 入力（QMP の key、`plan/ws092/tests/qmp-keys.py`）: 文字、Enter の自動の字下げ（`shot2.png`）、Ctrl+S で保存（file の byte を SSH で確認、280 byte、
    title の `•` が消える）、Ctrl+Z・Ctrl+Shift+Z、Home と Shift+End の選択・Ctrl+C・Ctrl+V（`shot4.png`）。
  - 検索: Ctrl+F で titlebar の field、`line` で次の一致を選択し全ての一致を強調（`shot5.png`）、Enter で次。
  - 未保存の Close（Ctrl+W）の dialog（Save・Don't Save・Cancel、`shot6.png`）、pointer の drag での選択（`shot7.png`）、右 click の context menu
    （press の serial で zdesktop が開く、`shot8.png`）。
  - App Home の tile（`shot-home.png`）と tile からの起動（Untitled の窓が開いた）。
  - 大きな file（3.9 MB、60001 行、日本語を含む）: 起動から READY まで約 2.5 秒、Ctrl+End と入力が即座に反映（`shot-big.png`）。
- 未実施: touch（guest で touch の注入ができる image でない。host の gesture の試験も未。実機はユーザー）、wheel の glide の目視、中 click の PRIMARY の貼り付けと
  他の app（Terminal）との clipboard の往復、View の menu の Line Numbers・Word Wrap の切り替え（menu の click は未）、実機。
- boot test（`OUTPUT=build/ws092/boot-p002 plan/tools/boot-test.sh build/amd64/hdd-image.img`、ssh の image を worktree で build）: PASS。

## 残り・次

- p003: libkeiland の file chooser（`keiland_file_chooser_*`）と editor の Open・Save As の結線。
- p004: 規約の全文との照合と回帰。

## 引き継ぎ（2026-09-29、wrap up。以後は別のエージェントが同じ worktree で続ける）

- 状態: p002 は cleared、worktree は全て commit 済み（未完成の差分は無いので `wip.patch` は無い）。
- **p003（libkeiland の file chooser）は未着手**。API の設計もまだ書いていない。決まっているのは editor 側の受け口だけ:
  `struct te_host` の `int (*choose)(void *data, int saving, const char *folder, const char *name)`（0 か errno を返し、開いたら editor は
  `app->choosing` で答えを待つ）と、答えの `TE_EVENT_CHOSEN`（`event.text` に path、取り消しは空文字列。置き換えの確認は chooser 側で済ませて返す）。
  `userland/desktop/textedit/main.c` の `main_host()` に `choose` を足し、chooser の結果を `te_window_push(window, TE_EVENT_CHOSEN)` で積めば editor は動く
  （`app.c` の `app_choose`・`app_chosen`）。
- p003 の設計で決めること（main の指示 2026-09-29）: `keiland_file_chooser_*` の API（開く・名前を付けて保存・folder の移動・sidebar の Home と Desktop 等・
  拡張子の filter・上書きの確認の結果の返し方）、描画を app の surface に重ねるか、別の xdg の popup・子の窓（`xdg_toplevel_set_parent`、shm と libtruetype で
  library が自分で描く案が app に依らない）にするか、touch（慣性は libkeiland の scroller）、Kei の見た目（すりガラス、Files の list に揃える）。
  WS091・WS089・Notes・PDF Viewer が後で使う。`include/libc/keiland.h`・`userland/desktop/libkeiland/exports.map`・`KEILAND_VERSION` は WS089 も足すので、
  適用の直前に branch を main に合わせ、版は main の最新の次にして報告に明記する。
- 試験の道具: `plan/ws092/tests/host-core.sh`（host 34/34）、`plan/ws092/tests/qmp-keys.py`（QMP で文字・chord・pointer）、desktop の guest は
  `build/ws092/zd-start.sh`（main の `build/ws035-sq/hdd-image.img` の複写 `build/ws092/zd/hdd-image.img`、runtime `build/ws092/zd-run`）に
  `build/amd64/bin/textedit` と `build/amd64/bin/wayland` を `guest.py put` して `/tmp/wayland --glass` と `WAYLAND_DISPLAY=wayland-0 /tmp/textedit FILE` で動かす。
  textedit の build: `make -j64 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/amd64 build/amd64/bin/textedit`
  （zdesktop の image 全体の build は libcxx が共有の llvm-source に patch を当てようとして止まるので行わない）。
- 登録の差分（home.c・icons・apps.conf・2 つの config・vmunix.mk）は WS091・WS089 と衝突しうる（main が merge で解く）。
