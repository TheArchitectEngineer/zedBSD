# ws175-p001 設計: Notes で PDF の画像と文字を編集する

2026-10-06 P2（ws175-p001、設計だけ、product の code は書かない）。由来はユーザー（2026-10-06）の依頼（[WS175](../ws.md)）。
**注意（2026-10-06）: design-reviewer の指摘（high 6・medium 16・low 11）はまだこの文書に反映していない。[phase.md](phase.md) の「review」の表を先に読み、その修正を優先する（特に H1〜H6、M1 の行列の式、M16 の Phase の分け方）。**
この文書は WS175 の実装の Phase（p002 以降）の正本。WS079 の [design-pdf.md](../../ws079/design-pdf.md)（Notes の PDF・ZNOT・増分の更新）を前提にし、そこに書いてあることは繰り返さない。

## 0. 要約

- **保存は今の Notes と同じ「base の bytes はそのまま、Notes の revision を 1 つ足し、保存のたびにその revision を作り直す」増分の更新**にする。
  編集した page だけ `/Contents` と `/Resources` を新しい版にする。削除した物の bytes は base の revision に残るので、残したくない人のために
  「古い版を含めない copy の保存」（全体の書き直し）を別の Phase の選択肢にする（D1）。
- 編集の単位は **page の top level の content の「物」**: 文字の行（同じ BT…ET の中の、同じ baseline の show の演算子の集まり）、画像
  （Image XObject の `Do`、inline image `BI…EI`）、form XObject の `Do`（「図形」: 移動・大きさ・削除だけ）。form の中・path（線や塗り）は編集しない。
- content stream は **libpdf が解いて書き直す**。物の byte の範囲と、その時点の graphics state（CTM・text matrix・font・色）を interpreter が記録し、
  編集した物だけを置き換えた 1 本の新しい content stream を作る。文字の block は触った時に「各 show の前に明示の `Tm`」の形に正規化してから
  一部を消す・差し替える（後ろの文字の位置が変わらない）。
- 文字の書き換えは、**元の埋め込み font に要る glyph が全て有ればその font のまま**、無ければ **system の font（Inter・JetBrains Mono・
  Droid Sans Fallback）に置き換え**、利用者に知らせる。新しい font は Type0/CIDFontType2/Identity-H、glyph を詰めた subset、`/ToUnicode` つきで埋め込む。
  3 つの font とも埋め込みは license 上 可（§4.4）。
- 挿入・差し替えの画像は **PNG と JPEG**。JPEG は bytes のまま（DCTDecode）、PNG は RGB/Gray で alpha が無ければ IDAT をそのまま
  （FlateDecode と PNG の predictor）、それ以外は解いて RGB＋SMask。圧縮のために libz-compat に deflate を足す（D4）。
- Notes の model は **page ごとの「物の上書きの表」と「挿入した物の列」**（状態の表で、操作の log ではない）。undo は物の前後の状態の組、
  journal と ZNOT にも同じ符号化で入れる。編集がある文書の ZNOT は major 2（古い Notes は他の PDF として安全に開く）。
- UI: toolbar に **Select（編集）・Text・Image** の道具。選んだ物に枠と四隅の handle、その上に小さな操作の帯（Edit Text・Font・Replace・Delete）。
  文字の編集は page の上でその場の編集の box（caret、IME）。複数頁は今の Notes で対応済み（`<`・`3 / 12`・`>`・PageUp/PageDown・`+ Page`）で、
  各頁の編集は page ごとに持つ。
- 規模の見積もり: **約 15 LW（p008 を入れて 16.5 LW）**。WS の Q1 の概算 6 LW より大きい（§10）。

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

**採用: 既定は増分の更新**。「古い版を含めない copy の保存（Save Clean Copy…）」を別の Phase（p008）の選択肢にし、ユーザーが採るか決める（D1）。
clean copy は「今の見た目の PDF を新しい file に全体で書き、その file は Notes では他の PDF（編集は焼き込み済み）として開く」物で、元の file はそのまま。

### 2.1 編集した page の書き方

`update.c` に page の置き方 `PDF_WRITER_PLACE_EDIT` を足す:

