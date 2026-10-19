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

- [WS256](../ws256/ws.md) の p011（framebuffer の実機）・p013（clock・reset の基盤）・p015（rootfs からの起動の実機）・p020（USB keyboard、画面での試験の操作。p005 以降の実機の判定）・p022（CRG の実機の確かめ）。p004 は p013・p015・p022、p007 は p011 の後（[design.md](design.md) §6）。
- 予備 (i)（uncached の別名の写像）は WS269 p004 が作って持つ（2026-10-19 Q1 の判断、q1321）。WS256 p023 は DMAC の確かめで要れば WS269 p004 の物を使う。WS256 p023 は WS269 の依存ではない（GPU は非 coherent を既定にする、[design.md](design.md) §3.2）。

## Phase

| Phase | 中身 | 依存 | Status |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計（driver の構成・firmware・userland の Vulkan の道・Phase の分割）。コードは書かない | — | in-progress（改訂 3.1 が出来た、Q1 の差分の確認待ち。2026-10-19 P2、q1316・q1319・q1321・q1324、[design.md](design.md)、[レビュー 1](design-review-1.md)・[レビュー 2](design-review-2.md)・[レビュー 3](design-review-3.md)） |

残りの Phase は p001 の設計から作る。

## 資料

- StarFive の vendor kernel の clone（2026-10-19 ユーザー「~/にvisionfive-linuxとしてクローンしてくれるとうれしいです。」）: `~/visionfive-linux`（git@github.com:starfive-tech/linux.git、blob の部分 clone、branch `JH7110_VisionFive2_6.12.y_devel` 4cecf169f38e を checkout）。`drivers/gpu/drm/img/img-rogue/`（Imagination の DDK 1.19@6345021 の kernel の部分、Dual MIT/GPLv2、774 file）、JH7110 の glue は `services/system/rogue/sf_7110/`、BXE の core の定義は `hwdefs/rogue/km/cores/rgxcore_km_36.50.54.182.h`。同じ branch に mainline の `drivers/gpu/drm/imagination/` もある（JH7110 の compatible は無い）。DT は `arch/riscv/boot/dts/starfive/jh7110.dtsi` の `gpu@18000000`（compatible "img-gpu"）。参照の範囲は Guardrail の出典の規則（値は `Value(s) referenced:`、code・構造・名前は写さない）。

## 未決

- GPU の firmware の入手と license、userland の Vulkan の道（Mesa の imagination の driver の範囲と、BXE-4-32 の対応の状況は未確認）。
