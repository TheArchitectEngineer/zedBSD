<!-- awesome-plan project=zedbsd record=ws092-design -->

# WS092 の設計: text editor（Text Editor、`/bin/textedit`）

2026-09-29 ws092-p001。目標は [ws.md](ws.md)（ユーザー「テキストエディタの追加。（シンプルなものでいいです）」、デモに必須）。見た目は
[Kei の基準](../ws035/kei-identity-design.md) と Files の浮いたすりガラス（`userland/desktop/files/glass.c`、[ws071 spec](../ws071/spec.md) §1・§2）、
形は PDF Viewer（`userland/desktop/pdfviewer/`: window・present・canvas・menu・titlebar・touch）、文字は Files の `text.c`（glyph の cache と fallback の font）、
clipboard と PRIMARY は Terminal（`userland/desktop/terminal/clipboard.c`・`primary.c`）、key の文字は Terminal の `keys.c`（US の配列）に合わせる。

## 1. 名前と範囲

- program `/bin/textedit`、application ID `textedit`、画面の名前は **Text Editor**（App Home の tile、window の title「<file 名> — Text Editor」、
  変更があれば名前の前に `•`、名前の無い新しい文書は「Untitled」）。画面の文字に Keiland・libkeiland を出さない（「Kei」の決まり）。
- 範囲（p002）: UTF-8 の plain text の開く・編集・保存・別名で保存・新規、undo・redo、選択・cut・copy・paste（Wayland の clipboard と PRIMARY）、
  検索（次・前、全ての一致の強調）、行番号（View で切り替え）、行の折り返し（View で切り替え）、慣性の scroll（touch・wheel）、文字の大きさの変更、
  閉じる時の未保存の確認、command の引数の file、Open・Save As の自前の file chooser、App Home への登録。
- 範囲外（後の WS）: 日本語の入力（WS095 の IME。§12 の差し込み口だけ用意する）、Files からの起動（WS093。`files/apps.c` の表は触らない）、
  置換、複数の tab・window 内の複数の文書、syntax の色、UTF-8 以外の符号化の変換、印刷、WS090 の共有 widget への移行（WS090 が出来たら移す）。

## 2. 構成（`userland/desktop/textedit/`）

desktop の app の型（app ごとに window・present・canvas・text を持つ）。元の file を写し、接頭辞を `te_` にする。

| file | 役割 | 元 |
| --- | --- | --- |
| `main.c` | 引数、loop（入力・tick・frame・cursor の点滅）、`TEXTEDIT READY/DONE/FAILED` の log | pdfviewer `main.c` |
| `textedit.h` | 型と共有の宣言（Wayland も Vulkan も知らない部分） | pdfviewer `viewer.h` |
| `window.c`・`window.h` | xdg toplevel、pointer・keyboard（key の repeat）・touch、close の要求、titlebar・menu・glass・clipboard の結線 | pdfviewer `window.c` |
| `present.c`・`shaders/`（`canvas.vert/frag`・`regenerate.py`）→ `shaders.h` | Vulkan: CPU の canvas を見せる 1 層 | pdfviewer `present.c` |
| `buffer.c` | 文書: gap buffer（UTF-8 の byte）、行の始まりの表、挿入・削除、位置の変換 | 新規 |
| `undo.c` | undo・redo の記録（操作の列、まとめ、保存の位置） | 新規 |
| `file.c` | 読み込み（大きさ・NUL・BOM・改行の検出）、保存（一時 file と rename、権限の保持） | 新規 |
| `layout.c` | 表示の行: 折り返し（cell の幅）、tab、全角の 2 cell、表示の行 ⇄ byte の位置、行番号の幅 | 新規 |
| `edit.c` | 編集の操作: cursor の移動（文字・語・行・page・文書）、選択、入力、削除、自動の字下げ、key の解釈 | 新規 |
| `find.c` | 検索（大文字小文字を無視、次・前、見える範囲の一致） | 新規 |
| `draw.c` | canvas: card、gutter、本文、選択・一致・cursor、status の chip、dialog（未保存の確認）、chooser、message | pdfviewer `draw.c` |
| `chooser.c` | Open と Save As の file chooser（Save As は名前の field） | pdfviewer `chooser.c` |
| `clipboard.c`・`primary.c` | wl_data_device（copy・paste）と primary selection | terminal の同名 |
| `keys.c` | evdev の code → 文字（US の配列、Shift） | terminal `keys.c` |
| `touch.c`・`touch.h` | wl_touch → `keiland_gesture`・`keiland_scroller`（scroll・慣性・rubber band）、tap・double tap・long press | pdfviewer `touch.c` |
| `menu.c`・`titlebar.c` | system menu と浮いた titlebar の control（検索の field） | pdfviewer の同名 |
| `glass.c` | keiland_glass の card | files `glass.c` |
| `canvas.c`・`text.c` | CPU の描画と文字（glyph の cache、fallback の font） | files の同名（pdfviewer の canvas の関数の名前に合わせる） |
| `Makefile` | `ZEDBSD_USERLAND_PACKAGE`（textedit、desktop、依存: libvulkan libwayland libkeiland libtruetype） | pdfviewer `Makefile` |