- page の `/Contents` = `[編集した content の stream, 上に描く物の stream]`。1 本目は §3 で作る新しい stream（元の content を `q … Q` で包んだ物）、
  2 本目は挿入した物と pen の stroke（今の overlay と同じ書き方）。元の content の stream は参照されなくなる（bytes は base に残る）。
- page の `/Resources` = 元の resource（直接・間接・継承）に、新しい Font・XObject・ExtGState を merge した直接の dictionary。`write_merged_category()` に
  `Font` を足す。新しい名前は `Kei` の接頭辞に通し番号で、page の resource に既に在る名前は飛ばして決める（今の「EEXIST で拒む」では、Notes が以前に書いた
  `Kei…` が base に入っている文書を保存できない）。
- 編集の無い page は今のまま（KEEP・OVERLAY・REPLACE）。
- Notes が作った文書（base が無い、全体の書き出し）でも同じ「page の編集」を使う: base の物は無く、挿入した物だけ（§3.6）。

### 2.2 前提として拒むもの（今と同じ）

暗号化・署名の PDF は開いても編集・保存しない（今の Notes の規則）。`pdf_document_page_count()` が読めない・page が `PDF_DISPLAY_DAMAGED` の page は
編集の道具を出さない（pen は使える）。

## 3. content stream の物の取り出しと書き換え（libpdf）

### 3.1 走査（scanner）

interpreter（`content.c` の `run_content()`）に「走査の mode」を足し、page の content（`/Contents` の stream を decode して連結した物。reader と同じく
stream の間に改行を入れる）を 1 回走らせて、top level の物の表を作る。各演算子の token の開始と終了の byte の位置は lexer の `position` で分かる。

物の種類と記録する物:

| 種類 | 何が 1 つか | 記録 |
| --- | --- | --- |
| 文字の行 `TEXT` | 同じ BT…ET の中で、同じ font・同じ向きで、baseline が同じ（text 空間の y の差が font size の 1/4 以下）で、前の show の終わりとの隙間が 3 em 以下の show の演算子（Tj・TJ・'・"）の並び | 各 show の byte の範囲、その直前の Tm・Tlm・CTM・text state（Tf と size・Tc・Tw・Tz・TL・Tr・Ts）・塗りの色、glyph の箱から作った行の外接の四辺形（page の shown space）、Unicode の文字列（§3.2、分からない文字は U+FFFD）、font の名前と種類 |
| 画像 `IMAGE` | Image XObject の `Do` 1 つ、または inline image `BI…ID…EI` 1 つ | `Do` の名前の token から `Do` までの byte の範囲、その時の CTM、画像の幅と高さ（sample）、unit square を写した四辺形、clip の有無と clip の外接の箱 |
| 図形 `GRAPHIC` | Form XObject の `Do` 1 つ | 同じ（四辺形は form の `/BBox` を `/Matrix` と CTM で写した物） |

規則:

- 番号（ordinal）は content の中で物が現れた順。同じ base の同じ page なら同じ番号になる（決定的）。指紋（物の byte の範囲の SHA-256 の先頭 8 byte）も持つ（§6.3）。
- **編集しない（一覧に出さない）物**: form XObject の中の文字・画像（form は全体で 1 つの図形）、Type 3 の glyph の手続きの中、pattern の中、text の
  rendering mode 4〜7（clip になる文字）、縦書き（Identity-V・WMode 1）の文字の内容の変更（移動・削除は可）、`PDF_DISPLAY_DAMAGED` 以降の content。
- 一覧には出すが文字の内容を変えられない物: Unicode が分からない文字を含む行（移動・大きさ・削除・「全文を打ち直す」は可、§5.4）、Type 3 font の行。
- q/Q の釣り合い: 走査は q の深さを数える。最後に開いたままの q の数を記録し、空の stack の Q（interpreter は無視する）の byte の範囲も記録する（§3.4 で落とす）。

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

