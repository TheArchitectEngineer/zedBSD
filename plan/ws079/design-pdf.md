# WS079 設計: PDF の形式・libpdf・PDF Viewer の段階

ws079-p001 の設計のうち PDF の部分。pen の入力・gesture・Notes の UI と文書 model は
[design-input-notes.md](design-input-notes.md) にあり、ここでは繰り返さない（stroke の点の意味・道具の種類はそちらが正本）。

## 1. Notes が保存する PDF

- **PDF 1.7**、classic の xref table（xref stream・object stream は使わない。どの viewer でも読め、libpdf の段階 ① の読み込みが小さくて済む）。
  header の 2 行目に 4 byte の binary の comment（`%\xE2\xE3\xCF\xD3`）。trailer に `/ID`（初回の保存で作り、上書きの保存でも第 1 要素を保つ）。
- 座標: Notes の model は page の左上が原点・y が下向き・単位 pt。content stream の先頭で `1 0 0 -1 0 H cm` を置き、以後は model の座標をそのまま書く。
  page の大きさは `/MediaBox [0 0 W H]`（既定 A4、595.276 x 841.890 pt）。
- **stroke は塗りつぶしの path**（線の `S` は使わない）。筆圧で太さが変わる線は PDF の線幅（page 全体で一定）で表せないため、Notes が
  model から作る輪郭（各点の法線方向に ±幅/2 を取った左右の辺と、両端の丸い cap、点の間の曲がりは round join 相当）を
  `m`・`l`・`c` と `h` で一つの閉じた path にして `f`（nonzero）で塗る。一つの stroke を一回の fill にするので、自己交差しても半透明が二重にならない。
  輪郭の計算は Notes と libpdf の writer の間で共有する（libpdf に `pdf_outline_stroke()` を置き、Notes の画面の描画も同じ輪郭を使う。
  画面と PDF で形が変わらない）。
- **色と透明**: `r g b rg`（DeviceRGB）。不透明でない道具（蛍光ペンなど）は page の `/Resources /ExtGState` に
  `<< /Type /ExtGState /ca a >>`（必要なら `/BM /Multiply`）を置き、`/GSn gs` で選ぶ。同じ (ca, BM) の組は一つの ExtGState を共有する。
  各 stroke は `q … Q` で囲まない（gs と rg を必要なときだけ出し、content を小さくする）。
- 消しゴムは model の上で stroke を消す・分ける操作なので、PDF には白の塗りも「消す」命令も出ない。
- 画像（ws.md p004 の範囲）: JPEG は bytes をそのまま `/DCTDecode` の Image XObject に、それ以外は 8bit RGB（alpha は `/SMask` の 8bit Gray）。
- stream は初めは無圧縮（libz-compat は inflate だけで deflate が無い）。libz-compat に deflate が入ったら content・編集 data を `/FlateDecode` に
  する。読む側は両方を受ける。
- `/Info` に `/Producer (zedBSD Notes)`・`/CreationDate`・`/ModDate`。

## 2. 詳細な編集 data の置き場所

候補の比較:

| 方式 | 利点 | 欠点 |
| --- | --- | --- |
| **埋め込みの file の stream**（catalog の `/Names /EmbeddedFiles` と `/AF`） | PDF 1.7 の標準の仕組み。任意の binary を stream の filter つきで持てる。qpdf・pdftk・多くの編集 tool が保つ。`/Params /Size /CheckSum /ModDate` がある。一つの object にまとまり、読むのが容易 | 他の viewer の添付の一覧に見える（中身は無害な binary） |
| page の `/PieceInfo` | page ごとの私的な dictionary（PDF 1.3〜）で、他の viewer に見えない | 仕様上、`/LastModified` が page の変更より古いと中身は無効とみなされ、編集 tool が捨ててよい。page 間の共通 data（道具の一覧など）の置き場が無い。大きな binary は結局 stream になる |
| XMP（`/Metadata`） | 文書の metadata の標準 | XML の text で、binary は base64 で 1.33 倍に膨らむ。多くの tool が XMP を書き直す・捨てる。検索・表示用の metadata の意味と合わない |

