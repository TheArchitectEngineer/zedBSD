---
id: desktop.windows.maximize-double-click
title: title bar の double click で最大化し、0.2 秒以内に描き直す
status: active
areas: [compositor, windows]
paths: [userland/desktop/wayland/shell.c, userland/desktop/wayland/toplevel.c]
machine: either
human: look
since: BUG-179
---

## 目的
double click で最大化し、中身が最大の大きさで描き直されるまでの時間を測る（BUG-179、目標 0.1〜0.2 秒）。

## 準備
App Home から Files を開く（浮いた窓）。

## 操作と確認
1. 操作: title bar の空いた所を double click。
   確認事項: 最大化。正解: `ZWL GLASS dock surface=S via=double-click …`。確認方法: log。
2. 操作: 描き直しを待つ。
   確認事項: 時間。正解: `ZWL GLASS resized surface=S docked=1 … after_ms=N` の N ≤ 200。確認方法: log の after_ms、撮影。

## 合格
最大化し、N ≤ 200。N が大きい時は値を書いて needs-person（QEMU は遅い）。
