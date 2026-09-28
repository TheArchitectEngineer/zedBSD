<!-- awesome-plan project=zedbsd record=ws079-p009 -->

# ws079-p009: 全文規約確認と回帰（必須の最終確認）

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-29 main: 残りの pen と touch の guest 試験を main が pen の image で流し、3 本とも PASS。規約の是正と他の回帰は 2 回目の区切りで済んでいた。`input-inject.c`・`input-inject.h`・touchinject の規約は WS081 p002 へ移管）
Disposition: normal
Parent: [WS079](../ws.md)
Queue: main の指示（PDF/Notes subagent、2026-09-28）。Awesome Plan の Queue の item ではない
Resume point: なし（cleared）。移管: `input-inject.c`・`input-inject.h`・touchinject の規約の是正は WS081 p002、その確認は WS081 の最終確認の Phase
<!-- awesome-plan-current:end -->

## 範囲（main の指示、2026-09-28）

WS079 の全 source を `plan/coding-style.md` の全文と照合し、違反を直し、host 試験と guest の確認を流す。

この subagent が直してよいのは libpdf（`userland/base/libpdf/`・`include/libc/pdf.h`）、`userland/desktop/pdfviewer/`、`userland/desktop/notes/`、
WS079 の試験の道具だけ（main の指示）。WS079 の他の source（kernel の HID の digitizer・touch と注入の device、compositor の tablet・touch・
端のジェスチャー、libwayland の protocol、libtruetype の outline、試験の program）は照合して main に報告する。

## 受け入れ条件

1. 直してよい範囲の全 file（libpdf 25・pdf.h・pdfviewer 15・notes 16）で `plan/tools/style-check.py` の違反 0。
2. 同じ範囲で、道具が見ない規則を調べる新しい `plan/ws079/tests/style-extra.py`（下）の指摘が 0、または読んで規約に当たらないと判断したもの。
3. 規則の目視: 道具で拾えない注釈の中身（文を言い直すだけの注釈）を拾い直す。
4. 回帰: amd64 の pdfviewer・notes・libpdf.so・libtruetype.so と pcat・pc98・rpi4 の libpdf.so が warning 0。host 試験 8 本
   （`run-pdf-ccitt`・`run-pdf-text`・`run-pdf-render`・`run-pdf-reader`・`run-pdf-writer`・`run-pdf-update`・`run-notes-host`・`run-pdfviewer-host`）。
   QEMU の Venus guest で `pdf-demo-guest.sh` の全段。
5. 範囲の外の WS079 の file の照合の結果を main に渡す。

## 行ったこと（2026-09-29）

- 道具: `plan/ws079/tests/style-extra.py`（新）は `style-check.py` の見ない規則を調べる: 関数の最後の裸の `return error;`・呼び出しの結果の直接の
  return・初期化子の中の呼び出し（§4・§11）、関数・型・file scope の変数の注釈（§2・§3・§10）、禁じられた注釈（§10）、2 つの NULL の検査を
  1 つの if で（§9）、著作権の見出し（§13）、if と else の括弧の対称・if を本体に持つ loop の括弧・制御文と括弧の無い本体の間の注釈・空行で
  始まる block（§8）。`plan/ws079/tests/style-insert.py`（新）は `style-check.py` の行番号で段落の注釈を差し込む（行の頭で正しい行か確かめる）。
- `style-check.py` の違反 133 → **0**（libpdf 25 file・`pdf.h`・pdfviewer・notes）: blank-after-brace 125（閉じ括弧の後の文に空行と段落の注釈。
  view.c の switch の case で両腕が 1 文の if/else は括弧を外し、腕ごとの注釈を足した）、call-in-condition 4（view.c の矢印の key: 頁が横・縦に
  収まるかを switch の前に変数に）、paragraph-comment 4（`pdf.h` の宣言の群に注釈、見出しの注釈を今の reader に合わせた）。
