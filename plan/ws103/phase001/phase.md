<!-- awesome-plan project=zedbsd record=ws103-p001 -->

# ws103-p001: 調査と設計

- Parent: [WS103](../ws.md)
- Status: cleared
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

## 記録（2026-09-30 夜、q508-i01、メインのエージェント Q1）

成果: [design.md](../design.md)（改訂 3）。code・build・QEMU は行っていない（設計の Phase）。

| 基準 | 結果 |
| --- | --- |
| 1 ioctl ごとの置き換え | design §2.1〜2.4: 起動の問い合わせ → VK_KHR_display、`--direct` の削除（D2）、記述の照合 → libvulkan の dedicated の import の照合と bind の守り（D3 の具体化）、fence → WSI が present ごとに新しい fence を送り compositor は poll だけ |
| 2 fence の世代と i915 | i915 の native に `GPU_CAP_FENCE` が無く（`src/drivers/gpu/i915/session.h:38-47`）、5330 では `set_acquire_fence` は送られない（fence の道は Venus だけ）。世代の照合は、送る fence を reset しない形で不要にした（kernel・protocol の変更なし） |
| 3 OS の backend の境界 | design §2.5: `gpu-zedbsd.c` と `zwl-gpu.h`、module の入れ替え |
| 4 macro を外した build の確かめ方 | design §2.6: `v1-check.sh`（grep と毒の header の `-fsyntax-only`） |
| 5 V4 の見込みと測り方 | design §2.7 |
| 6 Phase の分け方 | design §3: p002〜p007 |
| 7 design-reviewer | 2 回（blocker 2・major 7・minor 10 → 改訂 2、再レビュー blocker 0・major 2・minor 5 → 改訂 3）。処理は design §5 |

D3 の具体化: ユーザーの決定「bind のとき libvulkan が照らす」を、規格の戻り値の制約から「dedicated の import（`vkAllocateMemory`）で照らし、bind でも守る」にした。
照らす主体が libvulkan で compositor が ioctl を持たない点は決定のまま。

ユーザーの確認が要る点（Phase の clear を妨げない）: ws.md の V1（「macro を外した build」）と V4（「macro の有り・無しの差」）の言い回しを、module の入れ替えと
p002 の前・p006 の後の比較に直すこと（design §2.6）。

HAL・toolchain・kernel・UAPI の変更は無い。
