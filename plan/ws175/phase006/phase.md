<!-- awesome-plan project=zedbsd record=ws175-p006 -->
# ws175-p006: deflate、圧縮する stream、画像の取り込み、画像の私的な key と共有

Parent: [WS175](../ws.md)
Status: in-progress（2026-10-06 P2: 実装、host 試験 PASS。Q1 の判定待ち）
Disposition: normal
Queue: q809 の後に Q1 が順を指定（2026-10-06「q809 → WS175 p006 → 続き → q810」）
依存: [p003](../phase003/phase.md)（cleared）、D4（design.md §5.2。`userland/base/libz-compat` の path は Q1 が許可、2026-10-06）

## 範囲（design.md §10 の p006）

libz-compat の deflate（D4）、圧縮する stream の選び方 [H6][N11]、画像の取り込み（JPEG の向き・PNG の IDAT の素通しと検査・その他の PNG の RGBA＋SMask・CMYK の拒否）、
画像の私的な key と共有の XObject [M6]、preview の画像の bytes の持ち主と解放の口 [N4]。

## 実装（2026-10-06 P2）

- **deflate**（`userland/base/libz-compat/deflate.c`、新規）: `deflateInit_`・`deflateInit2_`（level 0〜9、windowBits 8〜15 と負の raw、memLevel・strategy は受けるだけ）・
  `deflate`・`deflateEnd`・`compressBound`・`compress`・`compress2`。LZ77（32 KB の窓、3 byte の hash chain、lazy match）、block（16384 の記号）ごとに
  stored・固定・動的 Huffman の短い物（長さの上限は頻度を半分にして作り直す）。inflate.c と同じく入力を全部持ち、Z_FINISH で全体を圧縮して出力を
  小分けに渡す。`include/libc/compat/zlib/zlib.h` に宣言と `deflateInit`・`deflateInit2` の macro、exports と 3 つの Makefile に追加。
  - 直した bug: 圧縮できない入力（雑音入りの 900 KB）で `compressBound` が小さすぎ、stored の block の頭（block ごとに 1 つ以上）が入らず
    末尾が切れた。block の数と 65535 byte ごとの頭の両方を数える式に直した。
- **圧縮する stream [H6][N11]**（libpdf）:
  - `pdf_writer_pack`（update.c、writer.h）: `compress2` で短くなる時だけ zlib の stream。writer.c だけを build する ws079 の試験が zlib 無しで link できるように update.c に置いた。
  - PLACE_EDIT の page の新しい content（`pdf_update_begin_edited`）を圧縮し `/Filter /FlateDecode`（`edited_flate`）。
  - editor の画像（`editor_write_image`）: RGB の samples と alpha の mask を圧縮（JPEG と PNG の行は元から圧縮済み）。
  - 圧縮しない: pen の stroke の content（上に描く stream）、NEW・REPLACE の page、ZNOT の添付、writer 自身の `pdf_writer_draw_rgba_image` の画像（major 1 の文書は今と同じ bytes）。
- **画像の取り込み**（`include/libc/pdf.h` の `struct pdf_image_source` を拡張、`userland/base/libpdf/intake.c` 新規）:
  - `PDF_IMAGE_SOURCE_PNG`: 8bit の Gray・RGB、alpha・palette・tRNS・interlace 無しの PNG は IDAT を連結して素通し（`/FlateDecode /DecodeParms << /Predictor 15 … >>`）。
    素通しの前に [L6]: 各 chunk の CRC、IHDR が最初に 1 つ、IEND まで、行を 64 KB ずつ inflate して行の数・長さ・filter（0〜4）・後ろに余りが無いことを確かめる（EINVAL）。
    それ以外の PNG は ENOTSUP（呼び手が libpng-compat で RGBA に解いて `PDF_IMAGE_SOURCE_RGBA` で渡す。libpdf は libpng-compat に依らない）。
  - `PDF_IMAGE_SOURCE_IDAT`: 読み戻した PNG の行（同じ検査）。
  - `orientation`（EXIF 1〜8、0 は 1）: 画素は回さず、置き方の行列 O × S で回す（`editor_draw_image`）。合わせ込み（`editor_fit`）は見えている向きの縦横比（5〜8 は縦横を入れ替え）。範囲外は EINVAL。EXIF を読むのは Notes（p007、`kl_picture_exif_orientation`）。
  - 上限: 一辺 16384・画素 64 M を超えると E2BIG（前は EINVAL）。CMYK の JPEG は EINVAL のまま [M15]。
  - 古い呼び手（p003 の大きさの `struct`、`size` が `orientation` の手前）も受ける（向き・id は無し）。
