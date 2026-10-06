<!-- awesome-plan project=zedbsd record=ws175-p007 -->
# ws175-p007: Notes の model（物の編集・画像・undo・ZNOT 2.0・journal・保存と開く時の照合）

Parent: [WS175](../ws.md)
Status: in-progress（2026-10-06 P2: 画像の段を実装、host 試験 PASS。Q1 の判定待ち）
Disposition: normal
Queue: Q1 の順（2026-10-06「p007（Notes の model）を先に」、D6 (b) 画像を先に）
依存: [p006](../phase006/phase.md)（cleared）、[p003](../phase003/phase.md)（cleared）。文字の部分は p004・p005 の後

## 範囲（design.md §10 の p007、画像の段）

物の状態・key・undo（z の位置・参照の数）・Reset・journal（画像の横の file [H4][N3]、版 [N10]、回復の照合 [N8]）・ZNOT major 2（EDIT・IMAG）・
保存（update と全体、OVER だけ PLACE_EDIT [N2]）・開く時の照合と新しい base [H3][N1][N12]・画像の読み戻し。文字（TEXT の状態・font）は p004・p005 の後に足す。

## 実装（2026-10-06 P2）

- **libpdf**（`editor.c`・`pdf.h`・exports）: `pdf_page_editor_blank(width, height, &editor)`（空の 1 page の PDF を memory に作って開き、editor が持つ。
  Notes 自身の page（NEW・REPLACE）に挿入した画像用、design.md [N2]）と `pdf_writer_draw_page_editor(writer, editor)`（blank の editor の挿入した画像を
  writer の開いた page の shown space に描く。画像は `pdf_writer_begin_page_edited` と同じく id で共有）。blank でない editor は EINVAL。
- **model**（`userland/desktop/notes/notes.h`、新規 `edit.c`）:
  - `struct notes_image`（id は stroke と同じ `next_id` から、参照の数、形 JPEG・PNG・ROWS（読み戻した PNG の行）・RGBA（zlib で圧縮、[N3]）、
    向き）。`notes_image_create`・`release`・`source`（editor に渡す形。RGBA は展開）・`set_bytes`（PDF から読み戻した bytes）。
  - `struct notes_edit`（元の物の key か挿入した物の id、flags DELETED・PLACED・IMAGE・INSERTED（0x10 以上は文字に予約）、変換（前 4 つは 1/65536、
    後 2 つは 1/64 pt に量子化）、画像の参照）。`notes_page.edits`（挿入した物は描く順）と `editor`（edits から作る cache、変更で作り直し）。
  - 原始の変更 `notes_document_put_edit`・`take_edit`（journal に記録してから適用）。利用者の変更 `notes_document_edit_object`（元の物は OVER の
    page だけ、挿入した物は画像が要る）・`notes_document_reset_object`（Reset・挿入した物の削除）は undo の entry `NOTES_UNDO_EDIT_OBJECT`
    （前と後の状態の複製と位置、[L7] 画像は参照の数で共有、履歴から落ちた entry が参照を放す）。
  - `notes_page_editor`（OVER は base の page、それ以外は blank。key が無ければ ESTALE）・`notes_page_object`（editor の物の index → 状態）・
    `notes_document_check_edits`（回復の照合 [N8]）。
- **ZNOT 2.0**（`encode.c`）: 編集が在る文書だけ major 2（無ければ今の 1.1 のまま）。TOOL の後に `IMAG`（id・形・幅・高さ・成分・向き、bytes は
  PDF の XObject）、page の PAGE・SRC の後に `EDIT`。読み手は major 1 と 2 を読む（古い Notes は major 2 を EINVAL にして他の PDF として開く、[L1]）。
  `notes_encode_edit`・`decode_edit`・`encode_image`・`decode_image` を journal と共有。
- **保存**（`save.c`）: OVER の page に編集があれば `pdf_writer_begin_page_edited`（PLACE_EDIT）、NEW・REPLACE の page の挿入した画像は
  `pdf_writer_draw_page_editor` で stroke の下に（content は無圧縮のまま hash される [N11]）。全体の書き出しも同じ。
