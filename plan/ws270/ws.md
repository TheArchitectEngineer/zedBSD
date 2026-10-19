<!-- awesome-plan project=zedbsd record=ws270 -->

# WS270 VisionFive 2 で Keiland の desktop を動かし、OS を完全に動かす

- ID: ws270、Master: [master.md](../master.md)、Primary Milestone: MG008
- Status: planning（2026-10-19 Q1 作成。WS256・WS269 の後）
- 起点（2026-10-19 ユーザー）: 「VisionFive2はまずフレームバッファ、SDカードを実装して、rootfsから起動できるところまで持って行きます。USBキーボードで操作できるようにします。LANとサウンドとNVMeも使えるようにします。NVMe SSDがボードにつけてあり、テスト可能です。ここまでのphaseを作ってください。そのあと、PowerVRのドライバを独立WSで作ります。最終的に、VF2でkeilandを動かして、OSを完全に動作させます。」
- 担当: 未割当。

## 完了の条件（案、着手の時に確定する）

1. VisionFive 2 の実機で、SD か NVMe の rootfs から起動し、graphical の login から Keiland の desktop が動く（USB の keyboard・mouse、LAN、sound を使う）。
2. riscv64 の userland の desktop の package（Keiland と依存）が build でき、image に入る。
3. 他の port と同じ受け入れの試験（範囲は着手の時に決める）。

## 依存

- [WS256](../ws256/ws.md)（周辺の driver まで）、[WS269](../ws269/ws.md)（GPU）。

## Phase

未定。
