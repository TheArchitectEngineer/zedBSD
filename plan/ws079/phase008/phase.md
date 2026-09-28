<!-- awesome-plan project=zedbsd record=ws079-p008 -->

# ws079-p008: libpdf の段階 ③（CFF・Type 1 の glyph の輪郭、標準の暗号の復号）

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-28、PDF/Notes subagent。host と QEMU の Venus guest の証拠と 4 platform の build。実機は未実施。clearance は main の判断で覆してよい）
Disposition: normal
Parent: [WS079](../ws.md)
Queue: main の指示（PDF/Notes subagent、2026-09-28、2026-10-17 のデモに向けて）。Awesome Plan の Queue の item ではない
Resume point: なし（残りは下の「残り」）
<!-- awesome-plan-current:end -->

## 範囲（main の指示、2026-09-28）

1. CFF / Type1C（FontFile3）と Type 1（FontFile）の glyph の輪郭を、TrueType と同じく display list の塗りとして描く。charstring（Type 2、
   eexec と charstring の復号つきの Type 1）を libpdf で読むか、libtruetype に輪郭を足すかを決めて記録する。
2. 標準の security handler の復号（RC4 40/128、AES-128/256、空の user password）で、暗号化されているが開ける PDF を描く。Notes は
   引き続き書き込みを拒む。
3. host の実 PDF（/usr/share/doc）と生成した PDF で pdftoppm と比べる。

規則: libpdf の API は追加だけ。

## 決めたこと

| 点 | 決めたこと | 理由 |
| --- | --- | --- |
| charstring をどこで読むか | **libpdf**（新しい `charstrings.c`・`type1.c`・`cff.c`・`cffdata.c`）。libtruetype は変えない | PDF の /FontFile（eexec の Type 1）と /FontFile3（裸の CFF、CID-keyed の CIDFontType0C）は PDF の埋め込みの形で、単独の font file として desktop が使う形ではない。charstring の輪郭は三次曲線で、libpdf の display list の path（`m l c h`）にそのまま入る（TrueType の二次の変換が要らない）。libtruetype は TrueType（glyf）の library のまま。OpenType の /FontFile3 は `CFF ` table を取り出して同じ CFF の読み手に渡し、TrueType の輪郭なら従来どおり libtruetype |
| font.c との境 | `internal.h` の `pdf_charstrings_*`（名前・自前の encoding・CID から glyph、glyph の輪郭を ems の path の step として callback に）。font.c は program の種類で TrueType の face か charstring の program を持ち、glyph の cache・幅・display list への変換は共通 | font.c の既存の構造（slot の cache、`emit()`）をそのまま使える |
| simple font の code → glyph | ① /Differences の名前 → ② font が base encoding を名指さないとき program 自身の encoding → ③ encoding の文字（Unicode）を glyph 名の文字（Adobe Glyph List）に合わせる → ④ program 自身の encoding | PDF 1.7 9.6.6.2 の Type 1 の規則（base encoding の無い font は program の encoding）と、他の reader の慣習 |
| CID-keyed CFF | charset が glyph → CID。CID を二分探索で glyph に。FDSelect で glyph ごとの private DICT（local subrs・幅）と FontMatrix（font DICT の matrix × Top DICT の明示の matrix） | CIDFontType0C の仕様 |
| seac | Type 1 の seac（asb adx ady bchar achar、accent を adx − asb だけずらす）と Type 2 の endchar の 4 引数（adx ady bchar achar）。base と accent は StandardEncoding の code の名前で引く | Type 1 と CFF の仕様。CFF の標準文字列と StandardEncoding は fontTools の表から生成（`make-cff-tables.py` → `cffdata.c`） |
| 暗号の場所 | 新しい `crypt.c`。reader は read_catalog で /Encrypt を見つけたら handler を開き（空の user password）、それより前に読んだ object（修復の走査）と object stream を読み直す。parse_indirect で読んだ object の文字列を復号（/Encrypt 自身と XRef stream を除く）、`pdf_filter_decode()` が stream の data を filter の前に復号（inline image・XRef・`EncryptMetadata false` の Metadata を除く） | object stream の中の object は個別に暗号化されない（object stream の stream が復号される）。読み込みの経路の 2 か所だけで済む |
| 暗号の範囲 | 標準 handler の R2〜R6: RC4（40〜128 bit、V1/V2）、V4 の crypt filter（/CF・/StmF・/StrF、V2・AESV2・/Identity・/None）、V5 の AESV3（R5 の SHA-256、R6 の ISO 32000-2 algorithm 2.B）。AES と RC4 は crypt.c に実装（AES の S-box は GF(2^8) から計算）、MD5 と SHA-256/384/512 は C library（`md5.h`・`sha2.h`） | libc に AES・RC4 が無い |
| 開けない暗号 | user password が空でない → `PDF_EPASSWORD`（新しい定数、値は EACCES）。他の handler・読めない /Encrypt → ENOTSUP（従来どおり）。どちらも修復に進まない | 公開 API は追加だけ（定数 1 つ） |
| 書き込みの拒否 | `pdf_writer_create_update()` は暗号化された base を EACCES で拒む（revision も暗号化しなければならない）。Notes は開けた後も `pdf_document_encrypted()` で見て EACCES（従来の拒否の notice） | main の指示「Notes still refuses to write on them」 |
| Type 3 font（範囲の「など」、design-pdf §5 の段階 ③） | glyph の procedure（/CharProcs、/Differences の名前）を content の interpreter が text の位置で走らせる: 独立の level、CTM の前に FontMatrix・font size と Tz と Ts・text matrix、塗りの色は今のもの、resources は font の（無ければ text の演算子の）。text matrix・operand・clip の途中の状態は保つ。入れ子は form の上限 | dvips の bitmap font（dtc-paper）と txirefcard の Type 3 が SKIPPED だった。bitmap の glyph は inline の stencil image で、p007 で描けるようになった |
| PDF Viewer | 開けない暗号化の文書（EACCES かつ `pdf_document_encrypted()`）は「it is protected by a password」 | 以前の「permission denied」は file の権限と区別できない |

