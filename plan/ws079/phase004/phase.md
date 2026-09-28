<!-- awesome-plan project=zedbsd record=ws079-p004 -->

# ws079-p004: libpdf の書き出しと自分の形式の読み込み

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-28、2 回目の区切り。受け入れの確認は下の「2 回目の区切りの確認」）
Disposition: normal
Parent: [WS079](../ws.md)
Queue: main の指示（Kei desktop subagent、2026-09-28 の 2 回）。Awesome Plan の Queue の item ではない
Resume point: なし（p004 の範囲は済んだ）。disk image の全体の build は未実施（下の「未実施」）
<!-- awesome-plan-current:end -->

## 範囲（ws.md の表）

libpdf の書き出し（page、ベクタの path、画像、編集の metadata）と自分の形式の読み込み。設計は [design-pdf.md](../design-pdf.md) §1・§2・§4。

## main の決定（2026-09-28）

- 公開の header `include/libc/pdf.h` を受け入れる（libpdf は自前の API）。
- stream は当面無圧縮で保存する。deflate は Future Work。
- 段階 ② の glyph の問題（libtruetype の輪郭の API か bitmap の item か）は p007 へ送る。
- Future Work の表への deflate の行の追加は main に任せた（他の worktree と F の番号が衝突しないように。内容: libz-compat の deflate、content・編集 data の `/FlateDecode`、きっかけは保存の大きさが問題になったとき）。

## この区切りで行ったこと（2026-09-28）

1. `plan/ws079/wip.patch` を適用して削除した: `userland/base/libpdf/Makefile`（platform `*`、`/lib/libpdf.so`）と
   amd64・arm64・pcat・pc98 の `platform/*/vmunix.mk` の `libpdf.so` の link の規則（`-z defs`、`exports.map`、`check-dynamic-elf.py`）。
2. `writer.c` を `plan/coding-style.md` に合わせた:
   - `error == 0 &&` の連鎖・`goto out`・条件演算子（page tree の区切りの `index == 0 ? "" : " "`）を無くした。
   - 方式: `struct pdf_buffer` に `error`（最初の失敗）を持たせ、`buffer_*` の append は `void` にした。失敗した buffer への追記は何もしない。
     一つの PDF object を書く段落は、終わりで `buffer->error` を一度だけ見る。page の content が一度失敗したら、その page は失敗のまま
     （`pdf_writer_save` が `write_page_objects` でその誤りを返す）。
   - `write_document` を catalog・page tree・information に分け（`write_catalog`・`write_page_tree`・`write_information`）、描画中の page の
     content を返す `open_content()` を置いた。一つの `if` に一つの条件（幅・高さ・色の成分の検査を分けた）。
3. `pdf_outline_stroke()`（新しい file `userland/base/libpdf/outline.c`）: 筆圧の点列 → 閉じた外形の多角形。
   - 幅 `w = width × (0.15 + 0.85 × p^0.7)`（design-input-notes.md §5.1 の曲線、p は 0..1 に clamp、NaN は `EINVAL`）。
   - 各点の法線（内側の点は前後の線分の単位方向の和＝角の二等分、端は一つの線分）で左右の辺を ±w/2 に取り、終端・始端に半円の丸い cap。
     cap の弦の数は誤差 0.05 pt 以内（半円あたり 2〜64）。動かない点（前の点から 0.001 pt 未満）は前の点に畳み、太い方を残す。
     一点だけの stroke は丸い点（円）。
   - `pdf_outline_free()`、および外形を nonzero の一回の fill で塗る `pdf_writer_fill_outline()`（自己交差しても半透明が二重にならない）。
4. trailer の `/ID` と `/Info` の日付:
   - `/ID [<永続の 16 byte> <版の 16 byte>]`。第 1 要素は `pdf_writer_set_document_id()` で与えるか、初回の保存で `arc4random_buf` で作り、
     以後の保存で保つ（`pdf_writer_get_document_id()` で読める。未定なら `ENOENT`）。第 2 要素は xref の前までの bytes の 32-bit FNV-1a ×4
     （開始値を変えた 4 本。暗号用ではなく版の区別だけ）。
   - `/Info << /Producer (zedBSD Notes) /CreationDate (D:YYYYMMDDHHmmSSZ) /ModDate (…) >>`（UTC、`gmtime_r`）。`pdf_writer_set_dates()` で
     与えるか、0 なら作成日は初回の保存の時刻（以後保つ）、更新日は各保存の時刻。負の時刻は `EINVAL`。
