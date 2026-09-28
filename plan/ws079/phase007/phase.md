<!-- awesome-plan project=zedbsd record=ws079-p007 -->

# ws079-p007: libpdf の段階 ②（一般の PDF、まず文字）

<!-- awesome-plan-current:start -->
Status: uncleared（2026-09-28、main の指示で区切りまで。文字・shading・xref stream・修復は host と QEMU（Venus）で動いた。未了は下の「残り」）
Disposition: normal
Parent: [WS079](../ws.md)
Queue: main の指示（Kei subagent、2026-09-28）。Awesome Plan の Queue の item ではない
Resume point: 下の「再開」
<!-- awesome-plan-current:end -->

## 範囲（main の指示、2026-09-28）

1. libpdf の content の解釈に文字: BT/ET・Tf・Tm・Td/TD/T*・Tc/Tw/Tz/TL/Ts・Tr（塗り・線・不可視、以上）・Tj/TJ/'/"、q/Q の中の text state。
   font: 埋め込み TrueType（FontFile2）を libtruetype の `truetype_glyph_outline()` で display list の塗りに（simple font の /Encoding（WinAnsi・MacRoman・
   Standard・/Differences）と cmap、/Widths）、Type0/CIDFontType2 の Identity-H と CIDToGIDMap、標準 14 font と埋め込みの無い font はシステムの font
   （keiland*.ttf）で代用し /Widths の寸法で配置。ToUnicode は不要。段階 ① の上限と安全を保つ。
2. 時間があれば shading（sh と pattern の塗り、axial・radial の type 2・3）。
3. 試験: host（文字の PDF を pdftoppm と許容差で比べる、ASan/UBSan、破壊の loop）、guest（Venus の PDF Viewer で文字の PDF、画面を
   `build/ws035-shots/ws079-p007-20260928-*.png`）、画素の数値を記録。

規則: libpdf の API は追加だけ（今回は公開 API の変更なし）。libtruetype への追加は 2 関数（下）。

## 行ったこと

### libtruetype（`userland/desktop/libtruetype`、公開 API の追加 2 つ、既存の関数の振る舞いは不変）

- `truetype_open_embedded(data, size, &face)`: `truetype_open()` と同じだが cmap が無い・選べる Unicode の subtable が無い face も開く（文書に
  埋め込まれた subset は cmap を持たないことが多い。debian-faq.pdf の CharisSIL・FreeMono の FontFile2 は cmap 無しを fontTools で確認）。
  `face.c` の開く処理を `open_face()`・`read_map()` に分けた（`truetype_open()` は従来どおり cmap を要求）。
- `truetype_cmap_lookup(face, platform, encoding, code, &glyph)`: 指定の (platform, encoding) の subtable を引く。format 0・4・6・12。無ければ ENOENT。
  （3,0）symbol と（1,0）Mac の cmap のため。`cmap.c` の `lookup()` に format 0・6 を足した（Unicode の選択は従来どおり 4・12 だけ）。
- `exports.map`・`include/libc/truetype.h`。package の platform を `amd64` から `*` に（libpdf が全 platform で要る）。arm64・pcat・pc98 の
  `vmunix.mk` に `libtruetype.so` の link 規則を足した（amd64 の写し）。

### libpdf（`userland/base/libpdf`）

- `font.c`（新規）: 文書ごとの font の cache（font dictionary の object ごと、4096 まで）。
  - simple font: /FontFile2 を libtruetype で。code → glyph は PDF の規則と他の reader の慣習: (3,1) を encoding の Unicode で、(3,0) を code と
    0xF000/0xF100/0xF200 + code で、(1,0) を encoding の MacRoman の code か code そのもので、(3,1) を code で、map の無い program は code = glyph 番号。
    /FirstChar・/Widths・/MissingWidth（無ければ face の advance、Courier は 600）。
  - encoding: `encoding.c`（新規、`plan/ws079/tests/make-encoding-tables.py` で生成）に StandardEncoding・WinAnsiEncoding（未使用の code は bullet、
    0xA0 space、0xAD hyphen）・MacRomanEncoding の Unicode 表と、/Differences の glyph 名 1010 個（3 encoding の名前と Adobe Glyph List（BSD-3-Clause、
    fontTools.agl）の Latin・Greek・句読点・通貨・矢印・数学・合字の範囲）。`uniXXXX`・`uXXXX`・`.sc` などの接尾辞も読む。
  - Type 1 の program（/FontFile）は段階 ③ まで描かず代用するが、平文部の組み込み encoding（`/Encoding 256 array … dup n /name put`）は読む
    （pdfTeX の CM font の ﬁ などが正しい文字になる）。
  - Type0: DescendantFonts の CIDFontType2、Encoding は Identity-H・Identity-V（DW2 の縦の原点と送り）、/W の 2 形式と /DW（CID 65535 まで）、
    /CIDToGIDMap（/Identity か stream）。他の CMap と CIDFontType0 は SKIPPED（文字の位置は /W で進める）。Type3 は SKIPPED（/FontMatrix の幅で進める）。
  - 代用: 名前（subset の接頭辞は無視）・flags・/FontWeight から sans・serif・mono と bold・italic を選び、`PDF_FONT_DIRECTORY`（既定
    `/usr/share/fonts`、compile 時の設定）の `keiland{,-serif,-mono}{,-bold,-italic,-bolditalic}.ttf` を近いものから探す。無い style は斜体を 0.2 の
    傾き、太字を 0.03 em の線で太らせる。TeX の font 名（CMR10・CMBX12・CMTT・SFRM・LMRoman…）から family と style を読む。代用の glyph が
    /Widths より 5% 以上広ければ横を縮める（0.6 まで）。代用に無い合字（U+FB00〜FB04）は字を並べて描く。Symbol・ZapfDingbats は代用しない（SKIPPED）。
    代用した font・読めない program は list に PDF_DISPLAY_SKIPPED を立てる。
  - glyph の輪郭: `truetype_glyph_outline()` の二次の輪郭を三次の `c` にして em 単位で font に cache（hash、200 万点を超えたら捨てて作り直す）。
