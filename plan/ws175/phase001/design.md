# ws175-p001 設計: Notes で PDF の画像と文字を編集する

2026-10-06 P2（ws175-p001、設計だけ、product の code は書かない）。由来はユーザー（2026-10-06）の依頼（[WS175](../ws.md)）。
design-reviewer の review（2026-10-06、high 6・medium 16・low 11、[phase.md](phase.md) の「review」の表）と、変えた節の再 review（N1〜N19）は
2026-10-06 にこの文書へ反映した（各所の `[H1]`・`[M3]`・`[N4]` などの印が表の行に当たる）。ユーザーの判断 D1〜D7（§12）は推奨のまま。
この文書は WS175 の実装の Phase（p002 以降）の正本。WS079 の [design-pdf.md](../../ws079/design-pdf.md)（Notes の PDF・ZNOT・増分の更新）を前提にし、そこに書いてあることは繰り返さない。

## 0. 要約

- **保存は今の Notes と同じ「base の bytes はそのまま、Notes の revision を 1 つ足し、保存のたびにその revision を作り直す」増分の更新**にする。
  編集した page だけ `/Contents` と `/Resources` を新しい版にする。削除した物の bytes は base の revision に残るので、残したくない人のために
  「古い版を含めない copy の保存」（全体の書き直し）を別の Phase の選択肢にする（D1）。
- 編集の単位は **page の top level の content の「物」**: 文字の行（同じ BT…ET の中の、同じ baseline の show の演算子の集まり）、画像
  （Image XObject の `Do`、inline image `BI…EI`）、form XObject の `Do`（「図形」: 移動・大きさ・削除だけ）。form の中・path（線や塗り）は編集しない。
- content stream は **libpdf が解いて書き直す**。物の byte の範囲と、その時点の graphics state（CTM・text matrix・font・色）を interpreter が記録し、
  編集した物だけを置き換えた 1 本の新しい content stream を作る。文字の block は触った時に「各 show の前に明示の `Tm`」の形に正規化してから
  一部を消す・差し替える（後ろの文字の位置が変わらない。落とす `TD` の leading は `TL` で保つ）。clip の文字（Tr 4〜7）を含む block、
  読めない stream を含む page は編集しない。
- 文字の書き換えは、**元の埋め込み font に要る glyph が全て有ればその font のまま**、無ければ **system の font（Inter・JetBrains Mono・
  Droid Sans Fallback）に置き換え**、利用者に知らせる。新しい font は Type0/CIDFontType2/Identity-H、glyph を詰めた subset、`/ToUnicode` つきで埋め込む。
  3 つの font とも埋め込みは license 上 可（§4.4）。
- 挿入・差し替えの画像は **PNG と JPEG**（CMYK の JPEG は v1 では受けない）。JPEG は bytes のまま（DCTDecode）、PNG は RGB/Gray で alpha が無ければ
  IDAT をそのまま（FlateDecode と PNG の predictor）、それ以外は解いて RGB＋SMask。圧縮のために libz-compat に deflate を足す（D4）。圧縮するのは
  新しい画像・font の stream と編集した base の page の content だけ（pen の stroke の content と ZNOT は今の Notes の読み方のために無圧縮）。
- Notes の model は **page ごとの「物の上書きの表」と「挿入した物の列」**（状態の表で、操作の log ではない）。undo は物の前後の状態の組、
  journal と ZNOT にも同じ符号化で入れる。編集がある文書の ZNOT は major 2（古い Notes は他の PDF として安全に開く）。物の key は種類と
  decode した content の中の byte の位置と長さ＋指紋で、開く時に 1 つでも適用できなければ文書全体を新しい base（編集は焼き込み）として開く。
  journal の snapshot は今の状態が参照する画像の bytes も持つ（保存の後の crash でも画像を失わない）。
- UI: toolbar に **Select（編集）・Text・Image** の道具。選んだ物に枠と四隅の handle、その上に小さな操作の帯（Edit Text・Font・Replace・Delete）。
  文字の編集は page の上でその場の編集の box（caret、IME）。複数頁は今の Notes で対応済み（`<`・`3 / 12`・`>`・PageUp/PageDown・`+ Page`）で、
  各頁の編集は page ごとに持つ。
- 規模の見積もり: **約 17.5 LW（Save Clean Copy を入れて 20 LW）**。WS の Q1 の概算 6 LW より大きい。画像を先に出す場合（D6）は先の段が約 10 LW（§10）。

## 1. 今あるもの（調査の結果）

| 項目 | 今の状態（source） |
| --- | --- |
| Notes の model | `userland/desktop/notes/notes.h`: 文書 = page の列、page = 大きさ・背景・origin（NEW・OVER・REPLACE）・base の page の番号・pen の stroke の列。undo は 4 種（stroke の追加・削除・page の追加・消しゴムの部分）。journal（`journal.c`）が変更ごとに追記 |
| 複数頁 | **対応済み**。他の PDF を開くと各 page が `NOTES_ORIGIN_OVER` の page になり、背景に描かれる。toolbar に `<`・`3 / 12`・`>`・`+ Page`、PageUp/PageDown（`main.c`・`ui.c`）。一度に 1 page を表示 |
| 背景の描画 | `main.c`: base の page を `pdf_page_render()` で display list にし、`pdf_display_list_rasterize()` で CPU で画素にし、Vulkan の texture（`NOTES_TEXTURE_BACKGROUND`）で stroke の下に描く |
| 保存 | `save.c`: Notes が作った文書は全体の書き出し（`pdf_writer_*`）。他の PDF の上の文書は **増分の更新**（`pdf_writer_create_update()`、`keep_page`・`begin_page_over(OVERLAY/REPLACE)`）で base の bytes の後ろに revision を 1 つ足す。保存のたびに base から作り直すので revision は積み上がらない。ZNOT（編集の data）は `kei-notes.bin` の添付。暗号化・署名の PDF は拒む |
| libpdf の writer | `writer.c`・`writer.h`: 塗りの path・不透明度（ExtGState）・画像（JPEG の bytes のまま、または 8bit RGB＋alpha の SMask）・添付。**文字・font の書き出しは無い**。**圧縮（deflate）は無い**（stream は無圧縮） |
| libpdf の update | `update.c`: 各 page を keep・overlay・replace・new で並べ、page の新しい版、page tree、catalog の添付、xref と `/Prev`。resource は接頭辞 `Kei` で、ExtGState と XObject を page の物と merge（同じ名が有れば EEXIST）。**Font の category の merge は無い**。page の object を読んだとおりに書き戻す `write_object()` が有る |
| libpdf の reader・interpreter | `content.c`: q/Q・cm・path・clip・色・ExtGState・text（BT/ET、Td TD Tm T*、Tj TJ ' "、Tc Tw Tz TL Tf Tr Ts）・Image/Form XObject の `Do`・inline image・shading。font は embedded の TrueType・CFF・Type1・Type3、Type0 は Identity-H/V だけ（UniJIS などの予約の CMap は読まない）、非埋め込みは system の font で代用（`/usr/share/fonts/keiland*.ttf`）。display list には**文字が残らない**（glyph の輪郭の fill になる） |
| 文字の抽出（Unicode） | **無い**。`/ToUnicode` を読む code が libpdf に無い。PDF Viewer の検索・選択（[ws128-p004](../../ws128/phase004/phase.md)）は planning のまま（libpdf の抽出待ち）。encoding.c に単純 font の encoding と glyph 名→Unicode の表は有る |
| font の file | `userland/desktop/fonts/`: `Inter.ttf`（→ `/usr/share/fonts/keiland.ttf`、**variable font**: fvar の opsz 14–32・wght 100–900、既定 400、glyph 2933、fsType 0、OFL 1.1、RFN なし）、`JetBrainsMono-Regular.ttf`（`keiland-mono.ttf`、fsType 0、OFL 1.1、RFN なし）、`DroidSansFallbackFull.ttf`（`keiland-fallback.ttf`、glyph 49382・約 4 MB、**fsType 8 = Editable embedding**、Apache-2.0）。serif の font は入っていない。libtruetype は gvar を適用しない（Inter は既定の instance = Regular で描かれる） |
| 画像の decode | `libpng-compat`（全ての色の型・bit 深さ・Adam7）、`libjpeg-compat`、`picture/`（EXIF の向き）。`libz-compat` は inflate だけ |
| 入力・UI の部品 | Notes は自前の Vulkan と toolbar（`ui.c`）。libkeiui の `kl_window_text_input()`（text-input-v3、IME の preedit・commit）、`kui_file_chooser`（Notes の Open で使用中）、窓の menu（`menu.c`） |
| host の道具 | qpdf・pdftotext・pdftoppm・gs・pdfinfo・python3 の fontTools（WS079 の `make-text-pdfs.py` が使用） |

## 2. PDF の書き戻し: 増分の更新か全体の書き直しか

| 方式 | 利点 | 欠点 |
| --- | --- | --- |
| **増分の更新**（今の Notes の方式を広げる） | base の bytes が変わらず、読めない構造（form の field・outline・link・tag・metadata・object stream）を壊さない。今の `update.c` の上に足すだけ。保存が速い（変えた page だけ）。Notes の「保存のたびに base から作り直す」と合い、元に戻すことも常に可能 | **消した文字・画像の bytes が base の revision に残る**（他の tool で古い版を取り出せる）。file が base より小さくならない |
| 全体の書き直し | 消した物が file から無くなる。不要な object を捨てて小さくできる | catalog から辿れる全 object（object stream・xref stream の中も）を読み直して番号を付け直す writer が要る。読めない・知らない構造を壊す危険。base が無くなるので、Notes の「base から作り直す」と編集の再適用（§6）が成り立たず、編集はその時点で焼き込みになる |