5. 画像の Image XObject（design-pdf.md §1）:
   - `pdf_writer_draw_jpeg_image()`: JPEG の bytes をそのまま `/Filter /DCTDecode`（成分 1 は DeviceGray、3 は DeviceRGB。CMYK は `EINVAL`）。
     幅・高さ・成分は呼び手が JPEG の frame header から渡す（libpdf は JPEG を読まない）。
   - `pdf_writer_draw_rgba_image()`: RGBA8（上の行から）を 8bit RGB の samples と、不透明でない画素があるときだけ 8bit Gray の `/SMask` に分ける。
   - 置き方: page の y 下向きの空間の矩形 (x, y, w, h) に `q w 0 0 -h x y+h cm /ImN Do Q`（画像の 1 行目が上）。画像は各 page の共有の
     `/Resources /XObject` に並ぶ。1 辺 16384 画素・文書あたり 4096 枚まで。画像も最後の `pdf_writer_set_fill_color()` の不透明度（`ca`）で塗られる
     （PDF の規則。試験で半透明の wedge の後の画像が薄くなったので、API の注記と試験の不透明への戻しを足した）。
6. 公開の API の追加（`include/libc/pdf.h`・`exports.map`）: `struct pdf_stroke_point`・`struct pdf_point`、`pdf_writer_fill_outline`、
   `pdf_writer_draw_rgba_image`・`pdf_writer_draw_jpeg_image`、`pdf_writer_set_document_id`・`pdf_writer_get_document_id`・
   `pdf_writer_set_dates`、`pdf_outline_stroke`・`pdf_outline_free`（動的 symbol は計 20）。pdf.h は `<time.h>` を読む。
7. host の試験 `plan/ws079/tests/host-pdf-writer.c`・`run-pdf-writer.sh` を更新: page 1 に筆圧が 0→1→0 の sine の波（不透明）、
   位相をずらした半透明の波、動かない 3 点の丸い点、page 2 に手で組んだ外形。ID と日付を固定して、plain・ASan・UBSan の出力が byte で一致。
   page 2 に ImageMagick で作る 64x48 の JPEG（`convert -size 64x48 gradient:red-yellow`）と、上が不透明で下が透明の 32x32 の緑の RGBA を重ねた。
   試験の program は第 2 引数に JPEG の path を取る。拒否の試験に ID の未定（`ENOENT`）、2 点の外形、負の日付、外形の空・幅 0・筆圧 NaN を足した。`goto` を無くした。

## 確認（2026-09-28、この worktree）

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| host の試験 | `sh plan/ws079/tests/run-pdf-writer.sh`（gcc 14.2.0、`-std=c89 -pedantic -Wall -Wextra -Werror`、plain・`-fsanitize=address`・`-fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all`） | 3 変種とも `host-pdf-writer: ok`、3 つの PDF が `cmp` で一致 |
| qpdf | `qpdf --check`（qpdf 12.2.0） | `No syntax or stream encoding errors found` |
| pdfinfo | `pdfinfo`（poppler 25.03.0） | Producer zedBSD Notes、CreationDate 2026-09-28 03:00:00 UTC・ModDate 12:00:00 UTC（表示は JST）、Pages 2、A4 |
| 埋め込み file | `qpdf --list-attachments`・`--show-attachment` | `zedbsd-notes.bin -> 10,0`、16 byte が一致 |
| `/ID` | trailer | `/ID [<7A65644253442D703030342D74657374> <5F056D932FFB0A7AA50020F116191AC0>]` |
| 描画 | `pdftoppm -r 110`（page 1 の上部） | 目視: 波の両端が細く中央が太い、端が丸い、y が下向き（波は page の上）、半透明の波が重なりで二重にならない、丸い点。画像 `build/ws035-shots/ws079-p004-20260928-stroke.png` |
| 画像 | `pdfimages -list`、`pdftoppm -r 72`（page 2 の一部） | JPEG は `jpeg` 64x48 rgb、RGBA は `image` 32x32 rgb と `smask` 32x32 gray。目視: JPEG は赤が上・黄が下（上下が正しい）、緑の正方形は上が不透明で下へ透明。画像 `build/ws035-shots/ws079-p004-20260928-images.png` |
| amd64 の build | `make -j16 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws079-p004-amd64 build/ws079-p004-amd64/dynamic/libpdf.so` | exit 0、warning 0。`libpdf.so` は NEEDED libc.so だけ |
| pcat・pc98・rpi4 の link | `config/ci/config-{pcat,pc98,rpi4}.mk` で同じ target、BUILD は `build/ws079-p004-<platform>` | 3 つとも exit 0、warning 0（画像を足した後に 4 platform とも再 build）。pcat・pc98 は Intel 80386、rpi4 は AArch64、4 つとも `pdf_` の動的 symbol 20 個（pc98 は soft-float で libc の `pow`・`acos` に `-z defs` で link できた） |