- `content.c`: text の演算子（BT ET Tf Tc Tw Tz TL Ts Tr Td TD Tm T* Tj TJ ' "）。text state は graphics state の level に持ち q/Q で戻る。1 つの
  文字列の glyph を user 空間の 1 つの path にして Tr で塗る（0 塗り、1 線、2 両方、3 なし、4〜7 はそれに加えて ET で clip）。文字列あたり
  1,048,576 文字まで、path は段階 ① と同じ点の上限。代用の太字は塗りの色の線で太らせる。
- `shading.c`（新規）と `content.c`: axial（2）・radial（3）の shading を `sh` と shading pattern（`cs /Pattern`・`scn /Pn`、PatternType 2、塗り・線・文字）
  で描く。display list の型は増やさず、shading を page の領域の RGBA の画像（1 pt 2 pixel、1 辺 1024 まで、関数を 1024 点で標本化）にして
  image item で置き、pattern は path の clip の中に置く。関数は type 0（1 入力の sampled）・2・3、色空間は Gray・RGB・CMYK・Cal*・ICCBased。
  他の shading・関数・tiling pattern は SKIPPED。
- `reader.c`: cross-reference stream（/W・/Index・type 0/1/2）、hybrid の /XRefStm、object stream（/N・/First、decode した stream を文書に保持、
  合計 256 MiB まで）、壊れた xref の修復（`n g obj` を末尾から走査、最後の trailer の /Root、無ければ XRef stream の辞書、無ければ /Type /Catalog の
  object から trailer を作る。object stream の中身は直接の object より弱い）。xref の並べ替えを libc の `qsort`（O(n²)）から自前の merge sort に。
  font の cache は reader から見えないよう、font.c が解放の関数を渡す（`pdf_reader_set_font_cache()`）。
- `internal.h`・`Makefile`（font.c・encoding.c・shading.c、require に desktop/libtruetype）。4 platform の `libpdf.so` が `libtruetype.so` を NEEDED。
- 公開 API（`include/libc/pdf.h`・`exports.map`）は不変。

### 試験（`plan/ws079/tests/`）

- `make-text-pdfs.py`（新規）: host の font（Liberation・DejaVu・Droid Sans Fallback）から試験の PDF を作る（commit しない）。
  - `text-simple.pdf`: WinAnsi・/Differences（α β γ ﬁ ﬂ）・MacRoman・(3,0) だけの symbolic・(1,0) だけの font、Tc Tw Tz TL T* ' " Ts TJ、Tr 0〜3 と 7
    （clip）、回転・傾き、q/Q の text state、Form XObject の中の文字。
  - `text-cid.pdf`: cmap 無しの CIDFontType2 の Identity-H と CIDToGIDMap /Identity、CIDToGIDMap stream（CID = Unicode）と /W の 2 形式、Identity-V の
    縦書き（日本語）、Tr 2。
  - `text-std14.pdf`: 埋め込みの無い Helvetica・Times・Courier とその bold・italic（/Widths は Liberation の寸法）、/Widths の無い Helvetica、Tw の両端揃え。
  - `shading.pdf`: axial（Extend）・radial（stitching）・sampled・CMYK・関数の配列・pattern の塗り・線・文字（ca 0.6 も）。
