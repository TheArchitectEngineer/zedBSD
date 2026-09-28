<!-- awesome-plan project=zedbsd record=ws074p051 -->

# ws074-p051: `libgif-compat`（GIF の共有の base の library）

Phase ID: `ws074-p051`（main に番号の確認を求めた）
Parent: [WS074](../ws.md)
Status: planned
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

## 後回し（follow-up）

- 圧縮（`EGif*`）、`DGifGetRecordType` 等の低水準の逐次の API、animation の合成（browser の側）。