**採用: 既定は増分の更新**。「古い版を含めない copy の保存（Save Clean Copy…）」を別の Phase（p009）の選択肢にし、ユーザーが採るか決める（D1）。
clean copy は「今の見た目の PDF を新しい file に全体で書き、その file は Notes では他の PDF（編集は焼き込み済み）として開く」物で、元の file はそのまま。
**[M11]** clean copy は新しい content が参照しない resource（消した画像・使わなくなった font の XObject と Font）を落とす。merge した resource を
そのまま複写すると消した画像が残るため。元の埋め込みの font の program は subset のまま複写するので、その font の使われなくなった glyph は残る
（font の program の作り直しは範囲外。D1 の説明にこの制限を書く）。

### 2.1 編集した page の書き方

`update.c` に page の置き方 `PDF_WRITER_PLACE_EDIT` を足す:

- page の `/Contents` = `[編集した content の stream, 上に描く物の stream]`。1 本目は §3 で作る新しい stream（元の content を `q … Q` で包んだ物）、
  2 本目は挿入した物と pen の stroke（今の overlay と同じ書き方）。元の content の stream は参照されなくなる（bytes は base に残る）。
- page の `/Resources` = 元の resource（直接・間接・継承）に、新しい Font・XObject・ExtGState を merge した直接の dictionary。`write_merged_category()` に
  `Font` を足す。
- **[M4] 名前の接頭辞**: 名前は描く時に writer 全体の通し番号で content に入る（`writer.c` の `KeiGS%u`・`KeiIm%u`）ので、page ごとに名前を選ぶことは
  できない。そこで `pdf_writer_create_update()` の時に、base の**どの page（継承の resource を含む）の ExtGState・XObject・Font にも、その接頭辞で始まる名前が
  無い**文書全体の接頭辞を 1 つ選ぶ（`Kei`、無理なら `Kei1_`、`Kei2_`…）。writer はその接頭辞で全ての名前を作る。今の「同じ名が有れば EEXIST」は残す
  （接頭辞の選び方が正しければ起きない）。注: 今の Notes でも、Notes が以前に書いた `KeiGS0`・`KeiIm0` が base の revision に入った文書（CHANGED で開いた物）の
  次の保存は EEXIST で失敗しうる潜在の bug がある（Q1 に報告済み、この接頭辞の選び方で直る）。
- 編集の無い page は今のまま（KEEP・OVERLAY・REPLACE）。
- **[N2]** `PDF_WRITER_PLACE_EDIT` を使うのは base の page の上の OVER の page だけ。Notes 自身の page（CHANGED の文書で REPLACE になる page、NEW の page）に
  挿入した物は、空の editor（`pdf_page_editor_blank()`、元の物が無い）の content を今の REPLACE・NEW の置き方で書く。REPLACE の page の古い content を
  `q … Q` で包んで残すと、前に保存した stroke と挿入した物が今の物の下に重ねて描かれ、消した stroke が戻ってしまうため。
- Notes が作った文書（base が無い、全体の書き出し）でも同じ「page の編集」を使う: base の物は無く、挿入した物だけ（§3.6）。

### 2.2 前提として拒むもの（今と同じ）

暗号化・署名の PDF は開いても編集・保存しない（今の Notes の規則）。`pdf_document_page_count()` が読めない・page が `PDF_DISPLAY_DAMAGED` の page は
編集の道具を出さない（pen は使える）。

**[M5]** interpreter の `read_contents()` は filter が読めない・上限を超えた stream を飛ばして描く（`content.c` 600〜668 行）。その page の content を
書き直すと飛ばした stream が消えるので、editor は `/Contents` の各 stream を自分で読み、1 本でも SKIPPED・LIMITED・DAMAGED なら、その page には
編集の道具を出さない（pen は使える。status に理由）。

## 3. content stream の物の取り出しと書き換え（libpdf）

### 3.1 走査（scanner）

interpreter（`content.c` の `run_content()`）に「走査の mode」を足し、page の content（`/Contents` の stream を decode して連結した物。reader と同じく
stream の間に改行を入れる）を 1 回走らせて、top level の物の表を作る。各演算子の token の開始と終了の byte の位置は lexer の `position` で分かる。

物の種類と記録する物:

| 種類 | 何が 1 つか | 記録 |
| --- | --- | --- |
| 文字の行 `TEXT` | 同じ BT…ET の中で、同じ font・同じ向きで、baseline が同じ（text 空間の y の差が font size の 1/4 以下）で、前の show の終わりとの隙間が 3 em 以下の show の演算子（Tj・TJ・'・"）の並び。**[M9]** 隣り合う top level の BT…ET の block が、同じ CTM・同じ font・同じ baseline で、間に描画の演算子（path・`Do`・inline image・shading）が無ければ 1 つの行にまとめる（語ごとに BT を出す生成器）。**[N16]** 間に色・`gs`・`q`・`Q` の演算子も無いことを条件に足す（まとめた行の内容を変えると 1 つの色になるため）。まとめた行は複数の block にまたがる | 各 show の byte の範囲、その直前の Tm・Tlm・CTM・text state（Tf と size・Tc・Tw・Tz・TL・Tr・Ts）・塗りの色、glyph の箱から作った行の外接の四辺形（page の shown space）、Unicode の文字列（§3.2、分からない文字は U+FFFD）、font の名前と種類 |
| 画像 `IMAGE` | Image XObject の `Do` 1 つ、または inline image `BI…ID…EI` 1 つ | `Do` の名前の token から `Do` までの byte の範囲、その時の CTM、画像の幅と高さ（sample）、unit square を写した四辺形、clip の有無と clip の外接の箱 |
| 図形 `GRAPHIC` | Form XObject の `Do` 1 つ | 同じ（四辺形は form の `/BBox` を `/Matrix` と CTM で写した物） |

規則:

- **[H3] 物の key**: 種類＋decode して連結した content の中の、物の最初の token の byte の位置と長さ（文字の行は最初の show の token）＋指紋
  （物の byte の範囲の SHA-256 の先頭 8 byte）。行のまとめ方（上の heuristic）に依る通し番号は key にしない（libpdf の更新だけで番号が変わりうるため）。
  一覧の中の並び（index）は content に現れた順で、その時その時の editor の中だけで使う。
- **編集しない（一覧に出さない）物**: form XObject の中の文字・画像（form は全体で 1 つの図形）、Type 3 の glyph の手続きの中、pattern の中、
  **[H2] text の rendering mode 4〜7（clip に足す文字）の show を 1 つでも含む BT…ET の block の全ての行**（block を `ET q…Q BT` で割ると、
  ET で掛かるはずの clip が途中で掛かり、後の描画が壊れるため）、縦書き（Identity-V・WMode 1）の文字の内容の変更（移動・削除は可）、
  `PDF_DISPLAY_DAMAGED` 以降の content。
- 一覧には出すが文字の内容を変えられない物: Unicode が分からない文字を含む行（移動・大きさ・削除・「全文を打ち直す」は可、§7.4）、Type 3 font の行。
  **[M9]** Tr 3（見えない文字、OCR の層）の行は削除だけ（移動・内容の変更は不可）、hit では見える物の後に回す（scan の上の OCR の文字が click を奪わない）。
- **[M10] 印付きの内容**: BDC・BMC・EMC の位置を記録する。BT の中で始まった印付きの内容（BDC/BMC が BT の後）の中の行は、その範囲で block を割らない
  （§3.4）。行を書き換えた時、その行を含む BDC の property の `/ActualText` は内容と合わなくなるので落とす（inline の dictionary はその key を除いて
  書き直す。**[N15]** 名前の property（`/Properties` の中の dictionary を名前で指す物）は、その dictionary を `/ActualText` だけ除いて inline に
  複写した `BDC` に置き換える（`BMC` にすると名前の dictionary の中の MCID が落ちる）。BDC/EMC の対と MCID は残す。
- **[L3] q/Q の釣り合い**: 走査は token から q の深さを数える（interpreter の stack の上限で数えない）。最後に開いたままの q の数を記録し、空の stack の Q
  （interpreter は無視する）の byte の範囲も記録する（§3.4 で落とす）。content が BT の中で終わる（ET が無い）ことも記録する。
  **[N14]** q の入れ子が interpreter の上限（63）を超えると、interpreter は state を push しなくなり（`content.c` 1286〜1290 行）、記録する C_rec が実際と
  ずれる。上限を超えた q の後の物は編集しない（一覧に出さない。[M5] の LIMITED と同じ扱い）。

### 3.2 文字の Unicode（抽出）

font.c に「code → Unicode」を足す（PDF の仕様の 9.10 の順）:

1. font の `/ToUnicode` の CMap（`begincodespacerange`・`beginbfchar`・`beginbfrange`、範囲の配列の形も。`usecmap` は読まない）。1 つの code が
   複数の文字（合字 `fi` など）に当たる物も。
2. 単純な font: 標準の encoding（StandardEncoding・WinAnsi・MacRoman）と `/Differences` の glyph 名 → Unicode（`encoding.c` の表、`uniXXXX`・`uXXXXX` の名前も）。
3. Type0 の Identity-H で ToUnicode が無い: 埋め込みの TrueType の cmap を逆に引いた GID → Unicode（CIDToGIDMap を通して）。
4. どれも無い: U+FFFD。

これは PDF Viewer の検索・選択（ws128-p004）にも要る部分なので、internal の関数（`pdf_font_unicode()`）として作り、後で ws128-p004 が公開の API にできる形にする（D5）。

### 3.3 編集の操作（libpdf の page editor）

新しい公開の API（`include/libc/pdf.h`、`exports.map` に追加。HAL ではない）。名前は p002・p003 で確定する。案:

