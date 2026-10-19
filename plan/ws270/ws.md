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

## 決定（2026-10-20 03:25 ユーザー、クリック）

WS270-D1 (c)（config-visionfive2.mk を desktop、最小は config-visionfive2-server.mk、p007 で切り替え）、WS270-D2 (b)、WS270-D3 (a)、WS270-D5 (a)。D4 は欠番（riscv64 の Noct は F-114）。原文は Master の decisions-log。

## Phase

| Phase | 中身 | 依存 | Status |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計（riscv64 の desktop の package の build、service・seat・input・display の claim の画面なしの立ち上げ、描画は WS269 p010 の後、Phase の分割）。コードは書かない | — | cleared（2026-10-20 Q1、改訂 2.1。D は未決） |
| [p002](phase002/phase.md) | riscv64 の desktop の build の不足のうち設計に依らない物（userland-dynamic の include、package の arch の表、kwl_cycles、machine 名） | — | in-progress（2026-10-19 P1、q1336・q1338。試しの build は rc=0（noct・emacs を config から外し、libcurses・keiland-preview の static の規則を足した）、amd64・rpi4 の rootfs rc=0） |
| [p003](phase003/phase.md) | QEMU riscv64-virt の画面なしの確かめ（service・USB keyboard の login・mouse の event・display が無い時の道・`keiland-preview` の static の実行）。試験の config、rootfs 73 MiB（256 MiB の root に入る）、riscv64 の native の cfg に `login=graphical` | p002 | cleared（2026-10-20 Q1、T1-752） |
| [p004](phase004/phase.md) | VF2 の実機の画面なしの確かめ（graphical login の失敗の道 E → `gpu-bars`・`display-inventory` D、USB keyboard・mouse C）。試験の config（graphical login y、firmware なし）、native.img 404,750,336 B、rootfs 73 MiB、`display-inventory` の platform に visionfive2 | p003 | in-progress（2026-10-20 P1、q1355。image と手順は準備済み、実機待ち） |

残りの Phase は p001 の設計から作る。