**採用: 埋め込みの file の stream**。一つの stream（`/Type /EmbeddedFile /Subtype /application#2Fx-zedbsd-notes`）を、file 名
`zedbsd-notes.bin` の file specification（`/Type /Filespec /F … /UF … /Desc (zedBSD Notes edit data) /AFRelationship /Source /EF << /F n 0 R >>`）
から指し、catalog の `/Names << /EmbeddedFiles << /Names [(zedbsd-notes.bin) spec] >> >>` と catalog の `/AF [spec]` の両方から引く。
`/AF` と `/AFRelationship` は PDF 2.0（PDF/A-3）の key だが、1.7 の reader は知らない key を無視するので害が無く、PDF/A-3 の道を残す。
Notes は `/AF` → `/EmbeddedFiles` の順に探し、subtype と file 名の両方が合う stream を使う。

### 2.1 編集 data の符号化（版 1）

little-endian の binary。可変長の整数は LEB128（符号つきは zigzag）。chunk 形式で、知らない chunk は長さで飛ばす（将来の版の追加を古い Notes が読める）。

```text
header: magic "ZNOT" (4) | major u16 = 1 | minor u16 = 0 | flags u32 = 0
chunk:  tag 4 文字 | length u32（本体の byte 数）| 本体
```

| chunk | 本体 |
| --- | --- |
| `DOC ` | page の数 varint、筆圧の最大値 u16（装置の値、例 4095）、時刻の基準（UNIX 時刻の ms、u64）、次の stroke の id varint |
| `TOOL` | 道具の表: 数 varint、各道具 = 種類 u8（pen・蛍光ペン・…、design-input-notes.md の定義）、色 RGBA 4 byte、基本の幅（1/64 pt、varint）、筆圧から幅への曲線の id u8、flags u8 |
| `PAGE` | page 番号 varint、幅・高さ（1/64 pt、varint）、背景の種類 u8（無地・罫線・方眼・元の PDF の page）、その page の content stream の SHA-256（32 byte、§3）、stroke の数 varint、続けて stroke |
| stroke（`PAGE` の中） | id varint、道具の index varint、flags u8（傾きの有無・時刻の有無）、点の数 varint、点の列 |
| 点 | x・y（1/64 pt、前の点との差の zigzag varint）、筆圧（0〜最大値、前との差の zigzag varint）、傾き x・y（0.01 度、差の zigzag varint、flags で有るときだけ）、時刻（前の点からの ms、varint、先頭の点は時刻の基準からの差） |

差分の varint で、1 点はおよそ 5〜8 byte。page の stroke が 2000・点が平均 100 なら約 1.2 MB（deflate が入れば数分の一）。
版の規則: minor の増加は chunk の追加だけ（古い Notes は飛ばす）。major が読めない値なら Notes は編集 data を使わず §3 の「他の PDF」として開く。

## 3. round-trip の規則

- **Notes が自分の file を開く**: 編集 data を読み、各 `PAGE` の SHA-256 を、PDF の同じ page の content stream（decode した bytes を
  `/Contents` の順に連結したもの）と比べる。全 page が一致すれば、編集 data から model を作り直して編集を続ける（PDF の path は読まない）。
- **一致しない page**（他の tool が page を書き換えた）・**編集 data が無い・壊れている・major が新しい**: その page は「元の PDF の page」を背景
  （libpdf の reader の display list）にし、その上に新しい stroke を足す。古い stroke を黙って編集可能にはしない。Notes は利用者にその旨を示す。
- **他の viewer**: 編集 data を知らなくても、page は普通の塗りの path として正しく表示・印刷される。添付の一覧に `zedbsd-notes.bin` が見える。
- **保存**: Notes が最初から作った文書は毎回全体を書き直す（小さく、xref が単純）。他の PDF（PDF Viewer の「書き込む」で開いたもの）は
  **増分更新**（元の bytes の後ろに、新しい content stream・`/Contents` を配列にした page・編集 data・新しい xref と `/Prev` を持つ trailer を
  足す）で保存し、元の内容（text・font・署名の前の版）を壊さない。増分更新の writer は p005 以降（p004 の範囲外）。
- 暗号化された PDF（段階 ③ 以前）・署名つきの PDF への書き込みは、読めるようになるまで拒む。

## 4. libpdf の構成

`userland/base/libpdf`、`/lib/libpdf.so`、公開の header は **`include/libc/pdf.h`**。
理由: `include/libc/compat/` は外の API を再実装した library（jpeglib・png・zlib・gif_lib）の場所で、zedBSD 自身の API の base の library は
`include/libc/truetype.h`・`include/libc/keiland.h` のように `include/libc/` の直下に一つの header を置いている。libpdf は自前の API なので後者に従う。
Wayland・Vulkan に依らず、依存は libc・libz-compat・libjpeg-compat（段階 ② で libtruetype）だけ。