```c
struct pdf_page_editor;		/* 1 page の物の表と、その上書き・挿入の状態 */

enum pdf_edit_kind { PDF_EDIT_TEXT = 0, PDF_EDIT_IMAGE = 1, PDF_EDIT_GRAPHIC = 2 };
enum pdf_edit_font { PDF_EDIT_FONT_ORIGINAL = 0, PDF_EDIT_FONT_SANS = 1, PDF_EDIT_FONT_MONO = 2, PDF_EDIT_FONT_CJK = 3 };

struct pdf_edit_key {		/* 物の key（§3.1 [H3]）: 種類、decode した content の中の最初の token の位置と長さ、指紋 */
	enum pdf_edit_kind kind;
	uint64_t offset; uint32_t length;
	unsigned char fingerprint[8];
};

struct pdf_edit_object {	/* 1 つの物の今の姿（上書きの後） */
	size_t size;		/* [L11] 呼ぶ側が sizeof を入れる。後で field を足しても古い呼び手と合う */
	enum pdf_edit_kind kind;
	unsigned flags;		/* DELETED・TEXT_KNOWN・TEXT_FIXED（内容を変えられない）・CLIPPED・INSERTED */
	double quad[8];		/* page の shown space（左上が原点、y が下向き、pt）の四隅 */
	const char *text;	/* UTF-8（TEXT）、分からない文字は U+FFFD */
	char font_name[64];	/* 元の /BaseFont（subset の接頭辞は除く）、または置き換えの font の名前 */
	double font_size;	/* shown space での見かけの大きさ（pt） */
	size_t image_width, image_height;
};

struct pdf_edit_text {		/* 文字の新しい内容 */
	size_t size;		/* [L11] sizeof */
	const char *utf8;	/* '\n' は改行 */
	enum pdf_edit_font font;
	double size;		/* pt（shown space） */
	double red, green, blue;	/* 挿入の文字だけ。既存の行の書き換えでは使わない（色は元の graphics state のまま、§3.4 [M3]） */
	double box_width;	/* 折り返しの幅（pt）、0 は折り返さない */
};

struct pdf_image_source {	/* 挿入・差し替えの画像 */
	size_t size;		/* [L11] sizeof */
	int kind;		/* JPEG（Gray・RGB の bytes のまま。CMYK は EINVAL、[M15]）・PNG_IDAT（predictor 付き deflate のまま）・RGBA（8bit、straight alpha） */
	const void *data; size_t size;
	size_t width, height; int components;
};

int  pdf_page_editor_open(struct pdf_document *document, size_t index, struct pdf_page_editor **editor);
int  pdf_page_editor_blank(double width, double height, struct pdf_page_editor **editor);
void pdf_page_editor_close(struct pdf_page_editor *editor);
size_t pdf_page_editor_count(const struct pdf_page_editor *editor);
int  pdf_page_editor_object(struct pdf_page_editor *editor, size_t index, struct pdf_edit_object *object);
int  pdf_page_editor_key(const struct pdf_page_editor *editor, size_t index, struct pdf_edit_key *key);
int  pdf_page_editor_find(const struct pdf_page_editor *editor, const struct pdf_edit_key *key, size_t *index);	/* 種類・位置・長さ・指紋が全て合う物、無ければ ENOENT */
int  pdf_page_editor_hit(struct pdf_page_editor *editor, double x, double y, size_t *index);
int  pdf_page_editor_reset(struct pdf_page_editor *editor, size_t index);			/* 元の姿へ */
int  pdf_page_editor_delete(struct pdf_page_editor *editor, size_t index);
int  pdf_page_editor_place(struct pdf_page_editor *editor, size_t index, const double transform[6]);	/* 元の位置からの shown space の変換 */
int  pdf_page_editor_set_text(struct pdf_page_editor *editor, size_t index, const struct pdf_edit_text *text, unsigned *result);
int  pdf_page_editor_set_image(struct pdf_page_editor *editor, size_t index, const struct pdf_image_source *image);
int  pdf_page_editor_insert_text(struct pdf_page_editor *editor, const struct pdf_edit_text *text, double x, double y, size_t *index);
int  pdf_page_editor_insert_image(struct pdf_page_editor *editor, const struct pdf_image_source *image, const double placement[6], size_t *index);
int  pdf_page_editor_render(struct pdf_page_editor *editor, size_t hidden, struct pdf_display_list **list);	/* hidden の物を除いて（無し: (size_t)-1） */
int  pdf_page_editor_render_object(struct pdf_page_editor *editor, size_t index, struct pdf_display_list **list);
int  pdf_writer_begin_page_edited(struct pdf_writer *writer, struct pdf_page_editor *editor);	/* update でも全体の書き出しでも */
```

editor は「元の物の表」＋「各物の上書きの状態」＋「挿入した物の列」を持つだけで、操作の履歴は持たない（undo は Notes、§6）。
**[L11]** 公開の struct は先頭の `size` で版を見分ける（ABI。getter の関数にする案は p002 で比べて決める）。
`set_text` の `result` は「元の font のまま書けた／置き換えの font になった（理由）」を返し、Notes がそれを利用者に知らせる。
`place` の変換は物の元の四辺形に対する shown space の affine（移動と拡大縮小。UI は回転を出さない）。

### 3.4 新しい content stream の組み立て

page の新しい content =

```text
q
<元の content。編集した物の byte の範囲だけを置き換え、空の stack の Q を落とした物>
<content が BT の中で終わっていたら ET>                                   [L3]
<開いたままだった q の数だけ Q>
Q
<挿入した物、挿入した順>
```

**行列の約束 [M1]**: PDF の行ベクトルの約束（点 p は p × M）。interpreter が記録する CTM は page の user space → shown space の行列 B を
含んだ `C_rec = C_user × B`（`content.c` 510 行、B は `base_matrix()`、/Rotate と crop box を含む）。よって画像の unit square → shown space は
`C_rec`、文字の text space → shown space は `Tm × C_rec`。`cm` の行列 M は新しい CTM を `M × CTM` にする。S は UI が決める shown space の変換
（移動と拡大縮小）。行列の数は `%.4f` ではなく有効数字 9 桁で書く（`pdf_buffer_append_number()` の 4 桁では大きな page の小さな縮尺で丸めが見える）[L4]。

物ごとの置き換え:

| 物・操作 | 元の byte の範囲を何に置き換えるか |
| --- | --- |
| 画像・図形の削除 | `Do` の名前と `Do`（inline image は `BI` から `EI` まで）を空に |
| 画像・図形の移動・大きさ | `q M cm /Name Do Q`、**M = C_rec × S × C_rec⁻¹**（新しい実効の行列 M × C_rec = C_rec × S）。inline image も同じく `q M cm BI … EI Q` で包む [L2] |
| 画像の差し替え | `q M cm /KeiImN Do Q`、**M = P × C_rec⁻¹**。P は「新しい画像の縦横比を保って元の四辺形（C_rec × S）の中に収め、中央に置く」unit square → shown space の置き方。古い XObject は使われなくなる（bytes は base に残る） |
| 文字の行の移動・大きさだけ [M2] | 行が属する block（まとめた行は全ての block）を正規化し、その行の各 show の直前の `Tm` を **Tm' = Tm × C_rec × S × C_rec⁻¹** に変えるだけ（block を割らない、code の列と font はそのまま。1 行の中の複数の font も保つ） |
| 文字の行の内容・font の変更 | block を正規化した上で、行の show の演算子を消し、新しい内容を出す（下） |

**文字の block の正規化**: block の中の位置の演算子（Td・TD・T*・Tm）を落とし、各 show の直前に、走査で記録したその show の直前の
`a b c d e f Tm` を出す。**[H1]** `tx ty TD` は TL（leading）も `-ty` にするので、落とす代わりに `-ty TL` を出す（TL は graphics state の一部で、
後の**触っていない** block の `T*`・`'`・`"` がその値を使う）。`'` と `"` は「`Tm`＋`Tj`」に直し（その `Tm` は `'`・`"` の暗黙の `T*` で行を移した**後**の物を記録する）、`"` の `aw ac` は `Tw`・`Tc` として先に出す
（この 2 つも後に残る値なので、元と同じく設定したままにする）。位置以外の演算子（Tf・Tc・Tw・Tz・TL・Ts・Tr・色・gs・BDC/BMC/EMC など）は元の順の
まま残す。これで block の中のどの show を消しても、他の show の位置は変わらず、block の後の text state も元と同じになる。

**文字の行の新しい内容**: 行の最初の show の位置で block をいったん閉じて出す:

```text
ET
q  BT  [/KeiFn size Tf]  a b c d e f Tm  <code の列> Tj  [ <次の行の Tm> <次の行> Tj … ]  ET  Q
BT
```

- **[M3]** 色と text state（Tc・Tw・Tz・Ts・Tr）は出さない。その位置の graphics state をそのまま受け継ぐので、Separation・CMYK・ICC・Pattern の色も
  元のまま。font を変える時だけ `Tf` を出す（`q … Q` が後の show のために元の font に戻す）。元の font のままなら `Tf` も出さない。
- **[N7] 大きさ**: `Tf` の size は常に元の行の `Tfs`（記録した text state）にし、大きさの変更は全て `Tm` の拡大縮小（S、[M2] と同じ）で表す。
  `pdf_edit_text.size` は shown space の pt なので、`1 Tf` で `Tm` に大きさを入れる生成器（cairo・Skia の形）の行に `size Tf` を書くと
  大きさが何倍にもなるため。置き換えの font の `Tf` も `Tfs` の値で書き、見かけの大きさは `Tm` で合わせる。
- 閉じた後の `BT` は Tm を単位行列に戻すが、正規化で後ろの show は全て明示の `Tm` を持つので影響しない。
- 行の残りの show（2 つ目以降、まとめた行では後の block の show も）は消す。
- **[M10]** 行が「BT の中で始まった印付きの内容」の中に在る時は block を割らない（`BMC`/`BDC` と `EMC` の入れ子が BT…ET と交差するのを避ける）。
  その時は行の show を消し、新しい内容を block の `ET` の直後に `q … Q` で出す。そこでは graphics state が行の位置と違いうるので、記録した text state
  （Tf・Tc・Tw・Tz・Ts・Tr）と塗りの色を明示する。色は device の色空間（DeviceGray・RGB・CMYK）の時だけ出せる。それ以外の色空間の行は、この場合に限り
  内容の変更を不可にする（移動・削除は可）。