Wayland・Vulkan を知らない `buffer.c`・`undo.c`・`file.c`・`layout.c`・`edit.c`・`find.c` は host（Linux）でも build でき、host の試験（§15）が直接呼ぶ。

## 3. 起動と command line

```
textedit [--display=NAME] [--font=PATH] [--fallback-font=PATH] [--width=N] [--height=N] [--timeout-s=N] [FILE]
```

- 既定の font は `/usr/share/fonts/keiland-mono.ttf`（等幅、Terminal と同じ）、fallback は `/usr/share/fonts/keiland-fallback.ttf`（日本語など。無くても動く）。
  UI の文字（chip・dialog・chooser）は `/usr/share/fonts/keiland.ttf`。
- FILE があれば開く。無い path なら「新しい file」として名前だけ持ち、最初の保存で作る（`textedit notes.txt` の習慣）。FILE が無ければ Untitled。
- 開いた file は `keiland_recent_add(path, "textedit")`。
- 標準 error に 1 行ずつ: `TEXTEDIT READY`（最初の frame）、`TEXTEDIT OPEN path=<p> bytes=<n> lines=<n> crlf=<0|1> bom=<0|1>`、
  `TEXTEDIT SAVE path=<p> bytes=<n>`、`TEXTEDIT DONE reason=<r>`、`TEXTEDIT FAILED <何が>`（試験は SSH でこの log と保存した file を読む）。

## 4. 見た目

- **window**: 本体は 1 枚のすりガラスの card（`KEILAND_GLASS_CARD`、角の半径 18、window の縁から 8 px 内側）。zdesktop の浮いた titlebar
  （CONTROLS）がその上に離れて浮く。card の中は白に近い不透明（#fbfcfd、文字を読むため glass を透かさない）で、余白は左右 16 px・上下 12 px。
  glass を持たない compositor・see-through でない swapchain では淡い slate の不透明の地（Files と同じ判断）。
- **本文**: 等幅の 15 px（Ctrl+`=`・Ctrl+`-` で 10〜32 px、Ctrl+`0` で戻す）、行の高さは font の ascent+descent+line gap に 4 px を足す。文字色 #1e293b。
- **gutter**（行番号、既定で表示）: 本文の左に行番号を右揃えで、色 #94a3b8、今の行の番号は #334155。折り返した続きの行には番号を出さない。
- **選択**: 淡い青（#cfe3ff、window が focus を失うと #e2e8f0）。**検索の一致**: 淡い黄（#fde68a）、今の一致は濃い黄（#fbbf24）。
- **cursor**: 2 px の縦棒（#2563eb）、530 ms ごとに点滅（入力の後 530 ms は点けたまま）。focus が無いと点滅しない枠だけ。
- **status の chip**: card の右下に小さな glass の chip（`Ln 12, Col 5`、選択中は `(34 selected)`、`CRLF` の file は `CRLF`）。
  常に表示（本文の右下の角に重なる。重なった行の文字は chip の下に透ける）。
