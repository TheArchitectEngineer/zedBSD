<!-- awesome-plan project=zedbsd record=ws079-p009 -->

# ws079-p009: 全文規約確認と回帰（必須の最終確認）

<!-- awesome-plan-current:start -->
Status: uncleared（2026-09-29、PDF/Notes subagent。直してよい範囲（libpdf・pdf.h・pdfviewer・notes・試験の道具）は規約の道具の違反 0 と回帰（host 8 本・4 platform の build・QEMU の Venus の全段）まで済んだ。WS079 の範囲の外の source（kernel の注入の device・compositor の corner.c・libtruetype の outline.c・試験の program）に違反が残り、main の判断が要る）
Disposition: normal
Parent: [WS079](../ws.md)
Queue: main の指示（PDF/Notes subagent、2026-09-28）。Awesome Plan の Queue の item ではない
Resume point: main が範囲の外の file（下の表）を直す許可か割り当てをしたら、それを直し、`style-check.py`・`style-extra.py` と該当の回帰（kernel は build と boot test、compositor は zdesktop の試験）を流して cleared にする
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

## 範囲の外の WS079 の source（main へ、直していない）

`style-check.py`（34）と `style-extra.py`（15）の指摘:

| file | 指摘 |
| --- | --- |
| `src/drivers/generic/input-inject.c`（p002） | blank-after-brace 4、call-in-condition 2（`cred_is_superuser(cred_current())`、`inject_event_valid`）、conditional 1、goto 2（`goto fail`、単一の後始末なら規約内）、multi-line-body 1（`return inject_key_valid(...) &&` の Boolean の式）、裸の `return error;` 1、呼び出しの結果の return 2 |
| `include/uapi/input-inject.h` | paragraph-comment 1 |
| `userland/desktop/wayland/corner.c`（p010） | blank-after-brace 3 |
| `userland/desktop/wayland/tablet.c`（p003） | 呼び出しの結果の return 1、joined-check 1（規約に当たらない見込み） |
| `userland/desktop/libtruetype/outline.c`（p007 の outline の API） | blank-after-brace 9、paragraph-comment 3、forward-declaration 3、conditional 1、著作権の見出しの前の mode 行、公開の関数の注釈 2、禁じられた注釈「Returns the computed result.」、呼び出しの結果の return 2 |
| `userland/base/tests/peninject/main.c`・`touchinject/main.c`（p002・p012 の試験の program） | conditional 4（printf の引数）、裸の `return status;` 3 |
| `userland/base/tests/tablet-probe/main.c` | joined-check 1 |
| `src/drivers/usb/hid-digitizer.c`・`hid-touch.c`・`include/drivers/usb/hid-*.h`・`userland/desktop/wayland/touch.c`・`touch.h` | 指摘なし |

## 未実施と制限

- 規約の目視は道具の拾う規則と、短い注釈・vague な注釈の拾い直し、notes/save.c などの一部の読み直しまで。57 file・約 5 万行の全行の目視はしていない。
- 範囲の外の上の file は直していない（main の判断待ち）。
- 実機: 未実施。