main（27d25cb9）を merge した後にも host の試験（3 変種・qpdf）と amd64 の `libpdf.so`（exit 0、warning 0）を確かめた（ws.md の表の衝突は main の行に p004 の状態を入れて解消）。

未実施: disk image の全体の build（`plan/ws035/tests/build-zdesktop-image.sh build/ws079-p004-amd64` を始めたが止めた。guest harness の構成が
lldb 入りの host の LLVM を `build/llvm-build` で configure し、install 先が共有の `build/llvm`（この worktree では main の `build/llvm` への symlink）
だったため、共有の toolchain を書き換える前に止めた。共有の `build/llvm` の時刻は 2026-09-27 のままで変わっていない。image の build は、main の
既存の image の dir で `libpdf.so` の差分だけを入れるか、lldb を含む LLVM が既に入った環境で行う）。guest での読み込み（libpdf を使う program が
まだ無い）、QEMU・実機の確認（この Phase の範囲に無い）。
最初の並列の build で pcat と pc98 が同じ `build/i386/sysroot` を同時に作って pc98 が失敗した（`stdint.h` が無い）。pcat の後に pc98 を走らせて解消（code の問題ではない）。

## 1 回目の区切りの残り（2 回目で済んだ）

- 自分の形式の読み込み → 済み（下の 2）。
- 輪郭の形の差 → main の判断（2026-09-28、ws.md）「stroke の形は `pdf_outline_stroke()` を唯一の元にする。Catmull-Rom の平滑化と丸い join をこの関数に入れる」
  に従って済み（下の 1）。design-pdf.md §1 を更新した。design-input-notes.md §5.3 の `stroke-geometry.c` の文は Notes（p005）の担当の文書なので
  この worktree では書き換えていない（ws.md の main の判断が優先。p005 が「`pdf_outline_stroke()` を呼ぶ」に揃える）。
- `plan/coding-style.md` の全文の見直し → 新しい code（`outline.c` の書き直し、`object.c`・`reader.c`・`internal.h`、writer の追加）に全文を当てた
  （条件の中の呼び出し・3 節以上の条件・`error == 0 &&` の連鎖・入れ子の呼び出しの引数を無くし、`resolve_key()` などに分けた）。
  試験の program（`plan/ws079/tests/`）は 1 回目の `host-pdf-writer.c` と同じ `if (error == 0)` の連鎖の形を残す。WS の最終確認は p009。

## 2 回目の区切りで行ったこと（2026-09-28）