| 部分 | file（案） | 内容 |
| --- | --- | --- |
| writer | `writer.c`・`outline.c` | 文書・page・path・色・ExtGState・画像・埋め込み file・xref・trailer の書き出し。`pdf_outline_stroke()`（筆圧の点列 → 輪郭の path） |
| lexer | `lexer.c` | token（数・名前の `#xx`・文字列の literal/hex・配列・dictionary・keyword・参照 `n g R`）。深さ・長さの上限 |
| object | `object.c` | 型つきの object（null・bool・int・real・name・string・array・dict・stream・ref）。文書ごとの arena に確保し、閉じるときにまとめて解放 |
| xref | `xref.c` | `startxref`、classic の table、xref stream（`/W /Index`）、`/Prev` の連鎖、hybrid の `/XRefStm`、object stream（`/N /First`）。段階 ② で壊れた xref の走査による修復 |
| filter | `filter.c` | FlateDecode（libz-compat の inflate）と predictor（PNG 10〜15、TIFF 2）、ASCIIHex・ASCII85・RunLength・LZW（段階 ②）、DCTDecode（libjpeg-compat の `jpeg_mem_src`）。decode 後の大きさの上限（既定 256 MiB）で bomb を止める |
| page tree | `page.c` | `/Pages` の木、継承する `/Resources /MediaBox /CropBox /Rotate`、page の数と index の表。循環の検出 |
| content の解釈 | `content.c`・`gstate.c` | graphics state の stack（CTM・色・alpha・線幅・cap・join・dash・clip）、path の構築と塗り（`m l c v y re h f f* S s B B* n W W*`）、色空間（DeviceGray/RGB/CMYK、ICCBased は N の代替、Indexed）、`gs`（ca・CA・BM）、`Do`（Image・Form）、inline image（`BI ID EI`）、段階 ② の text の演算子。線の塗り（`S`）は libpdf の stroker が fill の path に変える |
| font | `font.c`（②）・`cff.c`・`type1.c`（③） | ②: 埋め込み TrueType（`/FontFile2`）を libtruetype で、simple font の encoding（WinAnsi・MacRoman・`/Differences`）、Type0 + CIDFontType2（Identity-H、`/CIDToGIDMap`）、埋め込みの無い標準 14 font はシステムの TrueType で代用。③: CFF（`/FontFile3`）・Type1（eexec）・Type3 |
| display list | `display.c` | page の描画の結果。app はこれを描く |

### 4.1 display list

page の座標（pt、左上原点・y 下向き、`/Rotate` と CropBox を適用済み）で、順に描く item の配列:

- `PDF_ITEM_FILL`: path（move・line・cubic の列、変換済みの座標）、塗りの規則（nonzero・even-odd）、色 RGBA（float）、blend mode。
  線の `S` も stroker で fill にして入れる（app は塗りだけを実装すればよい）。
- `PDF_ITEM_IMAGE`: RGBA8 の画素、幅・高さ、単位正方形から page への変換行列、alpha、補間の要否。
- `PDF_ITEM_CLIP_PUSH`（path と規則）・`PDF_ITEM_CLIP_POP`。
- `PDF_ITEM_GLYPHS`（段階 ②）: glyph の輪郭の path の塗りとして入れる。libtruetype の公開 API には今 glyph の輪郭を返す関数が無い
  （`truetype_render_glyph` は bitmap）。**p007 の前に決めること**: libtruetype に輪郭の API を足すか、libpdf が bitmap の item を持つか。

display list は `flags` に「読めなかったもの（未対応の font・filter・shading）を飛ばした」を持ち、viewer が注意を出せる。
Bézier の平坦化は `pdf_path_flatten(tolerance)` を用意する（app の Vulkan の塗りは browser の `paint/vulkan.c` と同じ方式を p006 で選ぶ）。

### 4.2 API（案、`include/libc/pdf.h`）