struct pdf_edit_object {	/* 1 つの物の今の姿（上書きの後） */
	enum pdf_edit_kind kind;
	unsigned flags;		/* DELETED・TEXT_KNOWN・TEXT_FIXED（内容を変えられない）・CLIPPED・INSERTED */
	double quad[8];		/* page の shown space（左上が原点、y が下向き、pt）の四隅 */
	const char *text;	/* UTF-8（TEXT）、分からない文字は U+FFFD */
	char font_name[64];	/* 元の /BaseFont（subset の接頭辞は除く）、または置き換えの font の名前 */
	double font_size;	/* shown space での見かけの大きさ（pt） */
	size_t image_width, image_height;
};

struct pdf_edit_text {		/* 文字の新しい内容 */
	const char *utf8;	/* '\n' は改行 */
	enum pdf_edit_font font;
	double size;		/* pt（shown space） */
	double red, green, blue;
	double box_width;	/* 折り返しの幅（pt）、0 は折り返さない */
};

struct pdf_image_source {	/* 挿入・差し替えの画像 */
	int kind;		/* JPEG（bytes のまま）・PNG_IDAT（predictor 付き deflate のまま）・RGBA（8bit、straight alpha） */
	const void *data; size_t size;
	size_t width, height; int components;
};

int  pdf_page_editor_open(struct pdf_document *document, size_t index, struct pdf_page_editor **editor);
int  pdf_page_editor_blank(double width, double height, struct pdf_page_editor **editor);
void pdf_page_editor_close(struct pdf_page_editor *editor);
size_t pdf_page_editor_count(const struct pdf_page_editor *editor);
int  pdf_page_editor_object(struct pdf_page_editor *editor, size_t index, struct pdf_edit_object *object);
int  pdf_page_editor_fingerprint(const struct pdf_page_editor *editor, size_t index, unsigned char digest[8]);
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
`set_text` の `result` は「元の font のまま書けた／置き換えの font になった（理由）」を返し、Notes がそれを利用者に知らせる。
`place` の変換は物の元の四辺形に対する shown space の affine（移動と拡大縮小。UI は回転を出さない）。

### 3.4 新しい content stream の組み立て

page の新しい content =

```text
q
<元の content。編集した物の byte の範囲だけを置き換え、空の stack の Q を落とした物>
<開いたままだった q の数だけ Q>
Q
<挿入した物、挿入した順>
```

物ごとの置き換え:

| 物・操作 | 元の byte の範囲を何に置き換えるか |
| --- | --- |
| 画像・図形の削除 | `Do` の名前と `Do`（inline image は `BI` から `EI` まで）を空に |
| 画像・図形の移動・大きさ | `q M cm /Name Do Q`。行列は PDF の行ベクトルの約束（点 p は p × M）で書く。M = P × B⁻¹ × C⁻¹（C は `Do` の時の CTM、B は page の user space → shown space の行列（`content.c` の `base_matrix()`、/Rotate と crop box を含む）、P は unit square → shown space の新しい置き方 = 元の置き方 C × B に shown space の変換 S を後ろから掛けた C × B × S）。cm は M × C を新しい CTM にするので、新しい実効の行列は M × C × B = P |
| 画像の差し替え | `q M cm /KeiImN Do Q`。M は「新しい画像の縦横比を保って元の四辺形の中に収め、中央に置く」置き方 P から上と同じ式で作る。古い XObject は使われなくなる（bytes は base に残る） |
| 文字の行（どの操作でも） | その行が属する BT…ET の block を**正規化**した上で、行の show の演算子を消し、新しい内容を出す（下） |

**文字の block の正規化**: block の中の位置の演算子（Td・TD・T*・Tm）を落とし、各 show の直前に、走査で記録したその show の直前の
`a b c d e f Tm` を出す。`'` と `"` は「`Tm`＋`Tj`」に直し、`"` の `aw ac` は `Tw`・`Tc` として先に出す。位置以外の演算子（Tf・Tc・Tw・Tz・TL・Ts・Tr・
色・gs など）は元の順のまま残す。これで block の中のどの show を消しても、他の show の位置は変わらない。Tlm（line matrix）は後の位置の演算子が
無くなるので使われない。

**文字の行の新しい内容**: 行の最初の show の位置に、block をいったん閉じて出す:

```text
ET
q  <色>  BT  /F size Tf  <Tc Tw Tz Ts Tr の元の値か 0>  a b c d e f Tm  <code の列> Tj  [ 0 -leading Td  <次の行> Tj … ]  ET  Q
BT
```