1. `pdf_outline_stroke()` の平滑化と丸い join（`userland/base/libpdf/outline.c` を書き直した。署名は不変）:
   - 動かない点を畳んだ後の sample を **centripetal Catmull-Rom** で結ぶ。各区間を Bezier の形（制御点は knot の間隔 √距離 の接線から）に直し、
     Wang の上界（n = ⌈√(0.75·d/0.05)⌉、d は制御点の 2 階差分の大きい方、区間あたり 1〜256）で誤差 0.05 pt 以内の区間に切る。区間の終わりは
     次の sample にちょうど合わせ、曲線は全 sample を通る。端の区間は隣の sample を端で折り返した仮の点を使う。
   - **筆圧**は区間の前後 4 つの筆圧からの Catmull-Rom（Hermite）で補間し、区間の両端の筆圧の間に抑える（はみ出して太く・細くならない）。
     幅の曲線は 1 回目と同じ `width × (0.15 + 0.85 × p^0.7)`。
   - **丸い join**: 平滑化した中心線の各点で、曲がりの外側の辺はその点の周りの円弧（前後の線分の法線の間、誤差 0.05 pt の弦）、内側の辺はその点
     そのものを通る。nonzero で塗ると、多角形は区間ごとの台形・join の扇形・両端の半円の cap（全て同じ向き）の和そのものになり、角で細くならない。
     円弧が 1 本の弦で済む緩い曲がり（|θ| ≤ 2·acos(1 − 0.05/r)）は、前後の法線の二等分の方向の 1 点で済ませる（角の数が約半分になった）。
   - 決定的（乱数・状態なし）。同じ入力は同じ輪郭（試験で 2 回の呼び出しを `memcmp` で比べる）。上限: 中心線・輪郭とも 16,777,216 点（超えると `EINVAL`）。
2. 自分の形式の読み込み（新しい file `userland/base/libpdf/object.c`・`reader.c`・`internal.h`。公開の追加は `include/libc/pdf.h`・`exports.map`）:
   - `object.c`: 文書ごとの arena（64 KiB の block、上限 256 MiB）、lexer（空白・comment、数（桁を整数として貯めて 10 の冪で 1 回割る＝writer の
     数が同じ double に戻る）、名前の `#xx`、literal 文字列の escape（`\n` 等・8 進 3 桁・行の継続・改行の正規化）、16 進文字列）、parser（配列・辞書、
     `n g R` の参照、入れ子 32 まで）。
   - `reader.c`: `pdf_document_open`（file を全部読む、512 MiB まで）・`pdf_document_open_memory`（複写）・`pdf_document_close`・
     `pdf_document_page_count`・`pdf_document_page_box`・`pdf_document_page_content_hash`・`pdf_document_find_attachment`・
     `pdf_document_find_attachment_type`・`pdf_document_get_id`・`pdf_document_get_dates`。
     - header（先頭 1024 byte の `%PDF-`）、末尾 1024 byte の最後の `startxref`、classic の xref table と trailer、`/Prev` の連鎖（32 節まで、
       同じ節への戻りは拒む）。entry は読んだ順に集め、object 番号で並べて新しい方だけを残し、二分探索で引く（番号は 8,388,607 まで、節の件数は
       file の残りの bytes で抑える）。
     - 間接 object は一度だけ読み、読み中の印で自分を要る object（`/Length` が自分など）を拒む。参照の連鎖・読みの入れ子は 32 まで。stream は
       `/Length`（間接も可）の bytes の後に `endstream` が要る。
     - page tree は継承（MediaBox・CropBox・Rotate）つきで、node を walk ごとの印で一度だけ訪ね（循環・同じ kid の繰り返しを飛ばす）、深さ 32 まで。
     - `page_box`: MediaBox・CropBox（MediaBox で切る、何も残らなければ MediaBox）、Rotate（負・360 超は正規化、90 の倍数以外は 0）、回した後の幅・高さ。
     - `page_content_hash`: `/Contents`（無し・stream・stream の配列）を順に連結した bytes の SHA-256（libc の `SHA256Init/Update/Final`）。
     - `find_attachment_type`: catalog の `/AF`、次に `/Names /EmbeddedFiles` の name tree（`/Limits` を信じず全 node、walk ごとの印）。名前は `/UF`・`/F`、
       media type は stream の `/Subtype`。`/Params /Size` が長さと違えば `PDF_EFORMAT`。bytes は document の中を指す（close まで）。
     - `get_id`（trailer の `/ID` の第 1 要素が 16 byte のとき）、`get_dates`（`/Info` の `D:YYYYMMDDHHmmSS±HH'mm'`、省略・時差つき、無効は 0）。
     - 誤り: 形式の誤りは `PDF_EFORMAT`（`pdf.h`、= `EILSEQ`。C library に `EFTYPE` が無い）、xref stream・filter つきの stream・暗号化は `ENOTSUP`、
       file が大きすぎれば `EFBIG`。
   - writer に `pdf_writer_get_page_content_hash()` を足した（Notes が保存の前に編集 data の `PAGE` の SHA-256 を得る。reader の値と一致を試験で確かめた）。
   - 動的 symbol は 20 → 31。libpdf.so の依存は libc.so だけ（`SHA256*` は libc にある）。