- `style-extra.py` の指摘 25 → 7: 呼び出しの結果の return 5（`sqrt`・`atan2`・`page_mode_top`・`strerror` を変数に入れてから）、file scope の変数の
  注釈 13（pdfviewer の main.c の各変数、notes の ui.c、生成の `cffdata.c`（生成の `make-cff-tables.py` も）、2 つの `shaders.h`（生成の
  `shaders/regenerate.py` も））。残る 7 は joined-check で、どれも割り当ての検査ではない（引数の検査、roundtrip の後の global の有無、
  「どちらの font も無い」の判定）ので規約に当たらない（§6 の副作用の無い 2 節の条件）。
- 注釈の中身: 文を言い直すだけの短い注釈（「Reads it.」「Runs it.」「Adds it.」「Reports it.」「Multiply multiplies.」「No font.」など 15 か所）を
  何をするかを言う文に直した。3 語以下の注釈 148 のうち残りは case の演算子名（`rmoveto.` など、label の直後の注釈）と段落の対象の名前で、そのまま。
- 評価の順序と振る舞いは変えていない（view.c の `pv_app_content_width()`・`_height()` を switch の前に呼ぶのは副作用の無い計算）。

## 確認（2026-09-29、この worktree）

| 確認 | 結果 |
| --- | --- |
| `python3 plan/tools/style-check.py`（範囲の 57 file） | 違反 0 |
| `python3 plan/ws079/tests/style-extra.py`（同じ） | 7（joined-check、上の理由で規約に当たらない） |
| build: amd64 の pdfviewer・notes・libpdf.so・libtruetype.so、pcat・pc98・rpi4 の libpdf.so | 4 つとも exit 0・warning 0 |
| host 試験 8 本（`run-pdf-ccitt 100`・`run-pdf-text 20`・`run-pdf-render 100`・`run-pdf-reader`・`run-pdf-writer`・`run-pdf-update`・`run-notes-host`・`run-pdfviewer-host`） | 8 本とも ok（数値は p015 と同じ） |
| QEMU の Venus guest（main の image の copy、この worktree の 4 つを SSH で入れ替え）: `pdf-demo-guest.sh … quilt faq refcard programs encrypted notice annotate refuse ccitt password thumbnails` | 全段 ok、zdesktop の ERROR 0。1 回目は `encrypted` と `notice` の期待が p015 の前の振る舞い（password の要る文書の message、gnus-logo の注意）で MISSING になった。p015 の意図した変化なので段を直し（password の card、JBIG2 の `skipped.pdf` の注意）、2 段を流し直して ok。画面: `/home/awe/zedBSD-rpi4/build/ws079-shots/ws079-p009-20260929-*.png`（27 枚） |
| boot test（`plan/tools/boot-test.sh`） | 未実施（kernel と image は変えていない。guest は main の image の copy で起動した） |

## 2 回目の区切り（2026-09-29、main の判断の後）

main の判断（2026-09-29）: p015・p009 の commit は main に merge（384361e5）。範囲の外の file のうち WS079 が作った `userland/desktop/wayland/corner.c`・
`tablet.c`、`userland/desktop/libtruetype/outline.c`、`userland/base/tests/peninject`・`tablet-probe` はこの subagent が直してよい。
**`src/drivers/generic/input-inject.c`・`include/uapi/input-inject.h`・`userland/base/tests/touchinject` は WS081 の subagent が p002 で変更中（Scan Time の宣言）なので
触らず、規約の是正は main が WS081 に割り当てる（WS081 p002 で是正、確認は WS081 の最終確認の Phase へ）。** libpdf の公開 API の追加 2 つは承認。

### 行ったこと