- **message**（保存した・保存できない・見つからない 等）: card の下端の中央に 2.5 秒の chip（pdfviewer と同じ）。
- **dialog**（未保存で閉じる・Open・New）: card の中央に glass の card「Save changes to "<名前>"?」と 3 つの button（**Save**・Don't Save・Cancel）。
  Enter が Save、Esc が Cancel。
- **chooser**（Open・Save As）: pdfviewer の chooser の形（folder の一覧、上の段に今の folder）。Save As は下に名前の field（今の名前を選択した状態）と
  Save の button。既にある file の名前なら「Replace "<名前>"?」を確かめる。
- 文字は §3 の font。画面の文字は英語（他の app と同じ）。

## 5. 文書（`buffer.c`）

- **gap buffer**: UTF-8 の byte の配列と gap。初めの大きさは file の大きさ + 64 KiB、足りなくなったら倍。挿入・削除は gap を位置へ動かして行う。
- **行の表**: 各論理行の始まりの byte の位置（gap の外の論理の位置）。挿入・削除の後、変わった行から後ろを差分でずらし、足した改行・消した改行の分だけ
  表を広げ・縮める（O(行の数)。16 MiB の file でも 1 回の編集で数 ms）。
- 位置は「論理の byte の位置」（gap を除いた先頭からの byte 数）で表す。cursor と選択の端は常に UTF-8 の文字の境目に置く（不正な byte 列は 1 byte を 1 文字と数える）。
- API: `te_buffer_insert(buf, pos, bytes, n)`、`te_buffer_delete(buf, start, end)`、`te_buffer_byte(buf, pos)`、`te_buffer_copy(buf, start, end, out)`、
  `te_buffer_line_of(buf, pos)`・`te_buffer_line_start(buf, line)`・`te_buffer_line_end(buf, line)`、`te_buffer_next_char`・`te_buffer_prev_char`。
- 大きさの上限: 読める file は 16 MiB まで（超えれば「This file is too large to edit」）。編集で増える分の上限は 64 MiB。

## 6. file（`file.c`）

- **読み込み**: 全体を読む。NUL を含む file は「This file isn't plain text」で開かない（既定 J3）。UTF-8 の BOM は取り除いて覚え、保存で戻す。
  改行: CRLF が LF 単独より多ければ CRLF の file とし、読む時に CR LF を LF にし、保存で LF を CRLF に戻す（混在は多い方に揃う、既定 J4）。
  不正な UTF-8 はそのまま byte で持ち、表示は U+FFFD、保存はそのままの byte（壊さない）。開いた時に「Some bytes aren't valid UTF-8」を message で示す。
- **保存**: 同じ directory に `.<名前>.textedit-<pid>` を作って全体を書き、`fsync`、元の file の mode（無ければ 0644 & ~umask）を `fchmod` で移し、
  `rename` で置き換える（途中で失敗しても元の file は壊れない）。書けない directory・権限は「Can't save "<名前>": <理由>」。
  最後の行に改行が無ければそのまま（足さない、既定 J5）。
- 保存が成功したら undo の「保存した位置」を今に置き（§7）、title の `•` を消し、`TEXTEDIT SAVE` を出す。
- 開いている間に外で file が変わったかは見ない（既定 J6。保存の前に mtime だけ比べ、変わっていれば「"<名前>" changed on disk. Overwrite?」を確かめる）。

## 7. undo・redo（`undo.c`）

- 記録は操作の列: `{ 種類（挿入・削除）, 位置, byte 列, 前の cursor と選択, 後の cursor と選択, まとめの番号 }`。
- **まとめ**: 続けて打った文字（挿入が前の挿入の直後に続き、1 秒以内、空白・改行を越えない）、続けた Backspace・Delete は 1 つの undo の単位にする。
  paste・cut・選択の置き換え（削除 + 挿入）・字下げは 1 回で 1 単位。
