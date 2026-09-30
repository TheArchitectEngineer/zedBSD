<!-- awesome-plan project=zedbsd record=ws103-p001 -->

# ws103-p001: 調査と設計

- Parent: [WS103](../ws.md)
- Status: in-progress
- Disposition: normal
- Queue: q508-i01

## 目的と完了の基準

WS103 の決定 D1〜D3 の上で、compositor（`userland/desktop/wayland/`）の GPU の直の ioctl を全て無くす設計を作る。

1. ioctl ごとの置き換えを、code の場所と Vulkan の API・libvulkan の変更の単位で書く。
2. fence の世代の照合を libvulkan に移す方法と、i915 の native で external fence が使えるかを code で確かめて書く。
3. OS の backend の境界（protocol・import・fence の組）の形を書く。F-065 の Linux の組が後で入れられること。
4. macro を外した build の確かめ方を決める。
5. buffer ごとの import の費用（V4）の見込みと測り方を書く。
6. p002 以降の Phase の分け方（範囲・依存・確かめ）を書く。
7. design-reviewer のレビューを通し、指摘を処理する。

## 範囲の外

code の変更、`include/hal/hal.h`、toolchain、Linux・FreeBSD の backend、evdev の ioctl。

## 記録

（実行中）
