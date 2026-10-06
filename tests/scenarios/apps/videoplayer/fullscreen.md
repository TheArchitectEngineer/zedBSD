---
id: apps.videoplayer.fullscreen
title: Video Player を F11・Alt+Enter・double click で全画面にし、Esc で戻す
status: active
areas: [videoplayer, compositor]
paths: [userland/desktop/videoplayer/main.c]
machine: either
human: look
since: ws122-p005a
---

## 目的
動画の player が F11・Alt+Enter・picture の double click で全画面になり、Esc・F11 で戻ることを確かめる（BUG-223、ws122-p005a）。

## 準備
kei で `videoplayer /tmp/aat-samples/sample.mp4` を開く。

## 操作と確認
1. 操作: picture を click してから F11。
   確認事項: 全画面。正解: `VIDEOPLAYER FULLSCREEN on=1`（F11 は compositor の menu の Full Screen の項が取ることもある）。確認方法: log、撮影（全画面、2 秒後に bar が消える）。
2. 操作: Esc。
   確認事項: 戻る。正解: `VIDEOPLAYER FULLSCREEN on=0`。確認方法: log。
3. 操作: Alt+Enter、もう一度 F11。
   確認事項: 入って出る。正解: `VIDEOPLAYER FULLSCREEN toggle via=alt-enter` と `on=1`、F11 で compositor の `ZWL GLASS fullscreen-leave … via=f11`（BUG-194）か `VIDEOPLAYER FULLSCREEN on=0`。確認方法: log。
4. 操作: picture を double click、もう一度 double click。
   確認事項: 入って出る。正解: `VIDEOPLAYER FULLSCREEN toggle via=double-click` が 2 回、`on=1` と `on=0`。確認方法: log。

## 合格
各段の log の行と、撮影で全画面の picture。