閉じた後の `BT` は Tm を単位行列に戻すが、正規化で後ろの show は全て明示の `Tm` を持つので影響しない。text state は graphics state の一部なので
`q … Q` が元に戻す。行の残りの show（2 つ目以降）は消す。移動・大きさだけの時は、元の font と code の列をそのまま使い `Tm` だけを変える
（Tm' = Tm × C × B × S × B⁻¹ × C⁻¹。S は shown space の変換。C はその行の時の CTM）。

**文字の内容の変更の font**: 新しい文字列の各文字を、元の font の「Unicode → code」の逆引き（§3.2 の表の逆）で code にし、その code の glyph が元の
埋め込みの program に在るか（`pdf_font_glyph()` が描ける、幅が有る）を確かめる。全部在れば元の font・元の size で書く（見た目が保たれる）。
1 つでも無ければ（subset に無い文字が典型）、行全体を置き換えの font（§4）にし、`result` で知らせる。利用者が font を選んだ時は常に置き換えの font。
1 行に元は複数の font が混ざっていた（太字の語など）場合、編集した行は最初の show の font の 1 つにまとまる（制限、§9）。

### 3.5 見かけの確認（preview）

Notes の画面は editor の `pdf_page_editor_render()`（上の新しい content と merge した resource を、今の interpreter にそのまま走らせる）で背景を描く。
新しい font と画像の resource は editor の arena の中の object（stream の bytes は `pdf_object.bytes` に置く。inline image と同じ仕組み）として作り、
file の object と同じに interpreter に渡す。preview では font の subset は作らず、system の font の file 全体を `FontFile2` として渡す（subset は保存の時だけ）。
これで画面と保存した PDF が同じ interpreter・同じ content で描かれる。

### 3.6 挿入した物

- 文字: `q <色> BT /KeiFn size Tf a b c d e f Tm <行> Tj … ET Q`。折り返し（§5.4）は libpdf が font の advance で行に分ける。
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
| Droid Sans Fallback | Apache-2.0、OS/2 fsType 8（Editable embedding） | 可。fsType 8 は「埋め込んだ文書を編集してよい」で、Notes の用途に合う | Apache-2.0 の notice を保つため、`name` の著作権（ID 0）と license（ID 13・14）の record を subset に残す |

一般の規則（将来 font を足した時も効くように writer が守る）: OS/2 の fsType を読み、`0x0002`（Restricted License embedding）の font は埋め込まない
（その font を選べなくする）。`0x0100`（No subsetting）なら subset にせず全体を埋め込む。`0x0200`（Bitmap embedding only）の font は使わない。
元の PDF に埋め込みの font（§3.4 の「元の font のまま」）は、その PDF に既に入っている program を使うだけで、Notes は新しく埋め込まない。

## 5. 画像

### 5.1 受ける形式

**PNG と JPEG**（Q1 の指定どおり）。GIF・WebP・HEIC は範囲外（Image Viewer が開ける GIF は後で足せる）。

| 入力 | PDF での書き方 |
| --- | --- |
| JPEG（baseline・progressive、Gray・RGB・CMYK） | bytes をそのまま `/DCTDecode`（今の `pdf_writer_draw_jpeg_image()`）。EXIF の向きは画素を回さず、置き方の行列で回す（`picture/` の `kl_picture_exif_orientation()` で向きを読む） |
| PNG（8bit の Gray・RGB、alpha なし、interlace なし） | IDAT を連結した bytes をそのまま `/FlateDecode /DecodeParms << /Predictor 15 /Colors n /BitsPerComponent 8 /Columns w >>`（PNG の行の filter は PDF の predictor と同じ）。解き直しも圧縮し直しも要らない。reader は predictor を読む（`filter.c`） |
| PNG（それ以外: alpha・palette・16bit・1/2/4bit・interlace） | libpng-compat で 8bit の RGBA に解き、RGB と alpha（`/SMask`）に分けて deflate で圧縮（D4。deflate が無ければ無圧縮） |