- Ctrl+Z で 1 単位戻し（cursor と選択も戻す）、Ctrl+Shift+Z と Ctrl+Y でやり直す。新しい編集で redo の列は捨てる。
- 上限: 記録の byte の合計 32 MiB か 10000 単位を超えたら古い方から捨てる。
- **変更の有無**: 「保存した位置」（undo の列の中の番号）と今の位置が違えば変更あり。保存の後に undo で戻れば変更ありになる。

## 8. 表示の行（`layout.c`）

- **cell**: 等幅の font の advance を 1 cell とする。East Asian Width が W・F の文字（CJK・全角・絵文字の主な範囲の表）は 2 cell。tab は次の 4 の倍数の
  cell まで（既定 J7。tab の文字は保存でそのまま）。制御文字（U+0000〜U+001F の tab・改行以外、U+007F）は `^X` の 2 cell で淡く。
- **折り返し**（既定で on、View > Word Wrap）: 本文の幅の cell 数で、最後の空白の後で切る（空白が無ければ文字の境目で切る）。各論理行の表示の行の数を
  cache し、編集で変わった論理行と window の幅・文字の大きさが変わった時に計り直す（幅の変更は全体を計り直す。O(文書)、16 MiB で約 100 ms を許す）。
  折り返しが off なら 1 論理行 = 1 表示行で、横の scroll がある。
- **座標**: 表示の行の番号 → (論理行, その行の中の byte の範囲)、byte の位置 ⇄ (表示の行, cell の列)、点 (x, y) → byte の位置（最も近い文字の境目）。
  上下の移動は「望む列」（最後に左右に動いた時の cell の列）を保つ。
- **glyph の描画**: `text.c` は Files の `text.c` を元にし、main（等幅）に無い文字を fallback で描く。2 cell の文字は 2 cell の中央に置く。
  cache は (code point, 大きさ) で 4096 個（Files と同じ）。

## 9. 編集と key（`edit.c`、`keys.c`）

- 文字: zdesktop は keymap を送らず evdev の code を渡すので、Terminal と同じ US の配列の表（`keys.c`）で文字にする（Shift・Caps Lock）。
  日本語の配列・IME は WS095（§12）。
- key（Ctrl の組み合わせは system menu の shortcut とも一致させる）:

| key | 動作 |
| --- | --- |
| ←・→・↑・↓、Home・End、PageUp・PageDown | 文字・表示の行・行頭（字下げの後 → 行頭の往復）・行末・画面の分 |
| Ctrl+←・→ | 語の単位（英数字と `_` の並び、日本語は文字の種類（漢字・ひらがな・カタカナ）の並び） |
| Ctrl+Home・End | 文書の先頭・末尾 |
| Shift+上の全て | 選択を広げる |
| Backspace・Delete、Ctrl+Backspace・Ctrl+Delete | 文字・語の削除（選択があれば選択を削除） |
| Enter | 改行と、今の行の先頭の空白（space・tab）を写す自動の字下げ（既定 J8） |
| Tab・Shift+Tab | tab の文字を入れる。選択が複数行なら各行の先頭に tab を足す・1 つ取る |
| Ctrl+A・C・X・V、Shift+Insert・Ctrl+Insert・Shift+Delete | 全選択・copy・cut・paste |
| Ctrl+Z・Ctrl+Shift+Z・Ctrl+Y | undo・redo |
| Ctrl+N・O・S・Shift+S・W・Q | 新規・開く・保存・別名で保存・閉じる・終わる |
| Ctrl+F、Enter（検索の field）・F3・Ctrl+G、Shift+F3・Ctrl+Shift+G、Esc | 検索の field へ、次・前、検索を閉じて本文へ |
| Ctrl+`=`・`-`・`0` | 文字の大きさ |

- **Ctrl+N**（新規）: 同じ window で「Untitled」にする（未保存なら §4 の dialog）。複数の window は作らない（既定 J9。新しい window は App Home から）。
- **pointer**: click で cursor、drag で選択（上下の端の外で自動の scroll）、Shift+click で選択を広げる、double click で語、triple click で行、
  中 click で PRIMARY を click の位置へ貼る、wheel で滑らかな scroll（`keiland_scroller` の fling を wheel の量で起こす、pdfviewer と同じ）、
  右 click で context menu（Undo・Redo・Cut・Copy・Paste・Select All、`keiland_menu_popup`）。