**文字の内容の変更の font**: 新しい文字列の各文字を、元の font の「Unicode → code」の逆引き（§3.2 の表の逆）で code にし、その code の glyph が元の
埋め込みの program に在るかを確かめる。**[L5]** 在るとは: ToUnicode（等）の逆引きで code が決まり、その code が描く glyph が、空白類（U+0020 など）以外の
文字では空でない輪郭を持つこと（幅だけでは .notdef の代わりの空の glyph を見分けられない）。全部在れば元の font・元の size で書く（見た目が保たれる）。
1 つでも無ければ（subset に無い文字が典型）、行全体を置き換えの font（§4）にし、`result` で知らせる。埋め込みでない font（標準 14 font を含む）は
reader が system の font で代用して描いているので、常に置き換えの font にする。利用者が font を選んだ時は常に置き換えの font。
1 行に元は複数の font が混ざっていた（太字の語など）場合、内容を変えた行は最初の show の font の 1 つにまとまる（制限、§9。移動・大きさだけなら保つ [M2]）。

### 3.5 見かけの確認（preview）

Notes の画面は editor の `pdf_page_editor_render()`（上の新しい content と merge した resource を、今の interpreter にそのまま走らせる）で背景を描く。
preview では font の subset は作らず、system の font の file 全体を `FontFile2` として渡す（subset は保存の時だけ）。

**[H5] object の寿命**: font の cache（`font.c` 284〜289 行）は font の dictionary の pointer を key にし、文書が閉じるまで face を持ち続ける。editor の
arena の中に作った font の object を editor と一緒に捨てると、同じ address に後で作った別の object が cache に当たり、face が解放済みの bytes を指す
（use-after-free）。そこで system の font（Sans・Mono・CJK の 3 つ）と preview の画像の XObject の object は、**`pdf_document` の寿命の領域に 1 回だけ**
作り（stream の bytes は `pdf_object.bytes`）、全ての editor が共有する。font の cache が参照しうる object は文書が閉じるまで解放しない。画像の
object は画像の id ごとに 1 つ（同じ画像を何度差し替えても増えない）。
- **[N4] 画像の bytes の持ち主**: 文書の arena は解放せず上限が 256 MB（`PDF_READER_ARENA_MAX`、`internal.h` 23 行、`object.c` 82〜113 行）なので、
  preview の画像の stream の bytes は arena に複写しない。dictionary（cache の key になりうる object）は文書の arena に置き、bytes は libpdf が
  持つ別の buffer（RGBA は RGB と alpha の 2 つ）を指す。Notes が画像を手放した時（参照の数が 0、[L7]）に呼ぶ解放の口
  `pdf_document_release_image(document, id)` で bytes を解放し、dictionary は空の stream として残す（font の cache の key は変わらない）。
  文書を閉じる時に残りを全て解放する。
- **[N5]** editor が merge する `/Resources` は浅い複写にする: 値の pointer は base の文書の object を指したまま（page の Resources の中に直接書かれた
  font の dictionary を深く複写すると、cache の key が複写の address になり、同じ use-after-free が戻る）。
- **[N6] 試験**: ASan の quarantine は address を再利用させないので、この use-after-free の回帰は ASan では出ない。試験は editor の開閉を
  1500 回（`FONT_COUNT_MAX` 4096 を 3 つの font で超える回数）繰り返して毎回 render し、font の cache の数が開閉で増えないことを assert する。

**[M12]** preview（font 全体、CID = 元の GID）と保存（subset、CID = 新しい GID）は content の code が違うので「同じ content」ではない。同じ glyph を
描くことは試験で確かめる: 保存した file を libpdf で描いた画素と preview の画素を host で比べる（§11.1）。

### 3.6 挿入した物

- 文字: `q <色> BT /KeiFn size Tf a b c d e f Tm <行> Tj … ET Q`。折り返し（§7.4 の box の幅）は libpdf が font の advance で行に分ける。
  **[L8]** /Rotate の page では、挿入の Tm は page の回転を打ち消す向きにする（画面で水平に読める）。
- 画像: `q M cm /KeiImN Do Q`（挿入した物は元の content の外、page の初めの CTM の下なので M = P × B⁻¹）。
- 挿入した物は page の元の content より上、pen の stroke より下（z の順: 元の content → 挿入した物（挿入の順）→ pen）。順の入れ替えは範囲外。

## 4. font

### 4.1 置き換え・挿入に使う font

| 選択肢 | file | 範囲 |
| --- | --- | --- |
| 元の font（Original） | PDF に埋め込みの物 | §3.4 の条件を満たす時だけ選べる |
| Sans（Inter） | `/usr/share/fonts/keiland.ttf` | Latin・Greek・Cyrillic。Regular だけ（variable font の既定の instance） |
| Mono（JetBrains Mono） | `keiland-mono.ttf` | Latin |
| Japanese / CJK（Droid Sans Fallback） | `keiland-fallback.ttf` | 日本語・中国語・韓国語の漢字・仮名・記号 |

文字ごとの fallback: 選んだ font に無い文字（cmap が glyph 0）は Droid Sans Fallback で、そこにも無ければ Inter で、それにも無ければ描かずに数え、
`result` で知らせる。1 つの行が複数の font の run に分かれることがある（Inter の英字＋Droid の漢字。desktop の fontconfig の優先と同じ並び）。

serif の font は system に無いので出さない（Future Work の候補、D2）。太字・斜体も出さない（D3。variable font の wght の instance 化か、Tr 2 の擬似太字・
Tm の傾きの擬似斜体が要る）。

### 4.2 埋め込みの形

各 font（文書に 1 つ、全 page で共用）を:

- `/Type /Font /Subtype /Type0 /BaseFont /ABCDEF+Inter-Regular /Encoding /Identity-H /DescendantFonts [CIDFont] /ToUnicode <CMap>`
- CIDFont: `/Subtype /CIDFontType2 /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /CIDToGIDMap /Identity /W [...] /DW`
- FontDescriptor: `/Flags`（Inter 32 = nonsymbolic、Mono は FixedPitch も）、`/FontBBox`・`/Ascent`・`/Descent`・`/CapHeight`・`/ItalicAngle 0`・`/StemV`（OS/2 と head から）、`/FontFile2`。
- subset の tag（6 文字の大文字）は使った glyph の集合の hash から作る（同じ内容なら同じ名前）。
- `/ToUnicode` は CID → Unicode の `bfchar`（同じ glyph が複数の文字から来る時は最初の文字）。pdftotext・PDF Viewer（ws128-p004 の後）で検索・copy ができる。
- 文字の列は 2 byte の CID（= subset の新しい GID）の hex string。

reader は Type0/CIDFontType2/Identity-H と CIDToGIDMap /Identity を既に読む（`font.c`）ので、preview も他の viewer も同じに描く。

### 4.3 subset（TrueType）

libpdf の writer に TrueType の subset を作る部分を置く（`truetype` の table を自分で読む。libtruetype は glyph の描画用で table の書き出しを持たない）:

- 使った glyph と、それらの composite glyph が参照する部品の glyph を再帰で集め（深さの上限 8）、glyph 0（.notdef）を先頭に、**詰めて番号を付け直す**。
  composite の部品の glyph 番号も書き換える。
- 出す table: `head`（indexToLocFormat を合わせ、checkSumAdjustment を計算し直す）・`hhea`（numberOfHMetrics）・`maxp`（numGlyphs）・`loca`・`glyf`・`hmtx`・
  `cvt `・`fpgm`・`prep`（有れば。hinting の命令はそのまま）・`name`（著作権 ID 0、license の ID 13・14、family・full name、§4.4）・`OS/2`（fsType を含む）・`post`（format 3）。
- 出さない table: `cmap`（CIDToGIDMap で足りる）・`GSUB`・`GPOS`・`GDEF`・`kern`・`vhea`・`vmtx`、**variable font の `fvar`・`gvar`・`avar`・`HVAR`・`MVAR`・`STAT`**
  （これで Inter は既定の instance の静的な font になる。PDF の reader は variable font を扱わない）。
- 大きさの目安: Droid Sans Fallback の数百字の subset で数十 KB、Inter の英文で 20〜40 KB。
- 検証: host で fontTools が subset を開け、glyph の数・部品の参照・幅が元と合う。

### 4.4 license の制限

| font | license | 埋め込み | 注意 |
| --- | --- | --- | --- |
| Inter | SIL OFL 1.1、RFN なし | 可。OFL は文書への埋め込み（subset を含む）を許し、その文書は OFL に縛られない（OFL の FAQ） | font を単独で再配布しない（PDF の中だけ）。`name` の著作権・license の record を subset に残す |
| JetBrains Mono | SIL OFL 1.1、RFN なし | 同上 | 同上 |
| Droid Sans Fallback | Apache-2.0、OS/2 fsType 8（Editable embedding） | 可。fsType 8 は「埋め込んだ文書を編集してよい」で、Notes の用途に合う | Apache-2.0 の notice を保つため、`name` の著作権（ID 0）と license（ID 13・14）の record を subset に残す。**[L11]** subset は Apache-2.0 の §4 の「改変した物」に当たるので、§4(a) の license の写し（ID 13・14 の文と URL）と §4(b) の改変の notice（例: `name` の ID 5（version）に「subset by Kei Notes」を足す）を入れる。これで足りるかを D2 に添えてユーザーに示す |

確認済み（review、2026-10-06）: 3 つの font の fsType・RFN の有無・name ID 0/13/14 の有無、Inter の variable の table。Droid Sans Fallback は
composite の glyph が多く、§4.3 の部品の番号の付け直しは必須。