上限: 一辺 16384 sample（reader の `PDF_IMAGE_SIDE_MAX` と同じ）、画素の数 64 M。超える画像は「大きすぎる」と知らせて受けない（縮小して入れるのは範囲外）。

### 5.2 deflate（D4）

今の libpdf は stream を無圧縮で書く。挿入の画像（alpha つきの PNG、4000×3000 で約 48 MB）・編集した content（元が数 MB の page もある）・ZNOT が膨らむ。
**libz-compat に deflate（`compress2`・`deflateInit`/`deflate`/`deflateEnd` の最小）を足す**ことを推奨する: LZ77（hash chain、窓 32 KB）と固定・動的 Huffman。
WS175 の所有の path の外（`userland/base/libz-compat`）なので、Q1 の割当と path の許可が要る。代案は libpdf の中だけの deflate（他から使えない）か、無圧縮のまま（大きい）。

### 5.3 挿入の置き方

挿入した画像は、page の見えている範囲の中央に、page の幅の 1/2 と高さの 1/2 に収まる大きさ（縦横比を保つ。元の画像の 72 dpi の大きさより大きくはしない）で置く。

## 6. Notes の model・undo・journal・保存

### 6.1 model

`notes.h` に足す（名前は p006 で確定）:

```c
struct notes_image {		/* 挿入・差し替えの画像の bytes。複数の状態から参照されるので参照の数を持つ */
	uint32_t id; unsigned refs;
	unsigned kind;		/* JPEG・PNG_IDAT・RGBA */
	unsigned char *data; size_t size; size_t width, height; int components; int orientation;
};

struct notes_object_state {	/* 1 つの物の上書きの状態（元の物）か、挿入した物の状態 */
	uint32_t key;		/* 元の物: ordinal、挿入した物: Notes の id（stroke と同じ next_id から） */
	uint8_t fingerprint[8];	/* 元の物の指紋（§6.3） */
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

`NOTES_UNDO_EDIT_OBJECT` を足す: 1 つの entry = (page, key, 前の状態, 後の状態)。前の状態が「無い」のは挿入、後の状態が「無い」のは
元の物の上書きの取り消し・挿入した物の削除。undo は前の状態に戻して editor の該当の物を `reset` してから前の状態を適用し直す。

entry の単位（利用者の操作 1 つ = undo 1 つ）: 移動・大きさの drag の終わり、文字の編集の box を閉じた時（box の中の打鍵は box の中の undo、§7.4）、
font・size の変更、差し替え、挿入、削除。今の `NOTES_UNDO_LIMIT`（1000）の中に入る。画像の bytes は `notes_image` の参照の数で共有する（undo の entry は複製しない）。

### 6.3 ZNOT（編集の data）と journal

- 新しい chunk `EDIT`: page 番号 varint、状態の数 varint、各状態 = key varint・指紋 8 byte・flags u8・変換（1/64 pt と 1/65536 の固定小数）・
  文字（UTF-8 の長さ varint と bytes、font u8、size、色 RGBA、折り返しの幅）・画像の id varint。
- 新しい chunk `IMAG`: 画像の id、種類、幅・高さ・成分、向き、**PDF の中の XObject を指す印**（下）。画像の bytes は ZNOT に入れない（PDF の XObject と二重になる）。
  保存は画像の XObject の dictionary に私的な key `/KeiNotesImage <id>` を書き、開く時は page の resource の XObject を辿ってその key の stream の
  bytes（filter のかかったまま）を読み戻す。key が見つからない画像を使う状態は捨て、利用者に知らせる。
- **版**: `EDIT` を含む ZNOT は **major 2**。今の Notes（major 1 だけ読む）は major 2 を読めない時、design-pdf.md §3 の規則で「他の PDF」として開く
  （編集を焼き込んだ見た目が base になる）ので、古い Notes が保存しても編集は失われない。編集の無い文書は今の 1.x のまま書く。
- 開く時の照合: base の bytes の SHA-256（今の `base_hash`）が合い、各状態の指紋が editor の同じ ordinal の物と合う時だけ適用する。合わない状態は捨て、
  「N 個の編集を戻せなかった」と知らせる（base を他の tool が変えた時。今の pen の規則と同じ考え）。
- journal: 状態の変更 1 つを `EDIT` と同じ符号化で 1 record に。新しい画像は、最初に使った時に bytes ごと 1 record（`IMAG`＋bytes）で書く
  （保存の前に落ちても戻せる）。journal の回復（`notes_journal_recover()`）は record を順に適用する。

### 6.4 保存

- base の在る文書（`save_update()`）: 編集の在る page は `pdf_writer_begin_page_edited()`（§2.1）、その上に pen の stroke。編集の無い page は今のまま。
- Notes が作った文書（`save_whole()`）: 挿入した物の在る page は `pdf_page_editor_blank()` の editor から同じく書く。
- 文字に使った system の font は writer が文書全体で集めて、保存の終わりに font ごとに 1 つの subset を作る。
- autosave（変更の 5 秒後）は今のまま。文字の編集の box が開いている間は、その box の内容は保存に入れない（閉じた時に 1 つの変更になる）。

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
| 指 | 今の指の規則（スクロール・ズーム）を保つ。1 本の指の tap が選択、選んだ物の上の 1 本の指の drag が移動 | tap で box |
| key | Delete・Backspace で削除、矢印で 1 pt（Shift で 10 pt）移動、Esc で選択を外す、Ctrl+Z/Ctrl+Shift+Z | box の中は §7.4 |

### 7.2 選択と handle

- 選んだ物の四辺形を青い線（`#2f7cf6` 系、theme の accent）で描き、四隅に 8 px の四角の handle。
- 画像・図形の handle の drag は縦横比を保つ（Shift で自由）。文字の行の handle は等倍の拡大縮小（= font size の変更）。最小 4 pt、最大 page の 4 倍。
- 選んだ物の上（page の上端に近い時は下）に小さな操作の帯: 文字は **Edit Text・Font・Size・Delete**、画像は **Replace・Delete**、図形は **Delete**。
- clip の中の画像（`CLIPPED`）を動かすと clip の外は見えない。選んだ時に clip の箱を点線で描き、帯に「Clipped」と出す（clip を解くのは範囲外）。
- 編集できない物（form の中・path など）は選べない（click は何も選ばない）。内容を変えられない行は Edit Text を灰色にし、押すと理由を出す。

