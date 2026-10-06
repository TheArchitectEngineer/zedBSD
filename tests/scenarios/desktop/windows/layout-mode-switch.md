---
id: desktop.windows.layout-mode-switch
title: 最大化は desktop の tablet mode: 切り替え先も最大化、窓の mode では窓に
status: draft
areas: [compositor, windows, switcher]
paths: [userland/desktop/wayland/layout.c, userland/desktop/wayland/shell.c, userland/desktop/wayland/switcher-shell.c]
machine: either
human: none
since: ws142-p008
---

## 目的
最大化を窓ごとでなく session の状態（tablet mode）として扱うことを確かめる（BUG-217、ws142-p007 §1）。QEMU の自動の版は plan/ws142/tests/p010-guest.sh の 1.〜5.。

## 準備
Files と Terminal を開く（窓の mode）。

## 操作と確認
1. 操作: Terminal の title bar を double click。
   確認事項: mode。正解: Terminal が最大化、log `ZWL LAYOUT mode=docked reason=double-click`。確認方法: log、撮影。
2. 操作: Alt+Tab+Tab で Files へ。
   確認事項: 切り替え先。正解: Files も最大化（`ZWL LAYOUT switch … action=dock mode=docked via=switch`）、Terminal は描かれない。確認方法: log、撮影。
3. 操作: bar の restore の button。
   確認事項: mode。正解: Files が窓に戻り `ZWL LAYOUT mode=windowed`、Terminal（最大化のまま後ろ）が見える。確認方法: log、撮影。
4. 操作: Alt+Tab+Tab で Terminal へ。
   確認事項: 切り替え先。正解: Terminal が窓に戻る（`action=float mode=windowed`）。確認方法: log、撮影。
5. 操作: Terminal を最大化し、Terminal を閉じる。
   確認事項: 次の窓。正解: 前に来た Files が最大化（`ZWL LAYOUT front … action=dock`）。確認方法: log、撮影。

## 合格
各段の log の行と、撮影で最大化の間は今の app の窓だけが見えること。