- **touch**（`touch.c`、pdfviewer の touch.c と同じ部品）: 1 本指の drag で scroll（慣性と rubber band は `keiland_scroller`）、tap で cursor、
  double tap で語の選択、long press で context menu。選択の handle（つまみ）は作らない（既定 J10。範囲の調整は pointer か Shift+矢印）。

## 10. clipboard と PRIMARY（`clipboard.c`・`primary.c`）

- Terminal の `clipboard.c`（wl_data_device、`text/plain;charset=utf-8` と `text/plain`・`UTF8_STRING`）と `primary.c`（`zwp_primary_selection_v1`）を
  写し、drag and drop の部分は除く（受ける file の drop は WS093）。
- copy・cut は clipboard の持ち主になり、選択を変えるたびに PRIMARY の持ち主になる（空の選択では手放さない、X と同じ）。
- paste は data offer の pipe を非同期に読み（main loop の poll に入れる）、読み終えたら 1 つの undo の単位として挿入する。CR LF は LF に。
  16 MiB を超える paste は切り捨てて message。

## 11. 検索（`find.c`、titlebar の検索の field）

- titlebar（CONTROLS）の `KEILAND_CONTROL_SEARCH` の field（placeholder「Find」）。Ctrl+F で `keiland_titlebar_focus_control`、打つたびに
  （`text_changed`）cursor の位置から次の一致へ移って選択し、見える範囲の全ての一致を淡い黄で塗る。Enter（`text_done` の SUBMITTED）・F3 で次、
  Shift+F3 で前、Esc（CANCELLED）で本文へ戻る。
- 照合は byte の並びで、ASCII の大文字小文字を無視する（既定 J11）。末尾から先頭へ回り込み、回り込んだら「Search wrapped」を message、無ければ
  「No matches for "<語>"」。
- 見える範囲の一致は frame ごとに見える論理行だけを探す（全体の一致の数は数えない）。

## 12. 日本語の入力の差し込み口（WS095 のため）

- 入力は全て `te_edit_insert_text(app, utf8, length)`（選択の置き換え、undo のまとめ）を通す。WS095 は `zwp_text_input_v3` の commit_string を
  この関数に、preedit を「cursor の位置に下線付きで描く未確定の文字列」（`app->preedit`）に渡す。p002 では preedit を描く所（`draw.c`）と
  cursor の矩形を返す関数（`te_layout_cursor_rect`、set_cursor_rectangle 用）を用意し、protocol の結線は WS095 が行う。

## 13. menu と titlebar

- system menu（`keiland_menu`）: **File**（New Ctrl+N、Open… Ctrl+O、Save Ctrl+S、Save As… Ctrl+Shift+S、Close Ctrl+W、Quit Ctrl+Q）、
  **Edit**（Undo・Redo・Cut・Copy・Paste・Select All、Find… Ctrl+F、Find Next F3、Find Previous Shift+F3）、
  **View**（Line Numbers（check）、Word Wrap（check）、Bigger Text、Smaller Text、Actual Size）、**Help**（About Text Editor）。
  Undo・Redo・Cut・Copy は状態に応じて enabled を切り替える。
- titlebar の control（CONTROLS）: Open、Save（PRIMARY_ACTION、未保存の時だけ enabled）、Undo、Redo、検索の field。狭ければ zdesktop が「...」に畳む。

## 14. 登録（App Home）と image への組み込み

main の指示（2026-09-29）で、WS091・WS089 と同じ形の最小の差分をこの branch に入れる（衝突は main が merge で解く）。