### 7.3 描画

- 背景は今の `NOTES_TEXTURE_BACKGROUND`。編集の在る page は editor の `render()` から描く。
- 移動・大きさの drag の間は、背景を「その物を除いて」1 回描き直し、物だけの display list（`render_object()`）を 1 回画素にして新しい texture
  （`NOTES_TEXTURE_OBJECT`）にし、GPU で変換して描く（drag の度に page 全体を CPU で描き直さない）。drag の終わりで背景を描き直す。
- 文字の編集の box の間も同じく、背景は元の行を除き、box の中身は打鍵ごとに box の物だけを描き直す。

### 7.4 文字の編集の box

- page の上のその場の編集: 行の位置に、置き換えの後の font・size で文字を描き（preview は libpdf、§3.5）、caret（1 px の縦線、点滅）と選択の範囲を Notes が重ねる。
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

`NOTES TOOL select|text|image`、`NOTES EDIT select page=P object=I kind=text|image|graphic text="…"`（文字は先頭 40 byte）、
`NOTES EDIT move page=P object=I dx= dy=`、`NOTES EDIT resize page=P object=I sx= sy=`、`NOTES EDIT text page=P object=I chars=N font=original|sans|mono|cjk fallback=0|1`、
`NOTES EDIT font page=P object=I font=…`、`NOTES EDIT replace page=P object=I image=W×H`、`NOTES EDIT insert page=P kind=text|image object=I`、
`NOTES EDIT delete page=P object=I`、`NOTES EDIT undo|redo page=P object=I`、`NOTES SAVE … edits=N edited_pages=M`、`NOTES OPENED … edits=N dropped=K`。
既存の `NOTES SAVE`・`NOTES OPENED` の行は変えず、項目を後ろに足す（今の試験の pattern を壊さない）。

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
- 増分の更新では消した物の bytes が file に残る（D1 の clean copy を採らない限り）。

## 10. Phase の分け方と見積もり