- `pdf-text-guest.sh`（新規）: Venus の guest の手順（start・install・simple・cid・std14・shading・faq・quilt・stop）。
- 既存の host 試験の source の一覧を更新: `run-pdf-render.sh`・`run-pdfviewer-host.sh`（font.c・encoding.c・shading.c・libtruetype）、
  `run-pdf-reader.sh`（filter.c と libz-compat）。`host-pdf-reader.c` の期待を 2 つ変えた（空の xref stream は ENOTSUP ではなく PDF_EFORMAT、
  /Prev の loop は修復されて page 0 の文書として開く）。

## 確認（2026-09-28、この worktree）

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| 文字・shading と pdftoppm（100 dpi） | host の cc（gcc 14.2.0、`-std=c89 -pedantic -Wall -Wextra -Werror`）、poppler 25.03.0。代用 font の dir は Liberation | 下の表 |
| 実文書（/usr/share/doc の 9 個） | 全 page を plain と ASan+UBSan で描く | 9 個とも開き、全 page を描いて ASan・UBSan の報告なし（debian-faq 76 page: plain 0.87 s） |
| p006 の回帰 | `sh plan/ws079/tests/run-pdf-render.sh 150` | ok（12 の比較が全て許容内、Flate の copy が同じ画素、破壊の loop（plain 150・ASan/UBSan 各 50）で落ちず） |
| p004 の回帰 | `sh plan/ws079/tests/run-pdf-reader.sh` | ok（期待を 2 つ更新した後。plain・ASan・UBSan） |
| 破壊の loop（文字） | `build/ws079-p007-host/fuzz.sh 300`（ASan+UBSan。4 文書の非圧縮の copy と qpdf の object stream の copy に、content の変異と byte の変異 各 300） | 下の「破壊の loop の結果」 |
| object stream の copy | qpdf `--object-streams=generate` の text-simple を描く | 元と同じ画素（`cmp`、fuzz.sh の最後） |
| amd64 の build | `make -j16 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws079-p007-amd64 …/bin/pdfviewer …/dynamic/libpdf.so …/dynamic/libtruetype.so` | exit 0、warning 0。libpdf.so の NEEDED に libtruetype.so（check-dynamic-elf 通過） |
| pcat の build | `make -j16 ZEDBSD_CONFIG=config/ci/config-pcat.mk BUILD=build/ws079-p007-pcat …/dynamic/libpdf.so` | exit 0、warning 0（新しい libtruetype.so の規則を含む） |
| pc98・rpi4 の build | — | 未実施 |
| Venus の guest（QEMU、KVM） | main の `build/ws035-sq/hdd-image.img`（16:05）の copy を起動し、pdfviewer・libpdf.so・libtruetype.so を SSH で入れ替え、`plan/ws079/tests/pdf-text-guest.sh` | 全段 ok（下） |

pdftoppm との比較（100 dpi、左が libpdf）。「生」は p006 と同じ指標（平均差、差 64 超の画素の割合）、「ぼかし」は両方を半径 1 px の Gaussian で
ぼかした後（poppler は glyph を pixel の格子に合わせて描くので、文字の縁が 0.5 px ずれる。位置の誤りは 1 px のぼかしでは消えない）:

| 文書 | 生 平均 / 64 超 | ぼかし 平均 / 64 超 | 備考 |
| --- | --- | --- | --- |
| text-simple | 2.183 / 1.21% | 1.191 / 0.007% | 埋め込み font。目視で同じ |
| text-cid | 1.280 / 0.84% | 0.747 / 0.030% | 縦書きも同じ位置 |
| text-std14 | 5.433 / 3.06% | 3.026 / 0.69% | poppler は URW（Nimbus）、こちらは Liberation で代用。位置は同じ、字形が違う |
| shading | 2.299 / 0.94% | 2.267 / 1.07% | 差は CMYK の変換（p006 と同じ既知の差）と、poppler が pattern の塗りに ca を掛けず、文字の線の pattern を黒で描くこと（こちらは仕様どおり） |
| debian-faq p3（80 dpi） | 7.503 / 4.57% | 2.878 / 0.006% | xref stream・object stream・CIDFontType2（cmap 無し） |
| quilt p1（80 dpi） | 4.170 / 2.46% | 3.099 / 0.24% | Type 1（CM）を serif の代用で。組み込み encoding で ﬁ・ﬀ（字を並べる）も出る |

比較の画像: `build/ws079-p007-host/cmp-{text-simple,text-cid,text-std14,shading}.png`、`build/ws079-p007-host/real/cmp-{debian-faq-3,quilt-1}.png`（左 libpdf、右 poppler）。

guest（QEMU の Venus、zdesktop --glass 1280x800、代用 font は image の Inter と JetBrains Mono）。画面は
`build/ws035-shots/ws079-p007-20260928-{simple,simple-zoom,cid,std14,std14-zoom,shading,faq-3,faq-3-zoom,faq-12,quilt,quilt-zoom}.png`:

- text-simple: `PAGE items=37 flags=0 ms=74`、raster 11 ms（465x658）。cid: `items=9 flags=0 ms=343`（Droid Sans Fallback 4 MB の font の読み込みを含む）。
- shading: `items=26 flags=0 ms=407`、raster 266 ms（shading の画像の標本化）。
- debian-faq.pdf（76 page、xref stream）: page 3 `items=1224 flags=0 ms=28`、raster 59 ms、拡大で 909x1286 を 63 ms。
- quilt.pdf: `items=520 flags=1`（Type 1 の代用で SKIPPED）。

### 破壊の loop の結果

`build/ws079-p007-host/fuzz.sh 300`（ASan+UBSan、`-fno-sanitize-recover=all`）: 4 文書 × 2（qpdf の非圧縮の copy と object stream の copy）× 2（content の
変異 300、byte の変異 300）で落ちず、sanitizer の報告なし。開けた数: content の変異は全て、byte の変異は 299〜300（非圧縮）、object stream の copy は
text-simple 299・text-cid 300・shading 299・text-std14 20（小さい file で xref stream と object stream に当たり、修復でも catalog が見つからない）。時間:
text-simple の object stream の copy が最長 125 s（変異のたびに修復で全 object を読む）。

main の merge（2026-09-28、p014 の update.c と reader.c の追加）: reader.c の衝突 4 か所を両方残して解いた（xref_offset・has_previous と修復、
font の cache）。merge 後に host の plain で text-simple・text-cid の比較が同じ数値、実文書が開くこと、amd64 の libpdf.so・pdfviewer の build（warning 0）を
確認。merge 後の ASan・guest・p004/p006 の回帰は再実行していない。

## 未実施と制限

- 実機: 未実施。pc98・rpi4 の build: 未実施（arm64・pc98 の libtruetype.so の link 規則は build していない）。disk image への組み込みの build: 未実施
  （guest は main の image の copy に SSH で入れ替えた）。
- Notes の host 試験（`run-notes-host.sh`・`notes-perf.sh`）は reader.c を単独で link しているが、reader.c が filter.c（と libz-compat）を要るように
  なった（xref stream・object stream）。この 2 つは Notes の側（p014 が作業中）なので変えていない。直すには source に `userland/base/libpdf/filter.c`
  と libz-compat の object を足す（`run-pdf-reader.sh` と同じ）。
- 文字: ToUnicode は読まない（描画に不要）。Type0 の Identity 以外の CMap（埋め込みの CMap stream・UniJIS などの名前）、CIDFontType0、Type3、
  Type 1 と CFF の program（代用で描く）は段階 ③。縦書きの /W2 は読まない（DW2 だけ）。代用の font の字形は元と違う（位置と幅は /Widths どおり）。
  Symbol・ZapfDingbats は代用しない。標準 14 font で /Widths の無いものは代用の font の advance（Courier だけ 600）。
- shading: type 1・4〜7、PostScript の関数（type 4）、tiling pattern、/Background、/AntiAlias は SKIPPED・未対応。shading は 1 pt 2 pixel の画像なので
  大きく拡大すると粗い。
- ASCII85・LZW・RunLength の filter、inline image、soft mask と他の blend mode、読めない要素の注意の表示、thumbnail は未着手（段階 ② の残り）。
- 規約: 新しい code は coding-style の全文に沿って書いたが、全文の見直し（条件の中の呼び出し、`&&` の連鎖、式の Boolean の残り）はしていない。
  font.c の `read_builtin_encoding()`・`scan_objects()`・`paint_shading()` などに条件の中の比較の連鎖が残る。p009 か次の区切りで直す。

## 再開

1. `sh plan/ws079/tests/run-pdf-render.sh`・`run-pdf-reader.sh`・`run-pdfviewer-host.sh` を回す（今回の更新の後）。
2. 文字の host 試験を script にする（今は `build/ws079-p007-host/{quick,iter,blurcmp,real,realcmp,fuzz}.sh` の使い捨て。`run-pdf-text.sh` として
   `make-text-pdfs.py` の文書の生成・plain/ASan/UBSan の同じ画素・ぼかしの比較の許容（埋め込み: 平均 2.5・0.6%、代用: 平均 4.0・1.5%）・
   object stream と修復の copy・実文書・破壊の loop をまとめる）。
3. pc98・rpi4 の `libpdf.so` を build する。
4. 規約の見直し（上の「規約」）。
5. 段階 ② の残り（ASCII85・LZW・RunLength、inline image、注意の表示、thumbnail）。
6. guest: `GUEST_RUNTIME=$PWD/build/ws079-p007-run/rt DOCS=build/ws079-p007-host/real-docs sh plan/ws079/tests/pdf-text-guest.sh OUTDIR PREFIX start install simple cid std14 shading faq quilt stop`
   （`IMAGE` は既定で `build/ws079-p007-run/hdd-image.img`、main の zdesktop image の copy）。