- **私的な key と共有 [M6]**: `id`（Notes の画像の番号）を `/KeiNotesImage <id>` として画像の dictionary に書く。1 つの update の中で同じ id の画像は
  最初の page の object を共有する（editor が取った時の bytes の SHA-256 が同じ時だけ。違う bytes の同じ id は EINVAL）。
  読み戻し `pdf_page_editor_read_image(editor, id, source, &owned)`（新しい公開 API、exports に追加）: page の XObject から id の画像を探し、JPEG は
  bytes、PNG の行は file の中の stream の bytes のまま（暗号化した文書は ENOTSUP）、それ以外は samples と SMask を解いて RGBA。無ければ ENOENT。
- **[N4] bytes の持ち主**: 設計と違う形で満たす。preview の画像の stream の bytes は文書の arena ではなく editor の buffer を指し、editor が閉じる時に
  解放する（p003b で決めたとおり、reader は画像を object で cache しないので preview の object を editor と一緒に捨ててよい）。そのため
  `pdf_document_release_image` は作らない。Notes が同じ画像を複数の page の editor に渡すと bytes は editor ごとに複写される（p007 で量が
  問題になれば共有の口を足す）。

## 試験と結果（host、2026-10-06）

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws175/tests/run-host-deflate.sh <scratch>`（新規。ASan＋UBSan、7 種の入力 × level 0/1/6/9、1000 byte ずつの入力と 7 byte ずつの出力の raw、level 10 の拒否、python の zlib で全 28 の stream を読み戻し） | 31 passed、28 streams 一致、PASS |
| `sh plan/ws175/tests/run-host-edit-scan.sh <scratch>`（`host-edit-intake.c` 新規 42 項目を追加。plain・ASan・UBSan） | 7 つの試験とも PASS（intake 42/42）、qpdf --check は試料の page 3 の読めない stream だけ |
| `sh plan/ws079/tests/run-pdf-update.sh`・`run-pdf-writer.sh`・`run-notes-host.sh`（main の rm 無しの版） | ok・ok・ok |
| build: zedBSD の `libz-compat.so`・`libpdf.so`（config-amd64-zdesktop、-Werror）、keiland-linux の両方 | warning 0 |
| `python3 plan/tools/style-check.py`（deflate.c・intake.c・editor.c・update.c・writer.c・試験） | 0 |

host-edit-intake の内容: RGB（7×5）と Gray（6×3）の PNG（行の filter 0〜4 を全部）を素通しで入れ、preview と保存した file の描画の画素が元と一致。
tRNS・palette・RGBA・16bit・interlace は ENOTSUP、CRC の壊れ・行の不足・余り・filter 5 は EINVAL、16385 と 8193×8193 は E2BIG、向き 9 は EINVAL。
向き 6 の JPEG（8×4）を form の所に入れると 107.5,70〜122.5,100 に合わせ、最初の画素が右上（122.5,70）、行が下へ。64×64 の RGBA（alpha つき）。
p003 の大きさの呼び手。2 つの page に同じ id 7 の PNG → file の中で `/KeiNotesImage 7` は 1 つ、両 page の編集した content と RGBA の samples・mask が
FlateDecode、PNG は Predictor 15。別の update で同じ id の別の画像は EINVAL。開き直して JPEG の bytes・PNG の行・RGBA を読み戻すと与えた物と一致、
PNG の行はそのまま再び取れる、無い id は ENOENT。pdftoppm で保存した page 1 を描き、向き 6 の JPEG（青→緑の gradient）が右に青で立つことを目で確かめた。

## 未実施・残り

- QEMU・実機: 未実施（WS175 の p010 で T1 にまとめて依頼する）。
- Notes 側（p007）: EXIF の向きを読む、libpng-compat で RGBA に解く、画像の id の割り当て、`notes_image` の圧縮の形 [N3]、読み戻しの照合 [H3]、Flate の開き直しの試験（§11.1 の「Notes の文書として開くこと」）。
- font の program と ToUnicode の圧縮は p005（`pdf_writer_pack` を使う）。