一般の規則（将来 font を足した時も効くように writer が守る）: OS/2 の fsType を読み、`0x0002`（Restricted License embedding）の font は埋め込まない
（その font を選べなくする）。`0x0100`（No subsetting）なら subset にせず全体を埋め込む。`0x0200`（Bitmap embedding only）の font は使わない。
元の PDF に埋め込みの font（§3.4 の「元の font のまま」）は、その PDF に既に入っている program を使うだけで、Notes は新しく埋め込まない。

## 5. 画像

### 5.1 受ける形式

**PNG と JPEG**（Q1 の指定どおり）。GIF・WebP・HEIC は範囲外（Image Viewer が開ける GIF は後で足せる）。

| 入力 | PDF での書き方 |
| --- | --- |
| JPEG（baseline・progressive、Gray・RGB） | bytes をそのまま `/DCTDecode`（今の `pdf_writer_draw_jpeg_image()`）。EXIF の向きは画素を回さず、置き方の行列で回す（`picture/` の `kl_picture_exif_orientation()` で向きを読む） |
| PNG（8bit の Gray・RGB、alpha なし、interlace なし、tRNS なし） | IDAT を連結した bytes をそのまま `/FlateDecode /DecodeParms << /Predictor 15 /Colors n /BitsPerComponent 8 /Columns w >>`（PNG の行の filter は PDF の predictor と同じ）。reader は predictor を読む（`filter.c`）。**[L6]** 素通しの前に、自前の小さな chunk の parser で各 chunk の CRC を確かめ、IDAT を 1 度全部 inflate して行の数と長さが IHDR と合うことを確かめる（壊れた PNG を PDF に入れない）。tRNS の有る PNG は下の行（SMask）へ。iCCP・gAMA・cHRM・sRGB は無視する（色がわずかにずれることがある、§9） |
| PNG（それ以外: alpha・palette・16bit・1/2/4bit・interlace） | libpng-compat で 8bit の RGBA に解き、RGB と alpha（`/SMask`）に分けて deflate で圧縮（D4。deflate が無ければ無圧縮） |

**[M15]** CMYK（と YCCK）の JPEG は、今の writer の JPEG の書き出しが成分 1・3 だけ（`writer.c` 530 行）なので v1 では受けず、「この JPEG の
色の形式は入れられない」と知らせる。EXIF の向きを読む `kl_picture_exif_orientation()` の `picture/picture.c` は今の Notes が build していないので、
Notes の Makefile に足す [L6]。

上限: 一辺 16384 sample（reader の `PDF_IMAGE_SIDE_MAX` と同じ）、画素の数 64 M。超える画像は「大きすぎる」と知らせて受けない（縮小して入れるのは範囲外）。

### 5.2 deflate（D4）

今の libpdf は stream を無圧縮で書く。挿入の画像（alpha つきの PNG、4000×3000 で約 48 MB）・編集した content（元が数 MB の page もある）が膨らむ。
**libz-compat に deflate（`compress2`・`deflateInit`/`deflate`/`deflateEnd` の最小）を足す**ことを推奨する: LZ77（hash chain、窓 32 KB）と固定・動的 Huffman。
WS175 の所有の path の外（`userland/base/libz-compat`）なので、Q1 の割当と path の許可が要る。代案は libpdf の中だけの deflate（他から使えない）か、無圧縮のまま（大きい）。

**[H6] 圧縮する stream と、しない stream**:

- 圧縮する: 新しい画像の XObject と SMask、埋め込む font の program（`FontFile2`）と ToUnicode、**編集した base の page の新しい content**（§3.4、
  ZNOT が major 2 の文書だけに現れる）。
- 圧縮しない: **pen の stroke の content**（Notes の reader は、自分が書いた revision の content を hash で照合し、filter の付いた stream を拒む。
  `reader.c` 533〜557・3641〜3661 行。**[N9]** そのとき `open_own` は ESTALE を返し、`open_as_base(decoded=1)` で全 page が背景の CHANGED として
  開く（`save.c` 233〜258 行））と **ZNOT の添付 `kei-notes.bin`**（添付は decode せずに読まれる、`reader.c` 622〜624 行）。major 1 の文書は今と同じ
  bytes になる。
- **[N11]** NEW・REPLACE の page と空の editor の page（`save_whole` の page）の content も圧縮しない（その hash を記録し、`open_own` と
  `page_unchanged` が照合する。`save.c` 546〜555 行）。`PLACE_EDIT` の page は `pdf_writer_get_page_content_hash()` が ENOENT を返す（照合の対象外）。
- 試験: 画像を入れて保存し、開き直して Notes の文書（FOREIGN ではない）として開くこと（§11.1 の「Flate の開き直し」）。

### 5.3 挿入の置き方

挿入した画像は、page の見えている範囲の中央に、page の幅の 1/2 と高さの 1/2 に収まる大きさ（縦横比を保つ。元の画像の 72 dpi の大きさより大きくはしない）で置く。

## 6. Notes の model・undo・journal・保存

### 6.1 model

`notes.h` に足す（名前は p007 で確定）:

```c
struct notes_image {		/* 挿入・差し替えの画像の bytes。複数の状態から参照されるので参照の数を持つ */
	uint32_t id; unsigned refs;
	unsigned kind;		/* JPEG・PNG_IDAT・RGBA */
	unsigned char *data; size_t size; size_t width, height; int components; int orientation;
};

struct notes_object_state {	/* 1 つの物の上書きの状態（元の物）か、挿入した物の状態 */
	struct pdf_edit_key key;	/* 元の物: 種類・位置・長さ・指紋（§3.1 [H3]） */
	uint32_t id;		/* 挿入した物: Notes の id（stroke と同じ next_id から） */
	unsigned flags;		/* DELETED・PLACED・TEXT・IMAGE・INSERTED */
	float transform[6];	/* shown space の変換（PLACED）、挿入した物は置き方 */
	char *text; unsigned font; float size; uint32_t color; float box_width;	/* TEXT */
	struct notes_image *image;	/* IMAGE（差し替え・挿入） */
};

/* struct notes_page に足す */
	struct notes_object_state *edits; size_t edit_count;	/* 元の物の上書き（key の順）と挿入した物（挿入の順） */
	struct pdf_page_editor *editor;		/* 画面と保存のための cache（edits から作り直せる） */
```

editor は page を初めて編集する時・文書を開いた時に作り、edits を順に適用する（cache）。edits が正本で、editor はいつでも作り直せる。

### 6.2 undo・redo

`NOTES_UNDO_EDIT_OBJECT` を足す: 1 つの entry = (page, key, 前の状態, 後の状態, 挿入した物の列の中の位置)。**[L7]** 挿入した物の削除の undo は、
その位置（z の順）に戻す。前の状態が「無い」のは挿入、後の状態が「無い」のは
元の物の上書きの取り消し・挿入した物の削除。undo は前の状態に戻して editor の該当の物を `reset` してから前の状態を適用し直す。

entry の単位（利用者の操作 1 つ = undo 1 つ）: 移動・大きさの drag の終わり、文字の編集の box を閉じた時（box の中の打鍵は box の中の undo、§7.4）、
font・size の変更、差し替え、挿入、削除。今の `NOTES_UNDO_LIMIT`（1000）の中に入る。画像の bytes は `notes_image` の参照の数で共有する（undo の entry は複製しない）。上限で古い entry を
捨てる時は、その entry の前後の状態が持つ画像の参照を減らし、0 になった画像を解放する [L7]。

既存の物を元の姿に戻す操作 **Reset**（操作の帯、§7.2）を用意する。undo の履歴は file に残らないので、開き直した後に編集を取り消す手段はこれになる [L10]。
消した元の物は見えず選べないので、開き直した後には戻せない（§9）。

### 6.3 ZNOT（編集の data）と journal

- 新しい chunk `EDIT`: page 番号 varint、状態の数 varint、各状態 = 種類 u8・位置 varint・長さ varint・指紋 8 byte（元の物の key、[H3]）または
  挿入した物の id varint・flags u8・変換（1/64 pt と 1/65536 の固定小数）・文字（UTF-8 の長さ varint と bytes、font u8、size、色 RGBA、折り返しの幅）・
  画像の id varint。
- 新しい chunk `IMAG`: 画像の id、種類、幅・高さ・成分、向き、**PDF の中の XObject を指す印**（下）。保存した file の ZNOT には画像の bytes を入れない
  （PDF の XObject と二重になる）。
- **[M6] 画像の書き出しと読み戻し**:
  - 画像の id ごとに XObject を 1 つ書き、同じ画像を使う全ての page がそれを共有する（複数頁の同じ画像が重複しない）。
  - XObject の dictionary に私的な key `/KeiNotesImage <id>` を書く（writer に「画像の dictionary に追加の key を書く」API を足す）。
  - 読み戻しは種類ごと: JPEG は stream の bytes（DCTDecode のまま）、PNG_IDAT は stream の bytes と `/DecodeParms`、RGBA は `image.c` の decode で
    画像と `/SMask` の 2 つの stream を解いて RGBA に戻す。
  - 読み戻した bytes は file を閉じる前に Notes の `notes_image` に複写する（**[N9]** `save.c` 224〜231 行は、開く時に `open_own` の後で `file` を閉じる）。
  - **[N3]** `notes_image` は画像を圧縮した形で持つ: 元の PNG・JPEG の bytes、または RGBA を deflate した bytes（D4）。生の RGBA（4000×3000 で約 48 MB）を
    持たない（journal と memory のため）。