```c
/* 書き出し（p004） */
int pdf_writer_create(struct pdf_writer **writer);
int pdf_writer_begin_page(struct pdf_writer *writer, double width, double height);
int pdf_writer_set_fill_color(struct pdf_writer *writer, double red, double green, double blue, double alpha);
int pdf_writer_move_to(struct pdf_writer *writer, double x, double y);   /* line_to・curve_to・close_path も同形 */
int pdf_writer_fill(struct pdf_writer *writer, enum pdf_fill_rule rule);
int pdf_writer_end_page(struct pdf_writer *writer);
int pdf_writer_attach_file(struct pdf_writer *writer, const char *name, const char *mime_type, const void *data, size_t size);
int pdf_writer_save(struct pdf_writer *writer, const char *path);
void pdf_writer_destroy(struct pdf_writer *writer);

/* 読み込み（p004 は自分の形式、p006 で段階 ①、p007・p008 で ②・③） */
int pdf_document_open(const char *path, struct pdf_document **document);
int pdf_document_open_memory(const void *data, size_t size, struct pdf_document **document);
size_t pdf_document_page_count(const struct pdf_document *document);
int pdf_document_page_box(struct pdf_document *document, size_t index, struct pdf_page_box *box);
int pdf_document_find_attachment(struct pdf_document *document, const char *name, const void **data, size_t *size);
int pdf_document_page_content_hash(struct pdf_document *document, size_t index, unsigned char digest[32]);
int pdf_page_render(struct pdf_document *document, size_t index, struct pdf_display_list **list);
void pdf_display_list_destroy(struct pdf_display_list *list);
void pdf_document_close(struct pdf_document *document);
```

返り値は 0 か errno の値（`ENOMEM`・`EINVAL`・`EIO`、形式の誤りは `EFTYPE` 相当の値を header で定める）。thread の安全: 一つの document を
複数の thread で同時に使わない（viewer は page の描画を一つの worker で行う）。

### 4.3 安全

入力は信頼しない。整数の overflow の検査（画像の幅 x 高さ x 成分）、object の参照・Form XObject の入れ子・page tree の深さの上限（例 32）、
循環の検出、decode の上限、page ごとの演算子の数の上限。host の試験は ASan/UBSan で、段階 ② から壊れた PDF の fuzz の corpus を回す。

## 5. PDF Viewer の段階

| 段階 | 読む範囲 | Viewer | Phase |
| --- | --- | --- | --- |
| ① | Notes の PDF: classic xref、無圧縮と Flate の content、fill の path、DeviceRGB/Gray、ExtGState の `ca`・`BM`、DCT と RGB の画像、埋め込み file | 縦の scroll と page 単位の swipe の 2 mode、拡大・縮小、「書き込む」で `/bin/notes <path>` を起動（起動済みなら Notes に開かせる。経路は design-input-notes.md） | p006 |
| ② | 一般の PDF: xref stream・object stream・壊れた xref の修復、全 path・stroke・clip・Form、全 bit 深さの画像・Indexed・inline image、text と埋め込み TrueType・標準 14 font の代用 | 読めない要素の注意の表示、page の thumbnail | p007 |
| ③ | CFF・Type1・Type3、標準の暗号（RC4・AES-128・AES-256、空の user password）、shading、soft mask・透明 group、Separation/DeviceN。JBIG2・CCITT・JPX は評価して決める | password の入力 | p008 |

各段の後に評価してから次へ進む（ユーザーの指示）。段階 ① を超える PDF は、段階 ① の Viewer が「この PDF の形式はまだ読めない」と示して落ちない。

## 6. main への確認事項

1. header の置き場所を `include/libc/pdf.h`（truetype.h・keiland.h と同じ直下）にした。main の例の `include/libc/pdf/pdf.h` にする理由があれば変える。
2. 段階 ② の glyph: libtruetype に輪郭の API を足すか（別の WS/Phase か p007 の範囲）、libpdf が bitmap の item を持つか。
3. deflate: 保存の圧縮に libz-compat の deflate が要る。無圧縮のまま進め、deflate は Future Work に置くか。

## 7. p004 の状態（2026-09-28）

経過と確認は [phase004/phase.md](phase004/phase.md) が正本。要約:

- 済み: `include/libc/pdf.h`、`writer.c`（規約に合わせた。buffer の `error` で段落ごとに一度検査）、`outline.c`（`pdf_outline_stroke()`・
  `pdf_outline_free()`・`pdf_writer_fill_outline()`）、画像の XObject（JPEG の DCTDecode、RGBA の RGB と SMask）、trailer の `/ID`、`/Info` の日付、package の登録と amd64・arm64・pcat・pc98 の
  `libpdf.so` の link（warning 0）、host の試験（plain・ASan・UBSan、qpdf・pdfinfo、描画の目視）。
- 残り: 自分の形式の読み込み、輪郭の補間と丸い join の置き場所（§1 と design-input-notes.md §5.3 を揃える）。
- main の決定（2026-09-28）: header は `include/libc/pdf.h` のまま、stream は無圧縮（deflate は Future Work）、段階 ② の glyph は p007 で決める。