- worktree に `git merge main`（fast-forward で main の 1f5a3aa2 へ、384361e5 を含む）。
- `userland/desktop/libtruetype/outline.c` を規約に合わせて書き直した: 著作権の見出しの前の mode 行を除く、公開の関数を static の前に（`truetype_outline_load()`・
  `truetype_outline_bounds()` を前へ）、前方宣言を 1 行に、宣言を 1 行 1 つに、公開の関数に動詞の注釈、条件演算子 3 つを if に、C99 の複合 literal 2 つを
  変数（`control`）に、呼び出しの結果の直接の return を変数に、禁じられた注釈「Returns the computed result.」を除く、`load_simple()` を
  `read_flags()`・`read_coordinates()`・`trace_contour()` に分けた（x と y の同じ読み方を 1 つの関数に）。到達しない分岐（前の検査で除かれた
  `last < first` の skip）を除いた。
- `corner.c`: 閉じ括弧の後の 3 か所に段落の注釈。`tablet.c`: `tool_slot()` の呼び出しの結果の return を変数に。`peninject/main.c`: 裸の `return status;` 2 つを
  失敗と成功の return に、printf の引数の条件演算子 2 つを変数に。
- §6 の「3 つ以上の節、または `&&` と `||` の混在の条件は節を行に分ける」を、1 行に書かれた条件 35 か所（libpdf・pdfviewer・notes・tablet.c・corner.c・
  tablet-probe）で行に分けた（括弧と字下げで構造を示す、評価の順序は不変）。
- `plan/ws079/tests/truetype-render-dump.c`・`truetype-render-compare.sh`（新）: libtruetype を今の `outline.c` と git の revision の `outline.c` で host に 2 度 build し、
  font の全 glyph を 5 つの大きさで `truetype_render_glyph()` で描いて（結果・箱・送り・bitmap の hash）比べる。

### 確認（2026-09-29）

| 確認 | 結果 |
| --- | --- |
| `style-check.py`（範囲の 57 file と、corner.c・tablet.c・touch.c・libtruetype の outline.c・peninject・tablet-probe） | 違反 0 |
| `style-extra.py`（同じ） | 7（joined-check。割り当ての検査ではない 2 節の NULL の検査で規約に当たらない） |
| `truetype-render-compare.sh 1f5a3aa2`（DejaVu Sans・Serif・Sans Mono Bold、Noto Sans Balinese（変換つきの合成）、Liberation Serif、大きさ 9・13・16・24・40） | 5 font の 81,900 の描画が前の `outline.c` と全て同じ（うち 80,300 が描けた glyph）、ASan+UBSan の報告なし |
| `truetype-outline-test.sh`（p007 の outline の API と fontTools） | 4 font とも 0 differ |
| host 8 本（`run-pdf-ccitt 100`・`run-pdf-text 20`・`run-pdf-render 100`・`run-pdf-reader`・`run-pdf-writer`・`run-pdf-update`・`run-notes-host`・`run-pdfviewer-host`） | 8 本とも ok（Notes・PDF Viewer の文字の描画を含む） |
| build | amd64 の wayland・wltest・pdfviewer・notes・libpdf.so・libtruetype.so、pen の config の peninject・tablet-probe、pcat・pc98・rpi4 の libpdf.so: 全て exit 0・warning 0 |
| QEMU の zdesktop（main の `ws035-sq` の image の copy、この worktree の compositor と wltest を入れる） | `zdesktop-p010.sh`（上の右端のスワイプ、端のジェスチャー）: **p010: PASS**。`zdesktop-p013.sh`（mouse の三回 click、double click の docking）: **p013: PASS**。画面は `/home/awe/zedBSD-rpi4/build/ws079-shots/ws079-p009-20260929-zdesktop-p01{0,3}-*.png` |
| QEMU の PDF の demo（同じ image、最終の pdfviewer・notes・libpdf.so・libtruetype.so） | `pdf-demo-guest.sh` の全 13 段 ok、MISSING 0、zdesktop の ERROR 0（`ws079-p009b-20260929-*.png`） |
| boot test（`plan/tools/boot-test.sh build/ws079-p015-run/hdd-image.img`） | PASS（login prompt、`ws079-p009-20260929-boot-login.png`）。ただしこの image は main の image の copy で、この区切りの変更（userland だけ）を含まない |
| pen と touch の guest 試験（`p003-guest.sh`・`p012-guest.sh`・`zdesktop-p013-touch.sh`） | **未実施**: 注入の device（CONFIG_INPUT_TEST_INJECT）を持つ pen の image が要り、`build-pen-image.sh` が Noct の archive の取得と source の展開・build を要した（`build/NoctLang` を main への読み取りの symlink にしても、disk image の規則が worktree の `userland/base/noct/noct` を作ろうとする）。toolchain の規則で止め、途中で取得された archive（worktree の `userland/base/noct/distfiles/`、git の外）と symlink は消した。tablet.c・tablet-probe・peninject の変更は条件の行の分割・変数への代入・return の分割で、振る舞いは変えていない（build は warning 0） |