- **開く時**（`save.c`）: Notes の文書（annotated・notes）は編集の元の物の key を base の page で照合し、画像を file の page から
  `pdf_page_editor_read_image` で読み戻す。合わなければ file 全体を新しい base として開く `NOTES_OPENED_REBASED`（全 page が背景、[H3][N1][N12]、
  main.c の status「The edits no longer match this PDF; it opened as it looks」、log `kind=rebased`）。CHANGED の開き方で REPLACE の page を残すのは、
  その page の画像が全て読み戻せた時だけ（[N1]）。
- **journal**（`journal.c`、版 2 [N10]）: 記録 `PUT_EDIT`・`TAKE_EDIT`・`IMAGE`。画像の bytes は journal の横の `<journal>.images/<id>-<SHA-256 の先頭 8 byte>`
  に 1 回（同じ名前が有れば書かない）、記録には説明と SHA-256。snapshot の後にその画像の記録を書くので、保存した file の画像も journal が持つ [H4]。
  回復は bytes を SHA-256 で確かめる。discard（保存）は横の file も消す。`notes_journal_set_aside`（`.kept` に改名）と main.c の回復の照合
  （合わなければ `NOTES RECOVER failed reason=key`、journal を残して file を開く [N8]）。
- **build**: Notes の 3 つの Makefile に `edit.c` と libz-compat、`platform/amd64/vmunix.mk` の Notes の link の規則に `libz-compat.so`（共有の build の
  規則の変更、Q1 に報告）。ws079 の `run-notes-host.sh`・`notes-perf.sh` の host build に `edit.c` と libpdf の全体（libjpeg-compat・libtruetype）。

## 試験と結果（host、2026-10-06）

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws175/tests/run-host-notes-edit.sh <scratch>`（新規 `host-notes-edit.c` 33 項目、plain・ASan・UBSan、qpdf --check） | 33/33 ×3、qpdf は試料の読めない stream だけ、PASS |
| `sh plan/ws175/tests/run-host-edit-scan.sh <scratch>`（`host-edit-blank.c` 新規 10 項目を追加） | 8 本とも PASS ×3 |
| `sh plan/ws079/tests/run-notes-host.sh` | ok |
| build: zedBSD の `bin/notes`・`libpdf.so`（config-amd64-zdesktop）、keiland-linux の `bin/notes` | warning 0 |
| style-check（notes の全 .c、libpdf の変えた file、試験） | 0 |

host-notes-edit の内容: 他の PDF として開き、page 1 の画像の移動・form に JPEG（向き 6）・inline image の削除・PNG の挿入、新しい page に JPEG を挿入（blank）。
新しい page の元の物は EINVAL。undo・redo、Reset と Reset の undo（元の位置に戻る）、編集の無い物の Reset は ENOENT。保存 → 開き直すと annotated・8 page・
同じ編集、JPEG の bytes と向き、PNG は行（ROWS）で読み戻し、2 page の JPEG は 1 つの画像、各 page の画像の数。保存した ZNOT は 2.0。保存の後の変更で
journal（snapshot・画像 2 つ・変更）→ 回復した文書の編集が同じ、JPEG の bytes も在る。無い物の編集を journal に書くと回復の照合が ESTALE、`.kept` に退避。
無い物の編集を持つ notebook を開くと rebased（全 page が背景、編集なし）。pdftoppm で保存した page 1・2 を描き、意図した所に画像が在ることを目で確かめた。

## 未実施・残り

- QEMU・実機: 未実施（p010 で T1）。
- [M13] autosave の cache（page ごとの新しい content と圧縮した bytes を世代で cache）と時間の測定（10 頁 200 ms・開く 500 ms）は未実施。今は editor を
  変更ごとに作り直し、保存のたびに編集した page の content を組み立て・圧縮する。[N13] の窓の外の editor を捨てるのは p008（UI が窓を持つ）。
- journal の上限を超えた時に journal を止めて知らせること、参照 0 の画像の横の file をその場で消すこと（今は保存の discard でまとめて消す）は未実施。
- EXIF の向きを読む `kl_picture_exif_orientation`（`picture.c` を Notes の Makefile に [L6][N18]）、libpng-compat で RGBA に解くこと、挿入の既定の置き方
  （§5.3）は画像を選ぶ UI（p008）で行う。
- 文字の状態（TEXT、font・size・色・折り返し）は p004・p005 の後。flags の 0x10 以上と `notes_edit` の後ろに足す（q810 の IME の text box もその上に乗る）。