| Phase | 内容 | 所有 path | 依存 | LW |
| --- | --- | --- | --- | --- |
| p002 | libpdf: 走査（物の表・byte の範囲・graphics state・四辺形・指紋・hit）と文字の Unicode（ToUnicode・encoding・cmap の逆引き）。host 試験 | `userland/base/libpdf`、`include/libc/pdf.h` | p001 | 2.5 |
| p003 | libpdf: editor の適用（削除・移動・大きさ・画像の差し替えと挿入・元の font での文字の書き換え）、content の組み立て（正規化・q/Q）、preview の render、`PLACE_EDIT` と Font の merge・名前の衝突の回避、全体の書き出しでの editor。host 試験（qpdf・pdftoppm・pdftotext） | 同上 | p002 | 2.5 |
| p004 | libpdf: TrueType の subset（composite・variable の table を落とす・name を残す・fsType）、Type0/CIDFontType2 の埋め込みと ToUnicode、system の font の選択と文字ごとの fallback、行の折り返し、文字の挿入・font の変更。host 試験（fontTools・pdftotext） | 同上 | p003 | 2.5 |
| p005 | libz-compat の deflate（D4）、libpdf の stream の圧縮、画像の取り込み（JPEG の向き・PNG の IDAT の素通し・その他の PNG の RGBA＋SMask） | `userland/base/libz-compat`（Q1 の許可）、`userland/base/libpdf` | D4（p002〜p004 と並列可。libpdf の file は p003 と重なるので順に） | 1 |
| p006 | Notes の model: 物の状態・undo・journal・ZNOT major 2（EDIT・IMAG）・保存（update と全体）・開く時の再適用と照合・画像の読み戻し。host 試験（`host-notes.c` を広げる） | `userland/desktop/notes/`（model 側: notes.h・document.c・encode.c・journal.c・save.c） | p003 の API（p004・p005 と並列可） | 2 |
| p007 | Notes の UI: 道具・選択・handle・操作の帯・drag の描画・文字の編集の box と IME・font の picker・画像の file chooser・log。AAT の draft の手直し | `userland/desktop/notes/`（main.c・ui.c・render.c・window.c・menu.c・touch.c） | p004・p005・p006 | 3 |
| p008 | （D1 で採れば）Save Clean Copy: 読んだ文書を今の見た目で全体に書き直す writer（catalog から辿れる object の複写・番号の付け直し・object stream の展開・古い版を落とす） | `userland/base/libpdf`、Notes の menu | p003 | 1.5 |
| p009 | T1 の QEMU（Venus）で AAT の 5 シナリオと Notes の既存の回帰、FAIL の直し 1 回分。シナリオを active に | — | p007（p008） | 1 |
| p010 | 規約の全文の見直し（WS の変えた C の code 全部） | — | p009 | 0.5 |

合計 **15 LW**（p008 を入れると **16.5 LW**）、±50%。Q1 の概算 6 LW より大きい主な理由: 文字の抽出（ws128-p004 と共通）、font の subset と埋め込み、
content の書き換えの正規化、Notes の UI（文字の編集の box と IME）がそれぞれ独立に重い。ベータ2（WS の Status の案）に合わせるなら、
「画像の編集だけ（p002 の画像の部分・p003・p005・p006・p007 の画像の部分）」を先に出す 2 段の出し方もできる（D6）。

## 11. 試験

### 11.1 host（各 Phase、実装の担当）

置き場所は `plan/ws175/tests/`。

- **`make-edit-samples.py`**（p002 で作る。fontTools と python の zlib。WS079 の `make-text-pdfs.py` に倣い、host の Liberation・DejaVu・Droid の font を使う）:
  - `edit-basic.pdf`: 3 page。1 頁: 埋め込みの TrueType（WinAnsi、subset）の段落 5 行と JPEG 1 つ。2 頁: PNG（alpha つき）と、Type0/Identity-H/ToUnicode の
    subset の日本語の行。3 頁: `/Rotate 90` の page に文字と画像。
  - `edit-hard.pdf`: content が stream の配列で途中で分かれる、q/Q の釣り合わない（余る Q・閉じない q）、inline image、form の中の画像と文字、Type 3 の文字、
    clip の中の画像、text の rendering mode 7、`'` と `"`、TJ の kerning、1 つの BT に複数行（Td・T*）、ToUnicode の無い Identity-H。
  - `edit-xref-stream.pdf`: `edit-basic.pdf` を `qpdf --object-streams=generate` にした物（xref stream・object stream の base）。
  - gs（`ps2pdf`）で作った subset の Type1C の文書 1 つ（実際の生成器の形）。