3. host の試験:
   - `plan/ws079/tests/host-pdf-outline.c`（新規、`run-pdf-writer.sh` から plain・ASan・UBSan）: zigzag 3 本（角 53°・22°・6°、筆圧が変わる）、同じ線を
     戻る hairpin、3 pt 横を戻る hairpin、9 点だけの疎な loop。各 stroke で、2 回の輪郭が同じ、全 sample が内側、各 sample から前後の線分と平均の方向の
     法線 ±、それぞれ ±5° 回した方向に半幅の 85% の点が内側（nonzero の winding）を確かめる。**1 回目の `outline.c` では同じ検査が 532 件失敗**
     （全 stroke が失敗し、同じ線を戻る hairpin では先端の sample が外に出る）、新しい `outline.c` では 0 件。
   - `plan/ws079/tests/host-pdf-reader.c`・`run-pdf-reader.sh`（新規）: (1) writer で 3 page（A4・792x612・100.5x50.25）、画像、3000 byte の添付
     （全 byte 値と偽の `endstream`）を書き、file と memory から読んで page 数・箱・添付の bytes・hash（writer の値と一致）・ID・日付を比べる。
     (2) 追記の改訂（page 1 を CropBox つき・Rotate -90・`/Contents [5 0 R 5 0 R]` に、catalog を name tree だけの添付に替え、`/Prev` で結ぶ）。
     (3) 手作りの file: 間接の `/Length` と data 中の偽 `endstream`、`/Length` が自分、自分を 1000 回挙げる page tree、40 段の入れ子、40 段の page tree、
     catalog 無し、暗号化、xref stream、`/Prev` の輪、file より大きい節、filter、継承と Rotate 450・45、文字列・名前の escape、`/Params /Size` の不一致、
     16 進の ID、時差つき・年だけの日付、輪になった name tree。(4) 壊れた file: 小さな文書の全ての切り詰めと大きな文書の末尾 2048 byte の全ての切り詰め、
     決まった xorshift の 30,000 回 × 2 文書の破壊（1〜8 か所、byte の置換・数字の置換・挿入・削除・区切り文字、半分は末尾 600 byte に集中）。開けたものは
     全ての呼び出しを行う。
   - `run-pdf-writer.sh` は writer.c が `<sha2.h>` を読むので libc の `src/libc/openbsd-sha2.c` を host 用に compile して link するようにした。