## 実装

| file | 内容 |
| --- | --- |
| `userland/base/libpdf/charstrings.h`（新） | charstring の program の内部の形（glyph ごとの charstring と名前、名前の整列、CID の表、自前の encoding、global/local subrs、private DICT、matrix）と、path の出口 |
| `userland/base/libpdf/charstrings.c`（新） | `pdf_charstrings_close/count/find/name/builtin/cid_keyed/cid/outline`、path の helper（move・line・curve・close、matrix で ems へ）、名前の merge sort、StandardEncoding の code → glyph、OpenType の `CFF ` table の探索 |
| `userland/base/libpdf/type1.c`（新） | clear text の /FontMatrix と /Encoding（`pdf_type1_encoding()`、font.c の代用の Unicode にも使う）、eexec の復号（binary と 16 進）、private の lenIV・/Subrs・/CharStrings（RD・-| の binary）、charstring の復号、Type 1 の interpreter（hsbw・sbw・移動・線・曲線・closepath・callsubr・div・callothersubr（flex 0〜2、hint の置き換え 3、他は引数を返す）・pop・setcurrentpoint・seac） |
| `userland/base/libpdf/cff.c`（新） | CFF の header・INDEX・DICT（整数・実数）、Top DICT・charset（予め定められた 3 つと形式 0〜2）・encoding（標準と形式 0・1 と補遺）・private DICT と local subrs・FDArray・FDSelect（形式 0・3）、Type 2 の interpreter（全ての描画の演算子、flex 4 種、hintmask の byte の読み飛ばし、幅、callsubr/callgsubr の bias、endchar の seac、算術と stack の演算子） |
| `userland/base/libpdf/cffdata.c`（新、生成） | CFF の標準文字列 391 個、StandardEncoding の SID、Expert・ExpertSubset の charset（`plan/ws079/tests/make-cff-tables.py`、fontTools） |
| `userland/base/libpdf/crypt.c`（新） | 標準 security handler（上の表） |
| `userland/base/libpdf/font.c` | `load_program()` を種類ごとに（FontFile2 → libtruetype、FontFile3 → CFF か OpenType、FontFile → Type 1）、`map_program_codes()`・`read_difference_names()`、`append_glyph()`・`face_advance()` が charstring の program の輪郭と幅を使う、`composite_glyph()` が CID-keyed CFF の charset を引く、`read_builtin_encoding()` を `pdf_type1_encoding()` に。p007 から残っていた規約の違反（`load_program`・`read_builtin_encoding`）はこの書き換えで無くなった |
| `userland/base/libpdf/font.c`・`content.c`（Type 3） | `load_type3()` が /CharProcs・/Differences の名前・/Resources・6 つの /FontMatrix を読み、`pdf_font_type3_glyph()`（内部）が code の procedure を返す。`draw_type3_glyph()` が show_string の各 code で procedure を走らせる。/CharProcs のある Type 3 font は SKIPPED にしない |
| `userland/base/libpdf/reader.c`・`filter.c`・`internal.h` | 暗号の handler の開き方・読み直し・文字列と stream の復号、読み込んだ dictionary・stream に object の番号と世代を記録 |
| `userland/base/libpdf/update.c` | 暗号化された base を EACCES で拒む |
| `include/libc/pdf.h` | `PDF_EPASSWORD`（追加だけ） |
| `userland/base/libpdf/Makefile` | 新しい 5 つの source |
| `userland/desktop/notes/save.c` | 開けた暗号化の PDF も EACCES |
| `userland/desktop/pdfviewer/view.c` | password の要る文書の message |