- **版**: `EDIT` を含む ZNOT は **major 2**。今の Notes（major 1 だけ読む）は major 2 を読めない時、design-pdf.md §3 の規則で「他の PDF」として開く
  （編集を焼き込んだ見た目が base になる）ので、古い Notes が保存しても編集は失われない。編集の無い文書は今の 1.x のまま書く。新しい decoder は
  major 1 を読み続ける。**[L1]** 確認済み: 古い Notes は major ≠ 1 の ZNOT を EINVAL にし（`encode.c` 444〜449 行）、文書全体を FOREIGN で開く
  （CHANGED ではない）。そのとき新しい Notes で書いた pen の線も背景（焼き込み）になり、Notes の file であることは知らされない。
- **[H3] 開く時の照合**: base の bytes の SHA-256（今の `base_hash`）が合い、**全ての**状態の key が editor の物（`pdf_page_editor_find()`: 種類・位置・
  長さ・指紋が全て合う物）に当たり、画像の XObject も全て読み戻せた時だけ編集を再適用する。1 つでも当たらなければ、状態を捨てずに、
  **file 全体を新しい base として開く**（`open_as_base`: 今の見た目が base になり、編集と pen は焼き込み、undo は空）。status に「編集をこの版の
  PDF に合わせられなかったので、見えている形で開いた」と出し、log `NOTES OPENED … rebased=1`。**[N12]** そのための開き方の種類
  `NOTES_OPENED_REBASED` を足す（今の `open_as_base(decoded=1)` は CHANGED になり、status が「他の program が変えた page は背景になった」になる、
  `main.c` 629〜630 行）。**[N1]** rebase では全ての page を焼き込む。CHANGED の通常の開き方で `open_as_base` が content hash の合う非 OVER の page を
  REPLACE で残す時（`save.c` 772〜786 行。`page_unchanged` は content の stream だけを hash し XObject を見ない、869〜882 行）は、その page の編集の
  画像が全て読み戻せた時だけ残し、そうでなければその page を変わった page（背景）として扱う（宙に浮いた画像の id を持つ page を作らない）。こうすれば次の autosave で編集が file から消えない
  （状態を捨てて開くと、次の保存がその状態の無い revision を書き、編集が永久に失われる）。
- **[H4] journal**: 状態の変更 1 つを `EDIT` と同じ符号化で 1 record に。新しい画像は、最初に使った時に bytes ごと 1 record（`IMAG`＋bytes）で書く。
  保存すると journal は消え、次の journal は保存した file の ZNOT の snapshot から始まる（`journal.c` 8〜26・578〜586 行）が、その ZNOT には画像の
  bytes が無く、回復の `notes_attach_base()` は base の大きさまでしか読まない（保存した revision の XObject を読まない）。そのため**journal に、今の状態が
  参照する全ての画像の bytes を持たせる**（保存→編集→crash の回復で、保存の前に入れた画像を失わない）。
  - **[N3] 書き方**: snapshot は保存の後の最初の変更で UI の loop の上で丸ごと書かれ fsync される（`journal.c` 603〜611・640〜650 行）。画像の bytes を
    毎回 snapshot に入れると重いので、画像の bytes は journal の横の file（`<journal>.images/<id>`、内容の hash を名前に含める）に id ごとに 1 回だけ書き、
    snapshot と record には id と hash だけを入れる。横の file は保存した文書の画像が要らなくなった時（参照 0）と journal を消す時に消す。
    この時間は [M13] の予算に入れる。
  - **[N10] 順と版**: snapshot の本体（`EDIT` の chunk）は画像を id で参照するだけで、横の file が先に在るので前方参照は起きない。journal の新しい
    record の型のために `JOURNAL_VERSION`（`journal.c` 41 行）を上げる（古い Notes は新しい journal を回復しない）。
  - journal の上限 `JOURNAL_SIZE_MAX`（512 MB）を超える時は journal を止めて status で知らせ、次の保存までは crash の回復が効かないことを明示する。
  - **[N8] 回復の照合**: journal の回復（`notes_journal_recover()` と `notes_attach_base()`、`main.c` 588〜596 行）も [H3] と同じく key を照合する。
    1 つでも合わなければ（crash と回復の間に libpdf が変わった時など）、file には journal の変更が無いので rebase はできない。回復を失敗にし、journal と
    横の file を消さずに残し（status と log `NOTES RECOVER failed reason=key`）、file は今の保存された形で開く。状態を捨てて回復を続けると次の autosave
    で編集を失うため。
  - journal の回復は record を順に適用する。

### 6.4 保存

- base の在る文書（`save_update()`）: 編集の在る page は `pdf_writer_begin_page_edited()`（§2.1）、その上に pen の stroke。編集の無い page は今のまま。
- Notes が作った文書（`save_whole()`）: 挿入した物の在る page は `pdf_page_editor_blank()` の editor から同じく書く。
- 文字に使った system の font は writer が文書全体で集めて、保存の終わりに font ごとに 1 つの subset を作る。
- autosave（変更の 5 秒後）は今のまま。文字の編集の box が開いている間は、その box の内容は保存に入れない（閉じた時に 1 つの変更になる）。
- **[M13] 速さ**: autosave は UI の loop で走る（`main.c` 476〜481 行）。毎回 page の content の組み立て・deflate・font の subset（Droid の 4 MB から）を
  やり直さないよう、page ごとの新しい content と圧縮した bytes、font ごとの subset を「その page・その font の glyph の集合の世代の番号」で cache し、
  変わった物だけ作り直す。開く時は、照合 [H3] のために編集の在る page を全て走査する（描画はしない、1 page に interpreter の走査 1 回）。照合の
  結果で文書の開き方が決まるので遅らせない（後で外れると、その時までの編集との整合が取れない）。**[N13]** 照合の後は、§7.5 の窓（表示中の page と
  その前後）の外の editor を捨てる（edits から作り直せる）。目安: 10 頁を編集した文書の autosave 1 回が 200 ms 未満、開く時の照合が 500 ms 未満。p003・p007 で測る。

## 7. Notes の UI

### 7.1 道具

toolbar（`ui.c`）の道具の並びに 3 つ足す: **Select**（矢印、物の選択と移動・大きさ）、**Text**（`T`、文字の挿入）、**Image**（画像の挿入、押すと file chooser）。
pen・highlighter・eraser との切り替えは今の道具の切り替えと同じ。窓の menu（`menu.c`）にも Edit の項目（Select・Insert Text・Insert Image・Delete・Font…）。

道具ごとの入力:

| 入力 | Select | Text |
| --- | --- | --- |
| pointer・pen の click | その点の一番上の物を選ぶ（`pdf_page_editor_hit()`、無ければ選択を外す） | 選択の外: その点に新しい文字の box を開く。文字の上: その行の編集を始める |
| pointer・pen の drag | 選んだ物の上: 移動。handle の上: 大きさ。何も無い所: 何もしない（範囲の選択は範囲外） | 新しい box の幅を決める（折り返しの幅） |
| double-click | 文字の行: 編集の box を開く。画像: 差し替え（file chooser） | — |
| 指 [M8] | 今の指の規則（1 本の指 = scroll、double tap = zoom、`touch.h` 8〜16 行）を保つ。1 本の指の tap で選択。選んだ物の上の long-press（300 ms 動かさない）の後の drag で移動、handle の上も同じく long-press の後に大きさ。それ以外の指の drag は scroll。double tap は選んだ文字の行の上でだけ編集の box を開き、他では今の zoom | tap で box |
| key | Delete・Backspace で削除、矢印で 1 pt（Shift で 10 pt）移動、Esc で選択を外す、Ctrl+Z/Ctrl+Shift+Z | box の中は §7.4 |

### 7.2 選択と handle

- 選んだ物の四辺形を青い線（`#2f7cf6` 系、theme の accent）で描き、四隅に 8 px の四角の handle。
- 画像・図形の handle の drag は縦横比を保つ（Shift で自由）。文字の行の handle は等倍の拡大縮小（= font size の変更）。最小 4 pt、最大 page の 4 倍。
- 選んだ物の上（page の上端に近い時は下）に小さな操作の帯: 文字は **Edit Text・Font・Size・Delete**、画像は **Replace・Delete**、図形は **Delete**。
  元の物を変えた後は **Reset**（元の姿に戻す、§6.2）も出す。
- clip の中の画像（`CLIPPED`）を動かすと clip の外は見えない。選んだ時に clip の箱を点線で描き、帯に「Clipped」と出す（clip を解くのは範囲外）。
- 編集できない物（form の中・path など）は選べない（click は何も選ばない）。内容を変えられない行は Edit Text を灰色にし、押すと理由を出す。

### 7.3 描画

- 背景は今の `NOTES_TEXTURE_BACKGROUND`。編集の在る page は editor の `render()` から描く。
- 移動・大きさの drag の間は、背景を「その物を除いて」1 回描き直し、物だけの display list（`render_object()`）を 1 回画素にして新しい texture
  （`NOTES_TEXTURE_OBJECT`）にし、GPU で変換して描く（drag の度に page 全体を CPU で描き直さない）。drag の終わりで背景を描き直す。
- 文字の編集の box の間も同じく、背景は元の行を除き、box の中身は打鍵ごとに box の物だけを描き直す。

### 7.4 文字の編集の box

- page の上のその場の編集: 行の位置に、置き換えの後の font・size で文字を描き（preview は libpdf、§3.5）、caret（点滅）と選択の範囲を Notes が重ねる。
  **[L8]** caret は文字の向きに沿った線（text space の縦の線を shown space に写した物。/Rotate の page や回転した文字では斜め・横になる）。
- **[M7] 入力の配管**: 今の Notes の `window_event`（`window.c` 310〜345 行）は TEXT_COMMIT・PREEDIT・DELETE（surrounding の削除）を渡さず、
  key の repeat を捨て（373〜379 行）、key → 文字の変換を持たない。p008 で足す: repeat の key の受け取り、`kl_key_character()` での文字、
  `kl_window_text_cursor()` での caret の矩形、IME の surrounding の削除、clipboard の text の読み書き（見積もり +0.5 LW）。
