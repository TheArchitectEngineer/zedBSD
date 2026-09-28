<!-- awesome-plan project=zedbsd record=ws074p020 -->

# ws074-p020: `libjpeg-compat` 2: progressive、CMYK/YCCK、`jpeg_save_markers`

Phase ID: `ws074-p020`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p019

## 範囲（正常系のワンパス）

- progressive の Huffman（SOF2、T.81 G.1.2）: DC の最初と細かくする scan、AC の最初（EOBRUN）と細かくする scan。係数を
  component ごとの block の配列に貯め、最後の scan の後にまとめて逆 DCT する。量子化の表は component が最初の scan に出たときに
  覚える（libjpeg の latch と同じ）。restart は EOBRUN も戻す。
- 4 component: CMYK（Adobe の transform 0 か Adobe 無し）と YCCK（transform 2）。出力は `JCS_CMYK`（YCCK は libjpeg の
  式で CMYK に）。CMYK から RGB への変換は libjpeg-turbo にも無いので作らない（browser の側で行う、p021）。
- `jpeg_save_markers`: APPn と COM の segment を `marker_list` に残す（長さの上限付き、JPOOL_IMAGE）。JFIF・Adobe の読み取りは
  残した bytes からも行う。
- `JCS_EXT_BGRA` は p019 で済み。

## 受け入れ

1. amd64 の build（warning 0）、変えた file の style-check 0。
2. host（plain・ASan）: cjpeg `-progressive`（既定の scan の script: DC の最初と細かくする、AC の最初と細かくする）と Pillow の
   `progressive=True` の file が djpeg と byte ごとに一致。restart 付きの progressive も。CMYK（Pillow）の raw の出力が
   libjpeg-turbo の raw（Pillow の `CMYK;I` の反転を戻した値）と一致。`jpeg_save_markers` で APP1 の bytes が file と同じ。
   p019 の試験が下がらない。算術符号は理由付きで断る。
3. guest（jpeg-guest.sh）で同じ比較。

## 結果（2026-09-28）

cleared。

- 実装: `huffman.c`（progressive の 4 種の scan: DC の最初・DC を細かく・AC の最初（EOBRUN）・AC を細かく、`jpeg_compat_get_bits`、
  scan の種類ごとの表の確認、量子化の表の latch、`jpeg_compat_finish_progressive` の最後の逆 DCT）、`marker.c`（SOF2、4 component、
  progressive の SOS の検査、`jpeg_save_markers` と APPn・COM の保存、JFIF・Adobe は保存した segment の先頭から）、`decompress.c`
  （CMYK・YCCK の既定、YCCK → CMYK の変換、係数の配列の確保、`marker_list` の初期化）、`internal.h`・`error.c`・`exports.map`、
  `include/libc/compat/jpeglib.h`（`jpeg_save_markers`）。
- host（plain・ASan と UBSan）: `run-jpeg-tests.py` PASS。**193 file が参照と byte ごとに一致**（p019 の 150 に、6 つの大きさの
  cjpeg の progressive 5 種（既定・gray・1x1・restart 2・optimize）と Pillow の progressive、Pillow の CMYK、APP1・COM の
  付いた file）。CMYK は libjpeg-turbo の出力（Pillow の `CMYK;I` の反転を戻した値）と一致。`jpeg_save_markers` の 3 つの
  marker（APP0・APP1（Exif）・COM）が file の bytes と一致。算術符号（sequential・progressive）は理由付きで断る。壊した file
  144 個で crash・sanitizer の報告なし。切った file: sequential の 2 つは djpeg と一致。**progressive を切った file は decode
  して警告を出すが、djpeg と最大 5 違う**（libjpeg は後の scan の無い係数を block smoothing で補う。後回し）。
- guest（plain の guest、image の `libjpeg-compat.so`）: `jpeg-guest.sh` で **193/193 が参照と byte ごとに一致**、算術符号の
  2 file は断る（`jpeg-tests: PASS`）。guest の `/tmp` が 32 MiB の tmpfs で出力が入り切らなかったので、作業の場所を
  `/root/ws074` に移した（最初の実行は 66 file の出力が空で、比較の script の PNM の読み取りが止まらなかった: 短い出力を
  失敗にするよう直した）。2 回目の比較では p019 の残りの `refused-progressive.jpg`（今は decode できる）が失敗に数えられた
  ので、file を作る前に古い file を消すようにし、残りを消して guest の出力を比べ直した（193/193、PASS）。
- build: amd64 の image（warning 0、`libjpeg-compat.so` の `check-dynamic-elf.py` 通過）、pcat・pc98・rpi4 の CI の構成で
  `libjpeg-compat.so` が warning 0。style-check: library の全 file・header・`jpeg-driver.c` 0。
- boot test: p051 の記録（同じ image）。

## 後回し（follow-up）

- block smoothing（切れた progressive の file、全部の係数が細かくされない file で libjpeg と同じ絵にする）。
- YCCK の試験（手元の道具で YCCK の file を作れなかった。変換は libjpeg の式で実装）。
- 縮小（`scale_num`/`scale_denom`）、`jpeg_read_raw_data`、buffered image、suspension、12 bit、算術符号、圧縮。
