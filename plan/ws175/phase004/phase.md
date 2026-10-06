<!-- awesome-plan project=zedbsd record=ws175-p004 -->
# ws175-p004: libpdf の文字の書き換え（移動・削除・元の font での内容の変更）と Notes の文字の model

Parent: [WS175](../ws.md)
Status: cleared（2026-10-06 Q1 判定: host の試験 PASS（edit-scan 9×3、text-change 35/35、notes-edit 41/41・12/12 ×3、ws079 の回帰）。QEMU は p010。Notes の text の UI は p008 の文字の段）
Disposition: normal
Queue: Q1 の順（2026-10-06「p008 の後に p004・p005（文字）」）
依存: [p003](../phase003/phase.md)（cleared）、[p007](../phase007/phase.md)（cleared、Notes の model）

## 範囲（design.md §10 の p004）

文字の書き換え: 正規化 [H1]、移動の Tm [M2]、元の font での内容の変更 [M3]、印付きの内容 [M10]、glyph の判定 [L5]。Q1 の指示で Notes の文字の model も含める。
置き換えの font（subset・Type0 の埋め込み・文字ごとの fallback・挿入）は p005。

## 実装（2026-10-06 P2）

- **走査**（`content.c`・`internal.h`）: text object ごとの BT と ET の位置（`pdf_scan_block`）、text の位置を動かす演算子 Td・TD・T*・Tm（`pdf_scan_move`、TD は
  後の leading）、show の演算子（Tj・TJ・'・"）、BDC の bytes と対の EMC の終わり（`pdf_scan_mark`、BMC も stack で数える）。
- **editor**（`editor.c`）:
  - 行の移動・大きさ（`place`、Tr 3 の見えない行は ENOTSUP [M9]）と削除（`delete`）。行の四辺形も置き方で動く。
  - `pdf_page_editor_set_text(editor, index, const struct pdf_edit_text *, unsigned *result)`（新しい公開 API、exports）: 元の font で書けるか判定する。
    文字ごとに `pdf_font_code`（font.c、新規。ToUnicode の逆引き `pdf_tounicode_reverse`・単純な font の 256 code・composite の TrueType の cmap の
    glyph から CID）で code を引き、その glyph が埋め込みの program に輪郭を持つこと（空白は除く、[L5]）。埋め込みでない font、無い文字、
    TEXT_FIXED の行、元以外の font は ENOTSUP と `PDF_EDIT_TEXT_NEEDS_FONT`（p005 が置き換えの font で書く）。改行は EINVAL、見えない行は EPERM。
  - **新しい content の組み立てを置き換えの表に作り直した**: 画像・図形・空の stack の Q・文字の置き換えを (offset, length, 置き換え) で集め、offset の
    順に書く。変わった行（移動・削除・新しい語・hidden）を含む text object は正規化する: 位置の演算子を消し（TD は `<leading> TL` を残す [H1]）、各 show の
    前にその show の記録した `Tm` を書き、' と " は Tj に（" は `aw Tw ac Tc` を残す）。移動した行は Tm' = Tm × C_rec × S × C_rec⁻¹ [M2]、削除した行は
    show を消し、新しい語の行は最初の show の位置に `Tm <hex> Tj`（元の font、元の state のまま [M3]）、残りの show を消す。
  - [M10][N15] 新しい語・削除の行を囲む BDC の properties に /ActualText があれば、inline の dictionary はその key を除いて書き直し、名前の properties は
    `/Properties` の dictionary を key を除いて inline に複写する（参照を含む dictionary はそのまま）。`pdf_writer_write_dictionary_except`（update.c、新規の internal）。
  - 行の範囲（first, count）は、行の間の空の show も含む連続の範囲にした（p002b の数え方の修正）。
- **Notes の model**（`notes.h`・`edit.c`・`encode.c`・`journal.c`・`document.c`）: `NOTES_EDIT_TEXT`（0x10）と `notes_edit.text`（UTF-8、edit が持つ）・`font`
  （enum pdf_edit_font、0 は行の元の font）。複製・解放・EDIT と journal の符号化（長さ・bytes・font）。editor に `set_text` で適用。
  元の font で書けない語の edit は editor の作り直しで失敗するので、UI（p008 の文字の段）は edit の前に editor で確かめる（未実装、下）。

## p005 の計画の変更（2026-10-06 Q1 了解）

design.md §3.4 の「行の内容を変える時は `ET q BT [/KeiFn size Tf] Tm … Tj ET Q BT` で block を割る」は採らず、block の中で
`/KeiFn size Tf <Tm> <codes> Tj /<元の名前> <元の size> Tf` と書く（後の show のために元の font に戻す）。block を割らないので、BT の中で始まった
印付きの内容と BT…ET が交差する問題（[M10] の後半）が起きず、記録した text state と色を明示する必要も無い。そのために p005 で show ごとに
Tf の名前（resource の名前）を走査で記録する。

## 試験と結果（host、2026-10-06）

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws175/tests/run-host-edit-scan.sh <scratch>`（`make-edit-samples.py` が新しく `edit-text.pdf` を作る。新規 `host-edit-text-change.c` 35 項目、`host-edit-text.c` を p004 の期待に直した） | 9 本とも PASS ×3（plain・ASan・UBSan）。pdftotext で "Changed!"・"Marker"・"Namer" が読め、古い /ActualText（Old・Older）は読まれない。qpdf --check は試料の page 3 だけ |
| `sh plan/ws175/tests/run-host-notes-edit.sh <scratch>`（host-notes-edit に文字の 6 項目） | notes-edit 41/41・picture 12/12 ×3 |
| `sh plan/ws079/tests/run-notes-host.sh`・`run-pdf-update.sh` | ok・ok |
| build: zedBSD の `libpdf.so`・`bin/notes`、keiland-linux の `libpdf.so`・`bin/notes` | warning 0 |
| style-check（libpdf の変えた file、notes の全 .c、試験） | 0 |

host-edit-text-change の内容（edit-text.pdf: DejaVu の単純な TrueType の F1、埋め込みでない Helvetica の F2、Identity-H の F3）: "Line one" を 20 右へ、
"Quote" を始まりの点から 2 倍、"Next"（'）を削除、"Line two" に "Changed!"、Identity-H の行に語を 2 回、"Marked"・"Named"（inline と名前の
/ActualText の BDC の中）に "Marker"・"Namer"。Helvetica と WinAnsi の日本語は NEEDS_FONT、改行は EINVAL、Reset で元の位置。保存して開き直すと
変えた行が意図の所に在り、他の全ての行（TD が決めた leading で T* する後の block の行、" の Tw・Tc の後の行を含む）は元の四辺形のまま [H1]。

## 未実施・残り

- QEMU・実機: 未実施（p010 で T1）。Notes の文字の UI（Text の道具、編集の box、IME、font の picker）は p008 の文字の段。
- UI が edit の前に editor で「元の font で書けるか」を確かめること（model は edit を受けてしまい、editor の作り直しが失敗する）。
- 縦書きの行・Type 3 の行の内容の変更（TEXT_FIXED）は p005 の置き換えの font で「全文の打ち直し」。CFF の composite font の逆引きは未対応（ENOENT → NEEDS_FONT）。
