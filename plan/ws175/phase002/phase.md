<!-- awesome-plan project=zedbsd record=ws175-p002 -->
# ws175-p002: libpdf の走査（画像と図形の物の表）と、後の段の文字の走査・抽出

Parent: [WS175](../ws.md)
Status: in-progress（2026-10-06 q805 P2、p002a から）
Disposition: normal
Queue: q805（Q1、2026-10-06「ベータ1・2 の未実装を WS の順に」）
依存: [p001](../phase001/phase.md)（cleared、design.md が正本）

## 分け方（2026-10-06 P2、D6 (b)「画像を先に」に合わせる）

design.md §10 の p002 の「先の段」の部分を p002a、残りを p002b にする。

| 部分 | 内容 | 段 |
| --- | --- | --- |
| p002a | interpreter の走査の mode と、画像（Image XObject の `Do`・inline image）と図形（Form XObject の `Do`）の top level の物の表: key（種類・位置・長さ・指紋）、byte の範囲、C_rec、四辺形、clip の有無、hit、q/Q の釣り合い（開いたままの q の数、空の stack の Q の位置）、BT の中の終わり、q の上限を超えた後の物を出さない [N14]、読めない stream の page は編集しない [M5]。公開の API（`include/libc/pdf.h`・`exports.map`）の editor の開閉・数・物・key・find・hit・status。host 試験（plain・ASan・UBSan）と試料の画像の部分 | 先 |
| p002b | 文字の行の走査（行のまとめ [M9][N16]、Tr 4〜7 の block [H2]、印付きの内容 [M10]、Tr 3）と Unicode の抽出（ToUnicode・encoding・cmap の逆引き、§3.2） | 後 |

## p002a の受け入れ

- `pdf_page_editor_open()` が page の top level の画像・図形を content の順に返し、各物の四辺形が libpdf の render の画像の置き方と一致する（試料で確かめる）。
- key が物の最初の token の位置・長さ・指紋で、同じ文書を開き直すと同じ key、`find` で同じ物に当たる。
- form の中の画像・Type 3 の glyph の中は一覧に出ない。q の上限を超えた後の物は出ず status が PARTIAL。読めない stream を含む page は status が SKIPPED（編集しない）。
- hit は上（後）の物から。
- 記録: 余る Q の位置、開いたままの q の数、BT の中で終わること（p003 の組み立てが使う）。
- host 試験が plain・ASan・UBSan で PASS、build（zedBSD・Linux・FreeBSD の Makefile に editor.c）の warning 0、style-check 0。

## p002a の実装（2026-10-06 q805 P2）

- `content.c`: `pdf_page_render` の中身を `page_run`（scan の有り無し）に分けた。`pdf_content_scan`（internal）は page を描く時と同じ run に scan を付け、page の top level（`content_level == 1`、form・Type 3 の glyph の外）の `Do`（Image・Form）と inline image を、最初の operand から operator の終わり（inline は BI から EI）の byte の範囲、C_rec、四辺形（画像は unit square、form は BBox を Matrix と CTM で）、sample の幅と高さ、clip の有無、範囲の SHA-256 の先頭 8 byte で記録する。q/Q は token で数え、空の stack の Q の位置、開いたままの q の数、BT の中の終わりを記録する。q の上限を超えた後（`ignored_saves`）の物は出さず `partial`、run が DAMAGED・LIMITED で止まった時も `partial`。stream を読んだだけの flags（`read_flags`）は run の前に取る（[M5]）。
- `editor.c`（新規）: `pdf_page_editor_open`・`close`・`status`・`count`・`object`・`key`・`find`・`hit`。status は `PDF_EDIT_PAGE_SKIPPED`・`LIMITED`・`DAMAGED`（読めない stream、`PDF_EDIT_PAGE_READ_ONLY`）と `PARTIAL`。hit は後に描いた物から、凸の四辺形の中（面積の無い物は外）。
- `include/libc/pdf.h`・`exports.map`: 上の API と `enum pdf_edit_kind`・`struct pdf_edit_key`・`struct pdf_edit_object`（先頭の `size`）。`internal.h` に `struct pdf_scan_object`・`struct pdf_scan`。Makefile（zedBSD・Linux・FreeBSD）に editor.c。
- 試験: `plan/ws175/tests/make-edit-samples.py`（edit-images.pdf、4 頁）、`host-edit-scan.c`、`run-host-edit-scan.sh` → plain・ASan・UBSan とも 40 passed, 0 failed（3 つの物の bytes・四辺形・kind・sample、余る Q と開いた q、key と find と開き直し、hit、render の画像の置き方と四辺形の一致、/Rotate 90、読めない stream で READ_ONLY、深い q の後の物が出ず PARTIAL）。
- build: zedBSD の libpdf.so（新しい 8 つの symbol を export）、keiland-linux の warning 0、host の C89 pedantic の -Werror。style-check 0（content.c・editor.c・internal.h・pdf.h）。FreeBSD の build は未実施（Makefile.freebsd に足しただけ）。QEMU は不要（library だけ、Notes からはまだ使わない）。

## p002b の途中（2026-10-06 q805 P2、p006 の許可待ちの間に）

- `tounicode.c`（新規、internal）: font の /ToUnicode CMap を読む。`beginbfchar`・`beginbfrange`（1 つの文字列で最後の単位を数え上げる形と、文字列の配列の形）、UTF-16BE の宛先（surrogate の対は 1 文字、対の無い surrogate は U+FFFD、合字は複数の文字）、元の code の byte 長を区別、後の entry が前に勝つ。`usecmap` は読まない。entry は 65536 まで、1 code の文字は 8 まで、壊れた section はそこで終わり、それまでの分は残す。Makefile 3 つに足した。
- 試験: `plan/ws175/tests/host-tounicode.c`（13 項目）を `run-host-edit-scan.sh` に足した → plain・ASan・UBSan とも PASS。
- `font.c` の `pdf_font_unicode()`（internal、§3.2 の順）: /ToUnicode（初回に読む）→ 単純な font の encoding と Differences（`load_simple` の表を font に残す）→ composite の埋め込みの TrueType の cmap の逆引き（BMP を 1 回だけ引いて glyph → 文字の表、CIDToGIDMap を通す）→ U+FFFD。`free_font` で解放。font.c が tounicode.c を使うので、ws079 の host の 5 つの script（render・text・ccitt・update・pdfviewer-host）の source の並びに tounicode.c を足した。
- 試験: `host-font-unicode.c`（10 項目: WinAnsi の A と €、ToUnicode の Z と下の encoding の B、DejaVu Sans の Identity-H で A の glyph → A、glyph 0 → U+FFFD）。`make-edit-samples.py` に page 6（3 つの font、F3 は host の DejaVu Sans を丸ごと FontFile2 に）。run-host-edit-scan の 5 つの試験が plain・ASan・UBSan とも PASS。
- 残り（p002b）: 文字の行の走査（行のまとめ [M9][N16]、Tr 4〜7 の block [H2]、印付きの内容 [M10]、Tr 3）と editor の TEXT の物。