## 2 回目の区切りの確認（2026-09-28、この worktree）

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| writer と輪郭の host 試験 | `sh plan/ws079/tests/run-pdf-writer.sh`（gcc 14.2.0、`-std=c89 -pedantic -Wall -Wextra -Werror`、plain・ASan・UBSan） | exit 0。3 変種とも `host-pdf-writer: ok`・`host-pdf-outline: ok`（6 stroke とも失敗 0）、PDF は 3 変種で `cmp` 一致、qpdf 12.2.0 `--check` は 2 file とも `No syntax or stream encoding errors found` |
| 輪郭の検査の対照 | 同じ `host-pdf-outline.c` を 1 回目の `outline.c`（`git show HEAD:…`）と link | 532 件の「narrows」、`hairpin-exact: sample 12 is outside`、6 stroke とも失敗 |
| reader の host 試験 | `sh plan/ws079/tests/run-pdf-reader.sh` | exit 0。3 変種とも `host-pdf-reader: ok`（plain 6.8 s、ASan 25 s、UBSan 21 s）。破壊 30,000 回のうち小さな文書 3,247 回・大きな文書 4,989 回が開けて全 API を通った。ASan・UBSan の報告なし（LeakSanitizer も報告なし） |
| hash の独立の確認 | `qpdf --show-object=5,7,9 --raw-stream-data … \| sha256sum` と reader の値 | 3 page とも一致（`f633e86b…`・`fb52109f…`・`c70eef91…`） |
| round trip の file | `qpdf --check`・`qpdf --list-attachments` | 誤り無し、`zedbsd-notes.bin -> 17,0` |
| 描画の目視 | `pdftoppm -r 110`（全体）と `-r 300`（loop と hairpin の先端） | zigzag は角で細くならず滑らか（疎な sample は Catmull-Rom で波になる）、hairpin の先端は丸く全幅、3 pt 横に戻る hairpin は曲がり目に小さな膨らみ（中心線の U 字の分、自然）、疎な loop は赤い sample 点 9 個を全て通り筆圧に沿って太くなる。page 1 の波は 1 回目と同じ見た目（両端が細い・半透明の重なりが二重にならない）。画像 `build/ws079-shots/ws079-p004-20260928-*.png`（この worktree の build） |
| amd64 の `libpdf.so` | `make -j16 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws079-p004-amd64 build/ws079-p004-amd64/dynamic/libpdf.so` | exit 0、warning 0（clang 23.1.0、`-Wall -Wextra -Werror`、`-z defs`）。動的 symbol `pdf_` 31 個、NEEDED libc.so だけ。再実行でも up to date（最終の source が compile 済み） |
| pcat・rpi4 の `libpdf.so`（追加の確認） | `config/ci/config-{pcat,rpi4}.mk`、BUILD `build/ws079-p004-{pcat,rpi4}` | 2 つとも exit 0、warning 0、`pdf_` 31 個（Intel 80386 ELF32・AArch64）。rpi4 の 1 回目は新しい build dir で arm64 の sysroot ができる前に libc の math が compile されて失敗（`math.h` が無い、1 回目の pcat・pc98 と同じ順序の問題）、変更なしの再実行で通った |
| 空白 | `git diff --check` | 誤り無し |

## 未実施と制限

- **disk image の全体の build**（`plan/ws035/tests/build-zdesktop-image.sh`）: `make -n … disk-image` で、この worktree では guest の LLVM（`build/llvm-build` を
  `CMAKE_INSTALL_PREFIX=build/llvm`＝共有の toolchain への symlink で configure し、lldb の tblgen を build）・guest の clang・libcxx・openssl を build する
  ことが分かったので始めなかった（指示どおり `<BUILD>/dynamic/libpdf.so` の target だけにした。共有の `build/llvm` には触れていない）。`libpdf.so` の
  target は worktree の中の `build/llvm-source`（compiler-rt の builtins 用、main の `toolchain/llvm/distfiles` の tarball を複写して展開）と
  `build/amd64/sysroot` を作るだけだった。image に入れての guest での読み込みは、libpdf を使う program（Notes、p005）ができてから。
- QEMU・実機の確認: この Phase の範囲に無い（host の試験と target の build まで）。
- pc98 の `libpdf.so` は今回は build していない（pcat と同じ i386 の ELF。1 回目は 4 platform とも link 済み）。
- 読み込みは p004 の範囲（writer の形式）だけ: xref stream・object stream・filter（Flate）・壊れた xref の修復は p006・p007。一つの page の辞書が
  壊れていると文書全体を `PDF_EFORMAT` で拒む（p006 の viewer で緩めるかを決める）。
- 疎な sample（数十 pt 間隔）の鋭い zigzag は輪郭の角が多い（24 sample で約 1,900 点）。ペンの 200 Hz 程度の sample では区間が短く 1〜数個に切られる。
  PDF の大きさが問題になれば許容誤差（0.05 pt）か deflate（Future Work）で減らす。

## 再開

p004 の範囲は済んだ。回帰は `sh plan/ws079/tests/run-pdf-writer.sh` と `sh plan/ws079/tests/run-pdf-reader.sh`。build の確認は `BUILD=build/<自分の dir>` で
`<BUILD>/dynamic/libpdf.so` の target（pcat と pc98 は同時に走らせない。新しい build dir の初回は sysroot の順序で失敗することがあり、再実行で通る）。
