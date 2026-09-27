<!-- awesome-plan project=zedbsd record=ws074p019 -->

# ws074-p019: `libjpeg-compat` 1: baseline（huffman、任意の subsampling、restart、grayscale・YCbCr）、library の登録、host の試験

Phase ID: `ws074-p019`
Parent: [WS074](../ws.md)
Status: in-progress（2026-09-28、main の wrap up で中断。host は済み、guest と boot test が残る）
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
- **未実施**: guest での decode の比較（`jpeg-guest.sh`。guest に tar が無いので 1 file ずつ送る形に直した、その実行の途中で
  wrap up）、boot test。
- commit: `0c385f8c`（library・header・試験・image の設定）、この記録の commit。

## 再開の手順

1. `git merge main`（Kei への名前の変更で `/bin/browser` 等が変わっている場合、試験の script の path を合わせる）。
2. `sh plan/ws074/tests/build-browser-image.sh`、`sh plan/ws074/tests/browser-guest.sh plain`、`... wait`。
3. `sh plan/ws074/tests/jpeg-guest.sh`（約 150 file を 1 つずつ送るので数分かかる。background で走らせる）。期待:
   `jpeg-tests: PASS`（djpeg と一致）。
4. `sh plan/ws074/tests/boot-check.sh p019`、写真を見る。
5. この phase.md を cleared にし、ws.md の表と Resume point を直して commit、main へ報告。

## 後回し（follow-up）

- p020: progressive、CMYK/YCCK（Adobe の transform 2）、`jpeg_save_markers`（EXIF の向き）。
- 縮小（`scale_num`/`scale_denom`）、`jpeg_read_raw_data`、buffered image、suspension（`fill_input_buffer` が FALSE を返す
  source は終わりとして読む）、12 bit、算術符号、圧縮。