- key: 文字の入力、Backspace・Delete、←→（Ctrl で語）、Home・End、↑↓（行）、Shift で範囲の選択、Ctrl+A、Enter で改行、Ctrl+V（libkeiui の clipboard の text）、
  Ctrl+C・Ctrl+X（選択の範囲）。Esc か box の外の click で確定（1 つの undo）。確定の前の Ctrl+Z は box の中の取り消し。
- IME: box を開いている間 `kl_window_text_input(window, 1)`。preedit は caret の位置に下線つきで描き、commit で挿入する。候補の窓の位置のために caret の矩形を
  text-input に知らせる。zdesktop の screen keyboard も同じ口で来る。
- font の picker: 帯の Font で一覧（Original（元の名前）・Sans（Inter）・Mono・Japanese（Droid Sans Fallback））。Original は §3.4 の条件を満たす時だけ選べる。
  Size は `-`・数字・`+`（0.5 pt 刻み、6〜144 pt）。挿入の文字の色は pen の 5 色から（帯の色の点）。既存の文字の色は保つ（色の変更は範囲外）。
- 置き換えの font になった時は status に「“Helvetica” lacks some characters; the line now uses Inter」の形で出す（英語。UI の文言は Notes の今の文言に合わせる）。

### 7.5 複数頁

- 今の page の移動（`<`・`>`・PageUp/PageDown・`3 / 12`）を保つ。page を変えると選択と編集の box は確定して閉じる。
- 各 page の editor は表示した時に作り、page を離れても edits が残る（editor の cache は表示中の page とその前後だけ持ち、他は捨てて edits から作り直す）。
- 編集の在る page の数を `NOTES SAVE` の log と status に出す。page の削除・並べ替えは範囲外（Acrobat の page の操作）。

### 7.6 log（AAT と T1 の判定用）

**[L9]** 今の `NOTES TOOL %u` は道具の番号だけ（`main.c` 963 行）なので、`NOTES TOOL N name=pen|highlighter|eraser|select|text|image` の形に名前を
足す（番号は残し、今の試験の pattern を壊さない）。

`NOTES EDIT select page=P object=I kind=text|image|graphic text="…"`（文字は先頭 40 byte）、
`NOTES EDIT move page=P object=I dx= dy=`、`NOTES EDIT resize page=P object=I sx= sy=`、`NOTES EDIT text page=P object=I chars=N font=original|sans|mono|cjk fallback=0|1`、
`NOTES EDIT font page=P object=I font=…`、`NOTES EDIT replace page=P object=I image=W×H`、`NOTES EDIT insert page=P kind=text|image object=I`、
`NOTES EDIT delete page=P object=I`、`NOTES EDIT reset page=P object=I`、`NOTES EDIT undo|redo page=P object=I`、`NOTES SAVE … edits=N edited_pages=M`、
`NOTES OPENED … edits=N rebased=0|1`。object の I は editor の中の並び（§3.1）。
既存の `NOTES SAVE`・`NOTES OPENED` の行は今の項目を変えず、新しい項目を足す。`NOTES OPENED … path=%s` は path で終わる（`main.c` 2339 行、path に
空白が入りうる）ので、新しい項目は **`path=` の前**に入れる。

## 8. PDF Viewer・他の app への影響

- PDF Viewer は変えない（WS175 の保存した PDF は普通の PDF として開ける。文字は ToUnicode つきなので ws128-p004 の後に検索できる）。
- libpdf の公開の API が増える（`pdf.h`・`exports.map`）。既存の関数の形は変えない。WS127 p004（thumbnail）・ws128-p004 と libpdf の source が重なるので、
  同時に流さない（Q1 の調整）。

## 9. 範囲外（制限）

- Acrobat の全機能: form の field・署名・OCR・注釈（PDF の Annot）の編集・page の削除・並べ替え・回転・頁の挿入（`+ Page` の白紙を除く）。
- path（線・塗り・表の罫線）の編集、form XObject の中の物の個別の編集、図形の差し替え。
- 段落の再配置: 既存の文字は**行ごと**に編集する。行を伸ばしても次の行は動かない・折り返さない。複数の行をまたぐ選択・編集はしない。
- 1 行の中の複数の font・太字の語の保持（編集した行は 1 つの font の run の並びになる）。kerning・合字・shaping（GSUB/GPOS）は使わない（Latin と CJK は読める。
  Arabic・Indic などの複雑な文字は範囲外）。縦書きの内容の変更（移動・削除は可）。既存の文字の色の変更。太字・斜体・serif（D2・D3）。
- 物の重なりの順の変更、回転の handle、画像の切り抜き、clip の解除、複数の物の同時の選択・範囲の選択。
- 暗号化・署名の PDF の編集（今の Notes と同じく拒む）。Type 3 font の文字の内容の変更。予約の CMap（UniJIS など）の font の文字（reader が描かない）。
- 増分の更新では消した物の bytes が file に残る（D1 の clean copy を採らない限り）。clean copy でも、元の埋め込みの font の使われなくなった glyph は残る [M11]。
- 編集できない page・行（理由を status に出す）: clip の文字（Tr 4〜7）を含む block の行 [H2]、読めない・上限を超えた stream を含む page [M5]、
  page 全体が 1 つの form XObject で包まれた文書（form の `Do` 1 つの図形になり、中の文字・画像は選べない）と scan の PDF（画像 1 枚と Tr 3 の OCR の
  文字。画像は編集でき、OCR の行は削除だけ）[M9]、BT の中で始まった印付きの内容の中の、device 以外の色空間の行の内容の変更 [M10]。
- 開き直した後の、消した元の物の復元（undo の履歴は file に残らず、消した物は選べない。Reset は見えている物だけ）[L10]。
- CMYK・YCCK の JPEG の挿入 [M15]。PNG の色の profile（iCCP・gAMA・cHRM）は無視する [L6]。

## 10. Phase の分け方と見積もり

**[M16]** model（p007）は画像の種類と私的な key（p006）、font の収集（p005）に依るので、それらと並列にしない。画像と文字の書き換えを分け、D6 の
「画像を先に」の段を表で示す。

| Phase | 内容 | 所有 path | 依存 | LW | D6 の先の段 |
| --- | --- | --- | --- | --- | --- |
| p002 | libpdf: 走査（物の表・key・byte の範囲・graphics state・四辺形・指紋・hit・行のまとめ・Tr と印付きの内容の記録）と文字の Unicode（ToUnicode・encoding・cmap の逆引き）。host 試験（ASan/UBSan、fuzz の corpus） | `userland/base/libpdf`、`include/libc/pdf.h` | p001 | 2.5 | 画像の走査の部分（約 1） |
| p003 | libpdf: 画像・図形の editor（削除・移動・大きさ・差し替え・挿入）、content の組み立て（q/Q・ET の閉じ・行列）、preview の render と文書の寿命の object [H5]、`PLACE_EDIT` と Font の merge・文書全体の接頭辞 [M4]、読めない stream の判定 [M5]、全体の書き出しでの editor。host 試験 | 同上 | p002 | 2 | ○ |
| p004 | libpdf: 文字の書き換え（正規化 [H1]・移動の Tm [M2]・元の font での内容の変更 [M3]・印付きの内容 [M10]・glyph の判定 [L5]） | 同上 | p003 | 1.5 | — |
| p005 | libpdf: TrueType の subset（composite・variable の table を落とす・name と license の notice・fsType）、Type0/CIDFontType2 の埋め込みと ToUnicode（font の stream の圧縮を含む）、system の font と文字ごとの fallback、行の折り返し、文字の挿入・font の変更。host 試験（fontTools・pdftotext） | 同上 | p004・p006（[N18] writer.c が重なるので p006 の後） | 2.5 | — |
| p006 | libz-compat の deflate（D4）、圧縮する stream の選び方 [H6][N11]、画像の取り込み（JPEG の向き・PNG の IDAT の素通しと検査・その他の PNG の RGBA＋SMask・CMYK の拒否）、画像の私的な key と共有の XObject [M6]、preview の画像の bytes の持ち主と解放の口 [N4] | `userland/base/libz-compat`（Q1 の許可）、`userland/base/libpdf` | D4、p003 | 1.5 | ○ |
| p007 | Notes の model: 物の状態・key・undo（z の位置・参照の数）・Reset・journal（画像の横の file [H4][N3]、版 [N10]、回復の照合 [N8]）・ZNOT major 2（EDIT・IMAG）・保存（update と全体、OVER だけ PLACE_EDIT [N2]、cache [M13]）・開く時の照合と新しい base [H3][N1][N12]・画像の読み戻し。Notes の Makefile に `picture.c` [L6][N18]。host 試験（`host-notes.c` を広げる） | `userland/desktop/notes/`（model 側: notes.h・document.c・encode.c・journal.c・save.c・Makefile） | p006（文字の部分は p005） | 2.5 | 画像の部分（約 1.5） |
| p008 | Notes の UI: 道具・選択・handle・操作の帯・指の gesture [M8]・drag の描画・文字の編集の box と IME と入力の配管 [M7]・font の picker・画像の file chooser・log [L9]。AAT の draft の手直し | `userland/desktop/notes/`（main.c・ui.c・render.c・window.c・menu.c・touch.c） | p007 | 3.5 | 画像の部分（約 2） |
| p009 | （D1 で採れば）Save Clean Copy: 読んだ文書を今の見た目で全体に書き直す writer（catalog から辿れる object の複写・番号の付け直し・object stream の展開・古い版を落とす・参照されない resource を落とす [M11]） | `userland/base/libpdf`、Notes の menu | **[N17]** p008（文字と font・deflate・model と保存・menu の全てに依る） | 2.5 | — |
| p010 | T1 の QEMU（Venus）で AAT の 5 シナリオと Notes の既存の回帰、FAIL の直し 1 回分。シナリオを active に | — | p008（p009） | 1 | ○（画像の 2 シナリオ） |
| p011 | 規約の全文の見直し（WS の変えた C の code 全部） | — | p010 | 0.5 | ○ |