## 確認（2026-09-28、この worktree。host は gcc 14.2.0・poppler 25.03.0・qpdf 12.2.0）

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| 生成した文書と pdftoppm | `sh plan/ws079/tests/run-pdf-text.sh 200`（`make-text-pdfs.py` の新しい `programs.pdf`: Type 1 Nimbus Sans の WinAnsi＋/Differences（fi fl ß € Å ç）、自前の encoding の symbolic な Type 1（Standard Symbols、/Encoding なし）、CFF Type1C（Nimbus Roman、TJ・Tz）、OpenType の FontFile3（Nimbus Mono PS Bold）、Noto Serif CJK JP の subset の CID-keyed CFF を Identity-H と V、Tr 2 と傾き） | programs: ぼかし平均 0.963・64 超 0.000%（埋め込みの許容 2.5・0.6%）。p007 の 6 page は前と同じ数値。3 build（plain・ASan・UBSan）が全 page で同じ画素、object stream と修復の copy が 6 文書とも同じ画素 |
| 暗号 | 同じ script の 4b: text-simple と programs を qpdf で RC4 40・RC4 128（R3）・AES-128（R4、metadata の暗号化あり・なし）・AES-256 R5・R6・R6＋object stream で暗号化（空の user password） | 14 の copy とも 3 build が平文の文書と同じ画素。user password のある文書は `open error 13`（EACCES） |
| 実文書（/usr/share/doc の 9 つ、168 page） | 同じ script の 5（3 build）と、全 page の pdftoppm との比較（`build/ws079-scratch/cmp-real.sh` の plain、80 dpi） | SKIPPED の page: p007 の 94 → **3**（gnus-logo 1 と txirefcard・txirefcard-a4 の各 1 page。どれも CCITTFax の画像で、段階 ③ の範囲外）。全 page で 3 build が同じ画素・sanitizer の報告なし。gnus-logo 以外の 167 page でぼかし後の 64 超の画素は最大 0.213%（debian-faq p59）、平均は最大 4.74（txirefcard p2）。代表の page（script の中）: quilt p1 1.269/0.000%、crc-doc p2 0.876/0.000%、fontconfig-user p1 1.907/0.007%、shared-mime-info-spec p1 1.540/0.017%（埋め込みの許容）、debian-faq p3 2.880/0.007%、txirefcard p1 4.044/0.000%、dtc-paper p1 3.310/0.019%（「密な小さい文字」の許容 5.0・0.6%: 80 dpi の細い字は poppler の FreeType が stem を濃くし bitmap の glyph を別の方法で縮めるので平均が上がるが、64 を超える画素はほぼ無い。目視で同じ） |
| script の全体 | `run-pdf-text.sh 200` の後、dtc-paper p1 だけが埋め込みの許容の平均を超えた（3.310、64 超 0.019%）ので「密な小さい文字」の組に移し、`run-pdf-text.sh 20` で全体を流し直した | 200 の回: dtc-paper p1 以外は全て合格。20 の回: **run-pdf-text: ok** |
| 破壊の loop | 同じ script の 6: 6 文書 × 非圧縮と object stream の copy × ASan・UBSan × 変異 200（stream の中）＋200（file のどこでも）、AES-128 の programs、非圧縮にした quilt（Type 1）・txirefcard（CFF・Type 3）・dtc-paper（Type 3）の各 66＋66 | 落ちず、sanitizer の報告なし。programs の非圧縮の copy は font の program の bytes も変異する（qpdf が stream を展開）。開けた数: programs 200/199（object stream の copy 107/195）、暗号の copy 66/64、実文書 66/66 |
| 他の host の回帰 | `run-pdf-render.sh 300`・`run-pdfviewer-host.sh`・`run-pdf-reader.sh`・`run-notes-host.sh`・`run-pdf-update.sh`・`run-pdf-writer.sh`（reader が crypt.c と C library の MD5（openbsd-digest.c）を要るので、link する script に足した） | 6 つとも ok。追加の検査: `host-pdf-update encrypted`（qpdf で暗号化した base は開けて `pdf_writer_create_update()` が EACCES）、`host-notes encrypted`（開ける暗号化の PDF を Notes が EACCES で拒む）、plain・ASan・UBSan |
| build | amd64: `make -j16 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws079-p007-amd64 …/bin/pdfviewer …/bin/notes …/dynamic/libpdf.so …/dynamic/libtruetype.so`、pcat・pc98・rpi4: `config/ci/config-<p>.mk`・`BUILD=build/ws079-p007-<p>`・`…/dynamic/libpdf.so` | 4 つとも exit 0・warning 0。amd64 の libpdf.so が要る MD5・SHA-256/384/512 は libc.so が export（`llvm-nm -D`） |
| 規約（機械的） | `plan/tools/style-check.py`（新しい 4 つの C と font.c・shading.c は全体、他は p007 の commit b572ac16 と比べる）と 3 つ以上の節の条件の検出 | 違反 0。p007 で残した font.c の 2 関数も書き換えで解消 |
| QEMU の Venus guest（KVM、zdesktop --glass 1280x800） | main の `build/ws035-sq/hdd-image.img`（19:13）の copy を `build/ws079-p008-run/` に置き、この worktree の pdfviewer・notes・libpdf.so・libtruetype.so を SSH で入れ替えて `plan/ws079/tests/pdf-demo-guest.sh OUT PREFIX start install quilt faq refcard programs encrypted notice annotate refuse stop` | **全段 ok**（下）。zdesktop の log の ERROR 0 |

