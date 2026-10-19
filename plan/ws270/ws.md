<!-- awesome-plan project=zedbsd record=ws270 -->

# WS270 VisionFive 2 で Keiland の desktop を動かし、OS を完全に動かす

- ID: ws270、Master: [master.md](../master.md)、Primary Milestone: MG008
- Status: incomplete（2026-10-19 設計 p001 を P1 が開始（q1335、fg020）。旧: planning（2026-10-19 Q1 作成。2026-10-19 22:04 ユーザーの WS269 D5「(a) 画面なしで先行」: WS269 p010 までは service・seat・input・display の node の claim だけを進め、描画は WS269 p010（GPU の Vulkan）の後。Keiland は GPU の Vulkan 無しで描けない）
- 起点（2026-10-19 ユーザー）: 「VisionFive2はまずフレームバッファ、SDカードを実装して、rootfsから起動できるところまで持って行きます。USBキーボードで操作できるようにします。LANとサウンドとNVMeも使えるようにします。NVMe SSDがボードにつけてあり、テスト可能です。ここまでのphaseを作ってください。そのあと、PowerVRのドライバを独立WSで作ります。最終的に、VF2でkeilandを動かして、OSを完全に動作させます。」
- 担当: P1（設計 p001、2026-10-19 q1335）。

## 完了の条件（案、着手の時に確定する）

1. VisionFive 2 の実機で、SD か NVMe の rootfs から起動し、graphical の login から Keiland の desktop が動く（USB の keyboard・mouse、LAN、sound を使う）。
2. riscv64 の userland の desktop の package（Keiland と依存）が build でき、image に入る。
3. 他の port と同じ受け入れの試験（範囲は着手の時に決める）。

## 依存

- [WS256](../ws256/ws.md)（周辺の driver まで）、[WS269](../ws269/ws.md)（GPU）。

## Phase

| Phase | 中身 | 依存 | Status |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計（riscv64 の desktop の package の build、service・seat・input・display の claim の画面なしの立ち上げ、描画は WS269 p010 の後、Phase の分割）。コードは書かない | — | in-progress（設計の文書が出来た、design-reviewer 待ち。2026-10-19 P1、q1335、[design.md](design.md) 版 1、D1〜D5） |
| [p002](phase002/phase.md) | riscv64 の desktop の build の不足のうち設計に依らない物（userland-dynamic の include、package の arch の表、kwl_cycles、machine 名） | — | in-progress（2026-10-19 P1、q1336。試しの build の残り 3 件: keiland-preview（static）、libcurses・noct（toolchain）） |

残りの Phase は p001 の設計から作る。
