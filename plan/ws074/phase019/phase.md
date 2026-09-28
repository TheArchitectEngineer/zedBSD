<!-- awesome-plan project=zedbsd record=ws074p019 -->

# ws074-p019: `libjpeg-compat` 1: baseline（huffman、任意の subsampling、restart、grayscale・YCbCr）、library の登録、host の試験

Phase ID: `ws074-p019`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p002（cleared）

## 範囲（正常系のワンパス）

- `include/libc/compat/jpeglib.h`（新）: IJG libjpeg の decompress の API の部分集合（名前と値は本家、`JPEG_LIB_VERSION` 62、
  libjpeg-turbo の `JCS_EXT_*`）。struct は自前（本家との binary 互換は求めない、design.md §10）。
- `userland/base/libjpeg-compat/`（新、`/lib/libjpeg-compat.so`、`platform/amd64/vmunix.mk` に link の規則）:
  - `error.c`（標準の error manager、message の表）、`memory.c`（pool の memory manager）、`source.c`（`jpeg_mem_src`・
    `jpeg_stdio_src`、終わりの fake EOI、`jpeg_resync_to_restart`）、`marker.c`（SOI・SOF0/1・DQT・DHT・DRI・SOS・APP0（JFIF）・
    APP14（Adobe）、progressive・算術・lossless は断る）、`huffman.c`（表、bit、block、restart、データ切れは libjpeg と同じく
    segment の残りを 0 の MCU に）、`idct.c`（libjpeg の islow と同じ整数 IDCT と range limit）、`decompress.c`（API、
    libjpeg の fancy upsampling（h2v1・h1v2・h2v2）と整数比の複製、YCbCr・gray・RGB から gray・RGB・`JCS_EXT_*` へ）。
- 試験: `plan/ws074/tests/jpeg-driver.c`（program と同じ呼び方の driver、`host-jpeg`）、`run-jpeg-tests.py`（Pillow と
  cjpeg で毎回作る file、djpeg と Pillow と比べる）、`jpeg-guest.sh`（guest で同じ file を decode して host で比べる）。
  host に `libjpeg-turbo-progs`（cjpeg・djpeg 2.1.5）を入れた。
- guest の image（`config-amd64-browser.mk`）に libjpeg-compat を足した。

## 結果（2026-09-28 の時点）

- host（plain・ASan と UBSan）: `run-jpeg-tests.py` PASS。
  - 150 file（6 つの大きさ 1x1〜640x480、Pillow の q10/75/95 と 4:4:4/4:2:2/4:2:0・gray・optimize、cjpeg の sampling 1x1・2x1・
    1x2・2x2・4x1・4x2・3x1・混在 2x2,2x1,1x2、restart（行と block）、RGB、gray、q100、optimize）が **djpeg と byte ごとに一致**
    （150/150、最大差 0）、Pillow とも最大差 0。
  - `JCS_EXT_*` の 10 の順、色の file の gray 出力（djpeg -grayscale と一致）、fancy なし（djpeg -nosmooth と一致）、stdio の
    source。60% で切った file 2 つが djpeg と一致し警告を出す。progressive と算術は理由付きで断る。壊した file 96 個で crash・
    sanitizer の報告なし。
- amd64 の build: `libjpeg-compat.so` warning 0（`check-dynamic-elf.py` 通過）。style-check: library の全 file・header・
  `jpeg-driver.c` 0。guest の image の build は通った。
- 同じ commit に p017 の file の §11（返り値の段落）の直し: `net/http.c`・`net/tls.c`（HTTP/HTTPS の host の試験 14/14）。
- guest（2026-09-28、Kei への改名の後の main を merge、Venus の無い plain の guest）: `jpeg-guest.sh` で 150 file を guest の
  `libjpeg-compat.so` で decode し、**150/150 が djpeg と byte ごとに一致**（`jpeg-tests: PASS`）。
- boot test: `boot-check.sh p019` PASS（login prompt）。写真 `/home/awe/zedBSD-rpi4/build/ws074-shots/p019-20260928-boot-login.png`。
- commit: `0c385f8c`（library・header・試験・image の設定）、この記録の commit。

## 2026-09-28 のユーザーの決定の反映（同じ Phase で）

ユーザー:「JPEGライブラリは、userland/base/libjpeg-compatにして、共有にしましょう。GIFもそうするのがいいです。」
「include/libc/compat/jpeglib.hの方がいいです。訂正します。」「PNGもbase/libpng-compatにして、include/libc/compat/png/に入れましょう。」
（main 経由）。

- libjpeg-compat: header は `include/libc/compat/jpeglib.h` のまま。menu の group を `desktop` から `base` へ、platform を
  `amd64` から `*` へ。pcat・pc98・arm64 の `vmunix.mk` に `libjpeg-compat.so` の link の規則（amd64 と同じ形、`-z defs` と
  `check-dynamic-elf.py`）。`config/ci/config-pcat.mk`・`config-pc98.mk`・`config-rpi4.mk` で `libjpeg-compat.so` が
  warning 0 で link できた（i386 の soft-float の libc.so、arm64 の libc.so）。sun4u・x68k は base の data を image に入れない
  （x68k は dynamic の libc が無い）ので、選んでも何も入らない。
- libpng-compat: header を `include/libc/compat/png.h` から `include/libc/compat/png/png.h` へ移し、使い手（libpng-compat の
  `read.c`・`exports.map`、files の `thumb.c`、`plan/tools/files/host-png.c`、WS035 の記録）を直した。menu の group を `base` へ。
  platform は `amd64` のまま（libz-compat が amd64 だけで、libz-compat は触らない指示）で、MAC-T001 の例外の一覧
  （`plan/tools/menuconfig-target-host-test.py` の `platform_tied`）に理由付きで足した。
- 確認: `make menuconfig-host-test`（MAC-T001 PASS）、`plan/tools/files/host-png.sh` PASS、browser の image（files を含む）の
  build、Venus の guest で `files-p010.sh` PASS（PNG の thumbnail、写真
  `/home/awe/zedBSD-rpi4/build/ws074-shots/p019-20260928-files-png-thumbs.png`）、`browser-page.sh first.html` status 0（写真
  `p019-20260928-window-first.png`）。
- GIF は新しい Phase [ws074-p051](../phase051/phase.md)（`userland/base/libgif-compat`、`include/libc/compat/gif_lib.h`）。

## 後回し（follow-up）

- p020: progressive、CMYK/YCCK（Adobe の transform 2）、`jpeg_save_markers`（EXIF の向き）。
- 縮小（`scale_num`/`scale_denom`）、`jpeg_read_raw_data`、buffered image、suspension（`fill_input_buffer` が FALSE を返す
  source は終わりとして読む）、12 bit、算術符号、圧縮。