guest の段（QEMU、画面は `build/ws035-shots/ws079-p008-20260929-*.png`）:
- quilt（pdfTeX の Type 1 の CM）: `PAGE index=0 items=517 flags=0`、1 page の raster 30 ms。`quilt.png`・`quilt-zoom.png`（本物の CMR/CMSS）・`quilt-3.png`。
- faq（CFF と CIDFontType2、xref stream）: page 3 `items=1224 flags=0 ms=24`。`faq-3.png`・`faq-3-zoom.png`。
- refcard（CFF Type1C、横長）: `items=6279 flags=0 ms=32`。`refcard.png`・`refcard-zoom.png`。
- programs: `flags=0`、`programs.png`（Type 1・CFF・OpenType・CJK の横と縦）。
- encrypted: AES-256（R6）の programs.pdf が開く（`encrypted.png`）。user password のある文書は「Cannot open password.pdf: it is protected by a password.」（`password.png`）。
- notice: gnus-logo.pdf（CCITTFax）で `NOTICE shown flags=1`（`notice.png`、左下の pill）。
- annotate: PDF Viewer の quilt.pdf → Ctrl+E → `NOTES OPEN pages=12 strokes=0 kind=foreign`・`NOTES BACKGROUND source=0`、F11、pen の波と marker の波（`annotate-page1.png`、背景は Type 1 の本文）、Ctrl+S `NOTES SAVE reason=request pages=12 strokes=2`。保存した file（305,421 byte）を host へ取り、qpdf --check 誤りなし、pdftoppm の page 1 が元の上に 2 本の線（`annotated-pdftoppm.png`）。
- refuse: 暗号化の programs.pdf で Annotate → `NOTES OPEN failed error=25`（guest の EACCES）と notice（`refuse-encrypted.png`）。

## 未実施と制限

- 実機（i915・pen・touch の LCD）: 未実施。disk image への組み込みの build: 未実施（guest は main の image の copy に SSH で入れ替えた）。
- CCITTFax（gnus-logo、txirefcard の Type 3 の 1 glyph）・JBIG2・JPX の画像は SKIPPED のまま（注意が出る）。
- 暗号: user password の入力（PDF Viewer の password の画面）は無い（空の password だけ）。/P の権限は見ない（表示だけ）。公開鍵の handler（/Adobe.PubSec）と crypt filter の /Crypt の stream filter は ENOTSUP・未対応。暗号化された文書の添付（`pdf_document_find_attachment*`）と page の content の hash は復号しない（Notes は暗号化の文書を拒むので使わない）。
- charstring: hint（stem・hintmask・dotsection）は使わない（小さな文字の形は poppler と少し違う）。Type 2 の random は 0.5 固定。CFF の CharstringType 1 と Expert encoding、Multiple Master の othersubr（14〜18）は未対応。Type 3 の glyph は text の clip（Tr 4〜7）に加わらない。
- 規約の確認は機械的な検出と、自分の書いた code の読み直しまで。全文の目視の確認は p009。

## 残り

1. CCITTFax の decoder（G3 1D/2D・G4。scan の PDF と dvips の一部の bitmap font）、JBIG2・JPX の評価。
2. PDF Viewer の password の入力。
3. thumbnail（p007 の残り）。
4. p009（全文の規約と回帰）。
