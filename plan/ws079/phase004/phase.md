<!-- awesome-plan project=zedbsd record=ws079-p004 -->

# ws079-p004: libpdf の書き出し

<!-- awesome-plan-current:start -->
Status: in-progress（2026-09-28 の Kei desktop subagent の区切り。下の「残り」が済むまで cleared にしない）
Disposition: normal
Parent: [WS079](../ws.md)
Queue: main の指示（Kei desktop subagent、2026-09-28）。Awesome Plan の Queue の item ではない
Resume point: 下の「残り」
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
5. 公開の API の追加（`include/libc/pdf.h`・`exports.map`）: `struct pdf_stroke_point`・`struct pdf_point`、`pdf_writer_fill_outline`、
   `pdf_writer_set_document_id`・`pdf_writer_get_document_id`・`pdf_writer_set_dates`、`pdf_outline_stroke`・`pdf_outline_free`。
   pdf.h は `<time.h>` を読む。
6. host の試験 `plan/ws079/tests/host-pdf-writer.c`・`run-pdf-writer.sh` を更新: page 1 に筆圧が 0→1→0 の sine の波（不透明）、
   位相をずらした半透明の波、動かない 3 点の丸い点、page 2 に手で組んだ外形。ID と日付を固定して、plain・ASan・UBSan の出力が byte で一致。
   拒否の試験に ID の未定（`ENOENT`）、2 点の外形、負の日付、外形の空・幅 0・筆圧 NaN を足した。`goto` を無くした。

## 確認（2026-09-28、この worktree）

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| host の試験 | `sh plan/ws079/tests/run-pdf-writer.sh`（gcc 14.2.0、`-std=c89 -pedantic -Wall -Wextra -Werror`、plain・`-fsanitize=address`・`-fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all`） | 3 変種とも `host-pdf-writer: ok`、3 つの PDF が `cmp` で一致 |
| qpdf | `qpdf --check`（qpdf 12.2.0） | `No syntax or stream encoding errors found` |
| pdfinfo | `pdfinfo`（poppler 25.03.0） | Producer zedBSD Notes、CreationDate 2026-09-28 03:00:00 UTC・ModDate 12:00:00 UTC（表示は JST）、Pages 2、A4 |
| 埋め込み file | `qpdf --list-attachments`・`--show-attachment` | `zedbsd-notes.bin -> 10,0`、16 byte が一致 |
| `/ID` | trailer | `/ID [<7A65644253442D703030342D74657374> <5F056D932FFB0A7AA50020F116191AC0>]` |
| 描画 | `pdftoppm -r 110`（page 1 の上部） | 目視: 波の両端が細く中央が太い、端が丸い、y が下向き（波は page の上）、半透明の波が重なりで二重にならない、丸い点。画像 `build/ws035-shots/ws079-p004-20260928-stroke.png` |
| amd64 の build | `make -j16 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws079-p004-amd64 build/ws079-p004-amd64/dynamic/libpdf.so` | exit 0、warning 0。`libpdf.so` は NEEDED libc.so だけ、`pdf_` の動的 symbol 18 個 |
| pcat・pc98・rpi4 の link | `config/ci/config-{pcat,pc98,rpi4}.mk` で同じ target、BUILD は `build/ws079-p004-<platform>` | 3 つとも exit 0、warning 0。pcat・pc98 は Intel 80386、rpi4 は AArch64、どれも `pdf_` の動的 symbol 18 個（pc98 は soft-float で libc の `pow`・`acos` に `-z defs` で link できた） |

未実施: disk image の全体の build と guest での読み込み（libpdf を使う program がまだ無い）、QEMU・実機の確認（この Phase の範囲に無い）。
最初の並列の build で pcat と pc98 が同じ `build/i386/sysroot` を同時に作って pc98 が失敗した（`stdint.h` が無い）。pcat の後に pc98 を走らせて解消（code の問題ではない）。

## 残り（p004 を cleared にする前）

- 画像の XObject（JPEG の `/DCTDecode`、8bit RGB と `/SMask`）。
- 自分の形式の読み込み（design-pdf.md §4.2 の reader の p004 の分: `pdf_document_open*`・`page_count`・`page_box`・`find_attachment`・`page_content_hash`）。
- 輪郭の形の差: design-input-notes.md §5.3 は Notes の `stroke-geometry.c` が centripetal Catmull-Rom で補間し継ぎ目も丸（三角の扇）にすると書き、
  design-pdf.md §1 は輪郭の計算を libpdf の `pdf_outline_stroke()` で共有すると書く。今の `pdf_outline_stroke()` は補間をせず、鋭い角は二等分の法線で
  細くなる（丸い join ではない）。p005 の前に「補間と丸い join を `pdf_outline_stroke()` に入れて Notes も使う」か「Notes の `stroke-geometry.c` の外形を
  そのまま `pdf_writer_fill_outline()` に渡す」かを決め、設計の二か所を揃える（main の判断ではなく設計の整理で決められる）。
- `plan/coding-style.md` の全文の見直し（p009 の最終確認でも行う）。

## 再開

1. `sh plan/ws079/tests/run-pdf-writer.sh` が通ることを確かめる。
2. 上の「残り」を順に。build の確認は `BUILD=build/<自分の dir>` で `<BUILD>/dynamic/libpdf.so` の target（pcat と pc98 は同時に走らせない）。