## 範囲の外の WS079 の source（1 回目の照合）

`style-check.py`（34）と `style-extra.py`（15）の指摘:

| file | 指摘 |
| --- | --- |
| `src/drivers/generic/input-inject.c`（p002、**WS081 p002 で是正、main の割り当て**） | blank-after-brace 4、call-in-condition 2（`cred_is_superuser(cred_current())`、`inject_event_valid`）、conditional 1、goto 2（`goto fail`、単一の後始末なら規約内）、multi-line-body 1（`return inject_key_valid(...) &&` の Boolean の式）、裸の `return error;` 1、呼び出しの結果の return 2 |
| `include/uapi/input-inject.h`（**WS081 p002 で是正、main の割り当て**） | paragraph-comment 1 |
| `userland/desktop/wayland/corner.c`（p010） | blank-after-brace 3 |
| `userland/desktop/wayland/tablet.c`（p003） | 呼び出しの結果の return 1、joined-check 1（規約に当たらない見込み） |
| `userland/desktop/libtruetype/outline.c`（p007 の outline の API） | blank-after-brace 9、paragraph-comment 3、forward-declaration 3、conditional 1、著作権の見出しの前の mode 行、公開の関数の注釈 2、禁じられた注釈「Returns the computed result.」、呼び出しの結果の return 2 |
| `userland/base/tests/peninject/main.c`（2 回目の区切りで是正）・`touchinject/main.c`（**WS081 p002 で是正、main の割り当て**）（p002・p012 の試験の program） | conditional 4（printf の引数）、裸の `return status;` 3 |
| `userland/base/tests/tablet-probe/main.c` | joined-check 1 |
| `src/drivers/usb/hid-digitizer.c`・`hid-touch.c`・`include/drivers/usb/hid-*.h`・`userland/desktop/wayland/touch.c`・`touch.h` | 指摘なし |

## 未実施と制限

- 規約の目視は道具の拾う規則と、短い注釈・vague な注釈の拾い直し、notes/save.c などの一部の読み直しまで。57 file・約 5 万行の全行の目視はしていない。
- 1 回目の区切りの範囲の外の file は、2 回目の区切りで main の許した 5 つを直した。`input-inject.c`・`input-inject.h`・touchinject は WS081 p002（main の割り当て）。
- 実機: 未実施。

## main の最後の確認（2026-09-29）

main の checkout（d809ea1b、この Phase の 2 回目の区切りを merge 済み）で pen の試験の image を作り、guest の試験を流した。QEMU の証拠だけ。実機は未実施。

| 試験 | 結果 | log |
| --- | --- | --- |
| `plan/ws079/tests/build-pen-image.sh build/main-pen`（Noct の build を含む。共有の toolchain の tree は lock のまま） | ok | `build/main-pen-image.log` |
| `p003-guest.sh build/main-pen-p003`（pen: tablet・pointer・home・terminal・corner） | status=0 | `build/main-pen-p003.log` |
| `p012-guest.sh build/main-pen-p012`（multitouch） | PASS | `build/main-pen-p012.log` |
| `zdesktop-p013-touch.sh build/main-pen`（compositor の touch） | PASS | `build/main-pen-p013t.log` |

3 本の log に MISSING・FAIL は 0。