1. `userland/desktop/wayland/home.c`: `home_add_app("Text Editor", "/bin/textedit", "text editor edit txt notepad write", 0x1f9e9aU, "text");`
2. `userland/desktop/wayland/icons.c`・`icons.h`: `GLASS_ICON_APP_TEXT`（角の丸い紙に 3 本の横線と鉛筆の線画、他の絵と同じ線の太さ）、
   `icon_app_names` の `"text"`、`icon_app_ids` の `{ "textedit", GLASS_ICON_APP_TEXT, 0x1f9e9aU }`。
3. `plan/ws035/demo/apps.conf`: `Text Editor|/bin/textedit|text editor edit txt notepad write|1f9e9a|text`。
4. `config/ci/config-amd64.mk`・`plan/ws035/tests/config-amd64-zdesktop.mk` の `ZEDBSD_USER_PROGRAMS` に `textedit`、`platform/amd64/vmunix.mk` に
   pdfviewer と同じ link の規則（`DYNAMIC_TEXTEDIT_OBJS`、libvulkan・libwayland-client・libkeiland・libtruetype）と、basic の命令の除外の表に `textedit`。

## 15. 試験（p002・p003）

- **host の試験**（`plan/ws092/tests/`、Linux で build）: buffer（挿入・削除・行の表を乱数の編集 10 万回で素朴な文字列と比べる）、undo（乱数の編集の後に
  全て undo すると元、全て redo すると最後、まとめの規則）、file（LF・CRLF・BOM・不正な UTF-8・NUL の拒否・保存の往復が byte で同じ・権限の保持・
  書けない directory）、layout（折り返しの位置、全角、tab、座標の往復、望む列）、edit（語の移動、自動の字下げ、複数行の字下げ）、find（大文字小文字、回り込み）。
- **guest の試験**（Venus、main の desktop の image の複写に自分の `textedit` と App Home の一覧を置く。WS087 と同じく、clang・libcxx を含む image は build しない）:
  画面を `zdesktop-shot.py` で撮って `build/ws092-shots/` へ: App Home の tile、開いた文書（日本語を含む）、選択、検索の強調、折り返し on・off、
  行番号、未保存の dialog、Save As の chooser。入力は QMP の key と pointer（`plan/tools/files/qmp-input.py`）。保存した file を SSH で読んで byte を確かめる。
  clipboard は Terminal との間の copy・paste（Terminal の Paste で確かめる）。
- **touch**: host の試験で gesture の列を流す。guest の touch の注入が使えなければ未実施と書き、実機はユーザー。
- 回帰（p003）: 全文の規約（`plan/tools/style-check.py` の新しい file 0）、build の warning 0、他の app に触れていないこと（登録の差分を除く）、boot test。

## 16. 判断が要る点（既定を選んだ）

| # | 点 | 既定 | 理由 |
| --- | --- | --- | --- |
| J1 | 名前 | Text Editor、`/bin/textedit`、ID `textedit` | 他の app と同じ英語の名前 |
| J2 | 本文の font | 等幅（keiland-mono）、日本語は fallback で 2 cell | 桁が揃う。Terminal と同じ |
| J3 | NUL を含む file | 開かない | plain text の editor。binary を壊さない |
| J4 | 改行 | LF と CRLF を検出して保つ（混在は多い方） | Windows の file を壊さない |
| J5 | 最後の改行 | 足さない | 開いた file のまま |
| J6 | 外での変更 | 保存の前に mtime を比べて確かめる | 単純で上書きの事故を防ぐ |
| J7 | tab | 表示は 4 cell ごと、Tab key は tab の文字 | よくある既定 |
| J8 | 自動の字下げ | する（前の行の先頭の空白を写す） | 簡単で役に立つ |
| J9 | 新規 | 同じ window で Untitled | 1 window 1 文書 |
| J10 | touch の選択の handle | 作らない | 範囲を小さく保つ |
| J11 | 検索 | ASCII の大文字小文字を無視、正規表現なし | simple |
| J12 | 行番号・折り返しの既定 | どちらも on | 読みやすさ |
| J13 | tile の色 | 青緑（0x1f9e9a） | 他の app と重ならない |
| J14 | 設定の保存 | しない（行番号・折り返し・文字の大きさは起動ごとに既定） | simple。設定は WS089 の後 |
