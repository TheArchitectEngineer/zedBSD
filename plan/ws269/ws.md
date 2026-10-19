<!-- awesome-plan project=zedbsd record=ws269 -->

# WS269 VisionFive 2 の GPU（Imagination PowerVR BXE-4-32）の driver

- ID: ws269、Master: [master.md](../master.md)、Primary Milestone: MG008（関係: MG006）
- Status: incomplete（2026-10-19 Q1 作成。設計 p001 を 2026-10-19 に P2 が開始、実装は WS256 の実機の確認の後）
- 起点（2026-10-19 ユーザー）: 「VisionFive2はまずフレームバッファ、SDカードを実装して、rootfsから起動できるところまで持って行きます。USBキーボードで操作できるようにします。LANとサウンドとNVMeも使えるようにします。NVMe SSDがボードにつけてあり、テスト可能です。ここまでのphaseを作ってください。そのあと、PowerVRのドライバを独立WSで作ります。最終的に、VF2でkeilandを動かして、OSを完全に動作させます。」
- 担当: P2（設計 p001、2026-10-19 q1316）。

## 完了の条件（案、着手の時に設計で確定する）

1. JH7110 の PowerVR BXE-4-32 を kernel の driver が認識し、電源・clock・reset を入れ、firmware を載せて GPU を起こす。
2. Keiland が使う Vulkan の範囲（WS270 の desktop に要る物）で描画できる。範囲（Mesa の powervr の userland を使うか、自前か）は設計で決める。
3. VisionFive 2 の実機で確かめる。

## 依存

- [WS256](../ws256/ws.md) の p011（framebuffer）・p013（clock・reset の基盤）・p015（rootfs からの起動）。

## Phase

| Phase | 中身 | 依存 | Status |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計（driver の構成・firmware・userland の Vulkan の道・Phase の分割）。コードは書かない | — | in-progress（2026-10-19 P2、q1316） |

残りの Phase は p001 の設計から作る。

## 未決

- GPU の firmware の入手と license、userland の Vulkan の道（Mesa の imagination の driver の範囲と、BXE-4-32 の対応の状況は未確認）。
