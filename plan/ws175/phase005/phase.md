<!-- awesome-plan project=zedbsd record=ws175-p005 -->
# ws175-p005: 置き換えの font（subset と埋め込み、文字ごとの fallback、既存の行の font の変更、文字の挿入）

Parent: [WS175](../ws.md)
Status: cleared（2026-10-06 Q1 判定: p005a・p005b の host の試験 PASS。QEMU は p010。Notes の font・size の UI は p008 の文字の段）
Disposition: normal
Queue: Q1 の順（2026-10-06「p004 の後に p005」）
依存: [p004](../phase004/phase.md)（cleared）

## D2 の font の更新（2026-10-06 Q1、ws.md の末尾から写す）

D2（Inter・JetBrains Mono・Droid Sans Fallback）の後に、ユーザーが UI の font を Mahora に替え、fallback は JetBrains Mono と Droid Sans Fallback の 2 つだけ残し Inter は使わないと決めた（ws090-p020）。p005 の置き換えの font は install される `keiland.ttf`（Mahora Regular）・`keiland-bold.ttf`（Mahora Bold）・`keiland-mono.ttf`（Mahora Mono）、無い字は `keiland-fallback-mono.ttf`（JetBrains Mono）→ `keiland-fallback.ttf`（Droid）。Mahora は Zlib（ユーザーの著作）で subset の埋め込みは問題ない。

## p004 で決めた計画の変更（2026-10-06 Q1 了解）

行の内容を置き換えの font で書く時、block を `ET q BT … ET Q BT` で割らず、`/KeiFn size Tf <Tm> <CID の列> Tj … /<元の名前> <元の size> Tf` と block の中で書く。

## p005a の実装（2026-10-06 P2）

- **subset**（新規 `subset.c`）: `pdf_truetype_subset`。glyph の番号を保つ subset（fontTools の retain-gids と同じ）: 使った glyph と composite の部品（深さ 8）だけ
  輪郭を残し、他は空。loca は long にし、hmtx はそのまま（どちらも圧縮で小さくなる）。残す table は head（checkSumAdjustment を計算し直す）・hhea・maxp・loca・
  glyf・hmtx・cvt・fpgm・prep・name（著作権・license の record を含む）・OS/2・post（format 3）。cmap・GSUB・GPOS・GDEF・kern・vhea・vmtx・variable の table は落とす。
  **design.md §4.3 からの変更**: glyph を詰めて番号を付け直さない。content の CID が元の glyph 番号（CIDToGIDMap /Identity）のままで済み、subset を
  保存の最後に作っても content を書き直さなくてよいため。`pdf_truetype_permission`（fsType: 0x0002・0x0200 は埋め込まない、0x0100 は全体）、`pdf_truetype_metrics`・
  `pdf_truetype_advance`。
- **置き換えの font**（新規 `replace.c`）: `keiland.ttf`（Sans、Mahora Regular）・`keiland-mono.ttf`（Mono）・`keiland-fallback-mono.ttf`（JetBrains Mono）・
  `keiland-fallback.ttf`（CJK、Droid Sans Fallback）を `PDF_FONT_DIRECTORY` から読む（host 試験は `PDF_EDIT_FONT_DIRECTORY` で別の folder）。文書が持つ
  （`pdf_reader_set_edit_fonts`、文書を閉じる時に font cache の後で解放）: file の bytes、libtruetype の face（文字の glyph と advance）、preview の Type0 font
  （file 全体の FontFile2、Identity-H、CIDToGIDMap /Identity、`/DW -1` で face の幅）を文書の寿命の arena に（[H5]）。truetype_open_companions は使わない
  （PDF に埋め込むには file ごとの glyph が要るため、fallback の連鎖は libpdf が自分で辿る）。
- **埋め込み**（新規 `embed.c`、`writer.h`・`writer.c`・`update.c`）: `pdf_writer_use_glyph`（font の file を writer が自分で読み、使った glyph と最初の文字を記録）。
  layout で font ごとに 5 つの object（Type0、CIDFontType2 と使った glyph の /W、FontDescriptor（head・hhea・OS/2・post から）、FontFile2（subset を圧縮、
  /Length1）、ToUnicode（bfchar、U+FFFF を超える字は surrogate、圧縮））。名前は `<接頭辞>F<file>`、subset の tag は使った glyph の hash の 6 文字。新しい文書の
  resources と update の merge した resources に `/Font`（update は `write_entries_but` で Font も除いて merge、名前の衝突は EEXIST）。
