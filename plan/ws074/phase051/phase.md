<!-- awesome-plan project=zedbsd record=ws074p051 -->

# ws074-p051: `libgif-compat`（GIF の共有の base の library）

Phase ID: `ws074-p051`（2026-09-28 main が確認）
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p002

## 出典

2026-09-28 ユーザー:「JPEGライブラリは、userland/base/libjpeg-compatにして、共有にしましょう。GIFもそうするのがいいです。」
「include/libc/compat/jpeglib.hの方がいいです。訂正します。」→ design.md の D5（GIF の decoder の置き場）を browser の中の
`image/gif.c` から、base の共有の library `libgif-compat` に変えた。header は jpeglib.h・png の隣の `include/libc/compat/`。

## 範囲（正常系のワンパス）

- `include/libc/compat/gif_lib.h`（新）: giflib 5.2 の decode の API の部分集合。名前と値は giflib のもの（`GIFLIB_MAJOR` 5、
  `GIFLIB_MINOR` 2）、struct は自前（binary 互換は求めない、libjpeg-compat と同じ）。
  - 型: `GifFileType`（`SWidth`・`SHeight`・`SColorResolution`・`SBackGroundColor`・`AspectByte`・`SColorMap`・`ImageCount`・
    `Image`・`SavedImages`・`ExtensionBlockCount`・`ExtensionBlocks`・`Error`・`UserData`）、`ColorMapObject`、`GifColorType`、
    `GifImageDesc`、`SavedImage`、`ExtensionBlock`、`GraphicsControlBlock`、`GifByteType`・`GifWord`・`InputFunc`。
  - 関数: `DGifOpen`（読み取りの callback）、`DGifOpenFileName`、`DGifOpenFileHandle`、`DGifSlurp`（全部の frame を読み、
    interlace を戻した `RasterBits`）、`DGifCloseFile`、`DGifSavedExtensionToGCB`、`GifErrorString`。
  - error の code は giflib の `D_GIF_ERR_*`。
- `userland/base/libgif-compat/`（新、`/lib/libgif-compat.so`、base の group、全 platform）: GIF87a・GIF89a の header、
  global と local の color table、LZW（可変の code の幅、clear・end、code table の一杯）、interlace、extension（graphic
  control、application、comment を `ExtensionBlocks` に）。
- 試験: `plan/ws074/tests/gif-driver.c`・`run-gif-tests.py`（Pillow で作った GIF と、Pillow の decode と palette の index と
  色で比べる）。
- 使い手: browser の画像（p021）。

## 受け入れ

1. amd64・i386（pcat）・arm64（rpi4）の build（warning 0）、style-check 0、`make menuconfig-host-test`。
2. host（plain・ASan）: 1x1〜大きな画像、palette の大きさ 2〜256、interlace、透明色、複数の frame、local の color table の
   file が Pillow と一致。壊した file で crash しない。
3. guest で同じ比較（1 回）。

## 結果（2026-09-28）

cleared。

- 実装: `include/libc/compat/gif_lib.h`（giflib 5.2 の名前と値、struct は自前）、`userland/base/libgif-compat/`（`decode.c`: open
  の 3 つ、header と screen と global の color map、`DGifSlurp` の record の読み取り、image と local の color map、interlace を
  戻す、extension の block を giflib と同じ形で次の image へ（最後の image の後のものは file へ）、`DGifSavedExtensionToGCB`、
  `DGifCloseFile`、`GifErrorString`。`lzw.c`: sub-block と LSB から詰めた可変長の code、clear・end、表が一杯の後、code の
  幅の増加）、`Makefile`（base の group、`*`）、`exports.map`、amd64・pcat・pc98・arm64 の `vmunix.mk` に link の規則。
  guest の image（`config-amd64-browser.mk`）に足した。
- giflib との違い（header に書いた）: data の途中で file が切れた image も `ImageCount` に数え、decode できた pixel を残す（giflib は
  その image を落とす）。error の code は giflib と同じ。読み取りの callback の短い返事は続きを読む（giflib は終わりとする）。
- 試験の方法: `gif-driver.c` を 3 通りに build する（library の source と host、host の giflib 5.2.2（Debian の libgif-dev、
  2026-09-28 に導入）、zedBSD の `libgif-compat.so`）。driver の報告（screen、color map、各 image の位置・大きさ・interlace、
  extension の block、GCB、`DGifSlurp` の結果）と raster が giflib と byte ごとに同じかを比べる。
- host（plain・ASan と UBSan）: `run-gif-tests.py` PASS。Pillow で作った **75 file を名前と callback の 2 通りで読み、150 回とも
  giflib と一致**（1x1〜640x480、2〜256 色、interlace の有無、gray、透明色と comment、local の palette・delay・disposal・loop の
  animation 2 つ、表が一杯になる 512x512 の noise）。切った file 3 つは giflib と同じ error（102）で、前の image は一致し、途中の
  image を残す。GIF でない file は 103 で断る。壊した file 84 個で crash・sanitizer の報告なし。
- guest（plain の guest、image の `libgif-compat.so`）: `gif-guest.sh` で **75/75 が giflib と一致**（`gif-tests: PASS`）。
- build: amd64 の image（新しい file の warning 0。`noct` の package の `interpreter.c` の既存の warning は別）、pcat・pc98・rpi4 の
  CI の構成で `libgif-compat.so` が warning 0、`make menuconfig-host-test` PASS。style-check: library・header・`gif-driver.c` 0。
- boot test（p020・p051・zlib の移動の後の同じ image）: PASS、写真
  `/home/awe/zedBSD-rpi4/build/ws074-shots/p051-20260928-boot-login.png`。
- 同じ image で Venus の guest: `files-p010.sh` PASS（zlib の header の移動の後の PNG の thumbnail、写真
  `p051-20260928-files-png-thumbs.png`）、`browser-page.sh first.html` status 0。

## 後回し（follow-up）

- 圧縮（`EGif*`）、`DGifGetRecordType` 等の低水準の逐次の API、animation の合成（browser の側）。