- **`host-edit.c`**（libpdf を host で build、WS079 の `host-pdf-update.c` に倣う）: 走査の物の数・種類・四辺形・抽出の文字列が期待どおり。各操作の後、
  (1) 保存した file の先頭が base の bytes と一致、(2) `qpdf --check` が通る、(3) `pdftotext` で新しい文字が出て消した文字が出ない、
  (4) `pdftoppm` と libpdf の render の画素の差が編集した物の四辺形の外で 0（許容の差の閾値は p003 で決める）、(5) 開き直した editor の物の表が保存の前と同じ。
- **subset**: fontTools で開ける、glyph の数・composite の参照・hmtx の幅・`name` の record が期待どおり、fvar・gvar が無い。
- **Notes の model**（`host-notes.c` を広げる）: 各操作の undo・redo、journal の回復（画像の record を含む）、ZNOT の major 2 の書き・読み、major 1 の Notes の
  扱い（古い decoder が major 2 を拒むこと）、指紋の合わない状態を捨てること、画像の読み戻し。

### 11.2 T1 の QEMU（p009）

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
| `apps.notes.pdf-edit-multipage` | 3 page の文書の 1 頁と 3 頁（回転の page）を編集、pen の線を足し、保存・開き直しで編集が戻せる（undo できる）・pen の線が残る |

## 12. ユーザーの判断（推奨つき）

| # | 判断 | 選択肢 | 推奨 |
| --- | --- | --- | --- |
| D1 | 消した物の bytes が file に残ること | (a) 増分の更新だけ（残る。UI で一度だけ注意を出す）。(b) (a)＋「Save Clean Copy…」（全体の書き直しの copy、p008、+1.5 LW） | **(b)**。既定は安全な増分の更新、残したくない時だけ copy |
| D2 | 置き換え・挿入の font の種類 | (a) Inter・JetBrains Mono・Droid Sans Fallback の 3 つ。(b) serif の font（Noto Serif など、OFL）を system に足す | **(a)**。serif は Future Work（font の package の追加は別の判断） |
| D3 | 太字・斜体 | (a) 出さない。(b) 擬似（Tr 2 の太字・Tm の傾き）。(c) Inter の wght の instance 化 | **(a)**。依頼の範囲は「font を変える」まで |
| D4 | deflate の圧縮 | (a) libz-compat に deflate を足す（WS175 の p005、path の許可）。(b) libpdf の中だけ。(c) 無圧縮のまま | **(a)**。画像と content の大きさ、PNG の保存などにも使える |
| D5 | 文字の抽出（ToUnicode）の持ち主 | (a) WS175 の p002 が libpdf に作り、ws128-p004（PDF Viewer の検索）が後で使う。(b) ws128-p004 を先に | **(a)**。WS175 が先に要る。ws128-p004 は依存を p002 に付け替える |
| D6 | 出し方 | (a) 全部を 1 回に（15〜16.5 LW）。(b) 画像の編集を先に（約 8 LW）、文字を後 | 計画の段（ベータ2）の残りの量しだい。**(b)** を推奨: 画像だけでも単独で価値があり、文字の font の危険を後に分けられる |
| D7 | 既存の文字の編集の単位 | (a) 行ごと（段落の再配置なし）。(b) 段落の再配置 | **(a)**。(b) は Acrobat 相当で大きい |

技術の決定（ユーザーの判断は不要、Q1 の確認だけ）: ZNOT の major 2（§6.3）、画像の bytes を ZNOT に入れず XObject の私的な key で読み戻す（§6.3）、
`Kei` の名前の衝突を飛ばして決める（§2.1）、libpdf の公開の API の追加（§3.3）。