- **editor**（`editor.c`・`pdf.h`）: `pdf_edit_text` に font_size・色・box_width（p005b の挿入用。古い大きさの呼び手も受ける）、result に `REPLACED`・`MISSING`。
  `set_text` は元の font を求めて書ければ元の font、書けなければ（埋め込みでない・無い字・TEXT_FIXED・他の font を求めた）置き換えの font: 求めた font
  （ORIGINAL なら Sans）、無い字は JetBrains Mono、次に Droid、どれにも無い字は落として MISSING。行の元の font の resource の名前（走査で記録、
  `font_resource`）で元の font に戻す。名前が長すぎる行と、置き換えの font が 1 つも無い時は ENOTSUP・NEEDS_FONT。preview の resources に
  `ZedPreviewF<file>`、writer は `pdf_writer_begin_page_edited` で glyph を登録して `<接頭辞>F<file>`。
- 走査（`content.c`）: graphics state と show に Tf の名前（`font_resource`、63 byte まで）。

## 試験と結果（host、2026-10-06）

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws175/tests/run-host-edit-scan.sh <scratch>`（置き換えの font の folder を作る。host-edit-text-change に "Arial"（Helvetica の行）・日本語（WinAnsi の行）を置き換えの font で書き、開き直して ToUnicode から読めること。新規 `host-truetype-subset.c`・`check-subset.py`（fontTools）で DejaVu（composite）・Droid・JetBrains Mono・Mahora の subset） | 9 本 ×3 PASS、subset 4 つとも 0 failures |
| pdffonts（保存した file） | `ZICCXX+Mahora-Regular`・`NMLGHM+DroidSansFallback` が CID TrueType・Identity-H・emb・sub・uni |
| pdftotext・pdftoppm（保存した file） | Changed!・日本・Marker・Namer・Arial が読め、目で描画を確かめた |
| `run-host-notes-edit.sh`・ws079 `run-notes-host.sh` | PASS・ok |
| build: zedBSD の `libpdf.so`・`bin/notes`、keiland-linux の両方 | warning 0 |
| style-check（libpdf 全 .c、ws175 の試験） | 0 |

## p005b の実装（2026-10-06 P2）

- `pdf_page_editor_insert_text(editor, text, placement, &index, &result)`（新しい公開 API、exports）: 求めた置き換えの font（ORIGINAL は Sans）で、
  font_size の大きさ、red・green・blue の色、box_width で折り返し（0 は折り返さない）、改行（`\n`）で次の行。placement は box の空間（pt、左上が原点、
  y が下）→ shown space。折り返しは行の最後の空白の後（その空白は描かない）、空白の無い行は収まらない glyph の前。行の高さは大きさの 1.2 倍、
  baseline は行の上から大きさの 0.8。box の高さは行の数。物の kind は TEXT（INSERTED）、四辺形は box、移動・大きさは画像と同じく place で。
- content: 挿入した物の後（元の content の Q の後）に `q r g b rg BT <各 run の Tm> /<接頭辞>F<file> size Tf <CID の列> Tj … ET Q`。blank の editor の
  `pdf_writer_draw_page_editor` も同じ（shown space で）、glyph を writer に登録する。挿入した文字に `set_image` は ENOTSUP。
- **Notes の model**: `notes_edit` に `text_size`・`color`（0xRRGGBBAA）・`box_width`。`NOTES_EDIT_INSERTED|TEXT` は画像なしで受ける（大きさが要る）。
  EDIT と journal の符号化に大きさ（1/64 pt）・色・幅。画像の id は IMAGE の edit と文字でない挿入の物だけが要る。editor に `insert_text` で適用。

## 試験と結果（p005b、host、2026-10-06）

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws175/tests/run-host-edit-scan.sh <scratch>`（新規 `host-edit-insert-text.c` 14 項目: Sans 10pt 赤 50pt で折り返す文字、CJK の日本語と改行、大きさ 0 は EINVAL、preview、update の保存と開き直しで "Hello"・日本語・"second" が行として読める、blank の page に Mono の文字を挿入して新しい文書に保存し読める） | 10 本 ×3 PASS、pdftotext で挿入した語が読める、qpdf --check は試料の page 3 だけ。pdftoppm で描画を目で確かめた |
| `sh plan/ws175/tests/run-host-notes-edit.sh <scratch>`（置き換えの font の folder を使う。文字の挿入の model の 3 項目、日本語は置き換えの font で書けるに変更） | notes-edit 44/44・picture 12/12 ×3 |
| ws079 `run-notes-host.sh` | ok |
| build: zedBSD・keiland-linux の libpdf.so と bin/notes | warning 0 |
| style-check（libpdf・notes の全 .c、ws175 の試験） | 0 |

## 残り

- QEMU は p010。UI（Text の道具・編集の box・IME・font の picker・Font と Size のボタン）は p008 の文字の段。
- 太字（`keiland-bold.ttf`）は D3 のとおり出さない（Q1 の list に在るが、選ぶ UI が無い）。kerning・合字・shaping は範囲外（§9）。