合計 **17.5 LW**（p009 の Clean Copy を入れると **20 LW**）、±50%。Q1 の概算 6 LW より大きい主な理由: 文字の抽出（ws128-p004 と共通）、font の subset と
埋め込み、content の書き換えの正規化、Notes の UI（文字の編集の box・IME・入力の配管）がそれぞれ独立に重い。
**D6 の (b)「画像を先に」**: 上の表の「先の段」の列（p002 の画像の部分・p003・p006・p007 と p008 の画像の部分・p010・p011）で**約 10 LW**。
文字（p004・p005 と p002・p007・p008 の残り）は後の段で、**[N18]** 後の段にも T1 の QEMU と規約の見直しの Phase（p010・p011 に当たる物、約 1.5 LW）を
足す（全体は約 19 LW、Clean Copy 込み 21.5 LW）。

## 11. 試験

### 11.1 host（各 Phase、実装の担当）

置き場所は `plan/ws175/tests/`。

- **`make-edit-samples.py`**（p002 で作る。fontTools と python の zlib。WS079 の `make-text-pdfs.py` に倣い、host の Liberation・DejaVu・Droid の font を使う）:
  - `edit-basic.pdf`: 3 page。1 頁: 埋め込みの TrueType（WinAnsi、subset）の段落 5 行と JPEG 1 つ。2 頁: PNG（alpha つき）と、Type0/Identity-H/ToUnicode の
    subset の日本語の行。3 頁: `/Rotate 90` の page に文字と画像。
  - `edit-hard.pdf`: content が stream の配列で途中で分かれる、q/Q の釣り合わない（余る Q・閉じない q）、BT の中で終わる content [L3]、inline image、
    form の中の画像と文字、Type 3 の文字、clip の中の画像、text の rendering mode 7 の block [H2]、`'` と `"`、TJ の kerning、1 つの BT に複数行（Td・T*）、
    **block A の `TD` と、その後の別の block B の `T*`**（A の行を編集しても B がずれない [H1]）、語ごとの BT の行 [M9]、Tr 3 の OCR の層 [M9]、
    BT の中の BDC /ActualText と MCID [M10]、Separation と CMYK の色の行 [M3]、ToUnicode の無い Identity-H、filter の読めない stream を含む page [M5]、
    page 全体を包む form。
  - `edit-rotate.pdf` [M1]: /Rotate 0・90・180・270 の 4 page、crop box の原点のずれた page、拡大縮小した `cm` の中の `Do`。
  - CMYK の JPEG（拒まれること）[M15]、tRNS つき・壊れた CRC の PNG [L6]。
  - `edit-xref-stream.pdf`: `edit-basic.pdf` を `qpdf --object-streams=generate` にした物（xref stream・object stream の base）。
  - gs（`ps2pdf`）で作った subset の Type1C の文書 1 つ（実際の生成器の形）。
- **`host-edit.c`**（libpdf を host で build、WS079 の `host-pdf-update.c` に倣う）: 走査の物の数・種類・key・四辺形・抽出の文字列が期待どおり。各操作の後、
  (1) 保存した file の先頭が base の bytes と一致、(2) `qpdf --check` が通る、(3) `pdftotext` で新しい文字が出て消した文字が出ない、
  (4) **[M14]** 画素の比較は rasterizer ごとに別々: `pdftoppm` の編集の前と後、libpdf の render の編集の前と後を、それぞれ編集した物の四辺形を数 px
  膨らませた外で比べて差 0（pdftoppm と libpdf を互いに比べない。別の rasterizer なので一致しない）、(5) 開き直した editor の物の表と key が保存の前と同じ、
  (6) [M12] 保存した file を libpdf で描いた画素と preview の画素が一致（閾値は p005 で決める）。
- **安全**: 走査・ToUnicode の parser・PNG の chunk の parser・editor を ASan/UBSan で build し、design-pdf.md §4.3 の fuzz の corpus と上の試料を流す。
  editor の開閉の 1500 回の繰り返しと render、font の cache の数が増えないこと [H5][N6]。
- **Flate の開き直し [H6]**: 画像を入れて保存した文書を開き直し、Notes の文書（FOREIGN ではない）として開き、pen の stroke の content と ZNOT が無圧縮のまま。
- **subset**: fontTools で開ける、glyph の数・composite の参照・hmtx の幅・`name` の record が期待どおり、fvar・gvar が無い。
- **Notes の model**（`host-notes.c` を広げる）: 各操作の undo・redo・Reset、journal の回復（画像の record を含む）、**保存→画像の挿入→保存→編集→
  crash の回復で画像が残る [H4]**、ZNOT の major 2 の書き・読み、major 1 の Notes の扱い（古い decoder が major 2 を拒むこと）、key の合わない状態が
  1 つ有ると文書全体を新しい base として開くこと（状態を捨てない）[H3]、画像の読み戻し（JPEG・PNG_IDAT・RGBA の 3 種、複数頁の共有）[M6]、
  undo の上限での参照の数 [L7]、autosave の時間（10 頁）[M13]、回復の key の不一致で回復を失敗にし journal を残すこと [N8]、CHANGED の文書の
  REPLACE の page に挿入して保存・開き直しても消した stroke が戻らないこと [N2]、画像の読み戻せない page を REPLACE で残さないこと [N1]。

### 11.2 T1 の QEMU（p010）

AAT の image（`plan/tools/aat/config-amd64-aat.mk` を WS175 の `tests/` の config で使う）を QEMU の Venus で、下の 5 シナリオを 1 回の QEMU の起動でまとめて流す。
試験の PDF・PNG・JPEG は `make-edit-samples.py` が host で作り、`--file` の複写で image に入れるか SSH で `/tmp/aat-samples/` に置く（AGENTS.md の試験の image の規則）。
保存した PDF は SSH で host に取り、host の `qpdf --check`・`pdftotext`・`pdftoppm` で確かめる。

### 11.3 AAT のシナリオ（draft、`tests/scenarios/apps/notes/`）

| id | 内容 |
| --- | --- |
| `apps.notes.pdf-edit-image` | 画像の移動・大きさ・差し替え・削除と undo、保存して pdftoppm で確かめる |
| `apps.notes.pdf-insert-image` | PNG（alpha つき）と JPEG の挿入、保存と開き直し |
| `apps.notes.pdf-edit-text` | 既存の行の文字の編集（元の font のまま・置き換えの font になる場合）と削除、保存して pdftotext |
| `apps.notes.pdf-insert-text-font` | 文字の挿入、font の変更（Sans・Mono・Japanese）、IME の日本語、保存して pdftotext |
| `apps.notes.pdf-edit-multipage` | 3 page の文書の 1 頁と 3 頁（回転の page）を編集、pen の線を足し、保存・開き直しで編集と pen の線が残り、開き直した後は Reset で元に戻せる（undo の履歴は file に残らない [L10]） |

## 12. ユーザーの判断（推奨つき）

| # | 判断 | 選択肢 | 推奨 |
| --- | --- | --- | --- |
| D1 | 消した物の bytes が file に残ること | (a) 増分の更新だけ（残る。UI で一度だけ注意を出す）。(b) (a)＋「Save Clean Copy…」（全体の書き直しの copy、p009、+2.5 LW。消した画像・使わない resource は落ちるが、元の埋め込みの font の使われなくなった glyph は残る） | **(b)**。既定は安全な増分の更新、残したくない時だけ copy |
| D2 | 置き換え・挿入の font の種類 | (a) Inter・JetBrains Mono・Droid Sans Fallback の 3 つ。(b) serif の font（Noto Serif など、OFL）を system に足す | **(a)**。serif は Future Work（font の package の追加は別の判断）。併せて確認: Droid Sans Fallback の subset は Apache-2.0 の改変物なので、subset に license の record（name ID 13・14）と改変の notice（name ID 5 に「subset by Kei Notes」）を入れる。これで足りるか（§4.4） |
| D3 | 太字・斜体 | (a) 出さない。(b) 擬似（Tr 2 の太字・Tm の傾き）。(c) Inter の wght の instance 化 | **(a)**。依頼の範囲は「font を変える」まで |
| D4 | deflate の圧縮 | (a) libz-compat に deflate を足す（WS175 の p006、path の許可）。(b) libpdf の中だけ。(c) 無圧縮のまま | **(a)**。画像と content の大きさ、PNG の保存などにも使える |
| D5 | 文字の抽出（ToUnicode）の持ち主 | (a) WS175 の p002 が libpdf に作り、ws128-p004（PDF Viewer の検索）が後で使う。(b) ws128-p004 を先に | **(a)**。WS175 が先に要る。ws128-p004 は依存を p002 に付け替える |
| D6 | 出し方 | (a) 全部を 1 回に（17.5〜20 LW）。(b) 画像の編集を先に（約 10 LW、§10 の表の「先の段」）、文字を後 | 計画の段（ベータ2）の残りの量しだい。**(b)** を推奨: 画像だけでも単独で価値があり、文字の font の危険を後に分けられる |
| D7 | 既存の文字の編集の単位 | (a) 行ごと（段落の再配置なし）。(b) 段落の再配置 | **(a)**。(b) は Acrobat 相当で大きい |

技術の決定（ユーザーの判断は不要、Q1 の確認だけ）: ZNOT の major 2（§6.3）、画像の bytes を保存した ZNOT に入れず XObject の私的な key で読み戻す
（journal の snapshot には入れる、§6.3）、照合に外れた時は文書全体を新しい base として開く（§6.3）、文書全体の名前の接頭辞（§2.1）、圧縮する stream の
選び方（§5.2）、libpdf の公開の API の追加と `size` の field（§3.3）。
