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
| [p001](phase001/phase.md) | 設計（driver の構成・firmware・userland の Vulkan の道・Phase の分割）。コードは書かない | — | cleared（2026-10-19 Q1、改訂 3.1。D は未決） |
| [p002](phase002/phase.md) | 資料の固定（FW の版と sha256、mainline の tag、Mesa の版、BRN、vendor の DT・DDK）。コードなし | p001 | cleared（2026-10-19 Q1、q1326） |
| [p003](phase003/phase.md) | firmware の package（mirror、sha256、license・WHENCE・manifest、initrd） | p002、D3 | cleared（2026-10-19 Q1、q1328。driver の既定 on は p004） |
| [p004](phase004/phase.md) | PMU（PD_GPUA）・CRG の GPU の行・組み込みの記述・riscv64 の uncached の別名（予備 (i)）・attach の早い段 | WS256 p013・p015・p022、p002 | in-progress（2026-10-19 P2、q1330。実装・build・host 試験 PASS、実機の 1 行は未実施） |
| p005 | 遅い段: FW の parser・MIPS の page table・FW の起動・health の kick・IRQ | p003、p004、WS256 p020 | planned |
| p006 | coherency の確かめ（MIPS の道）・GPU の MMU・resource・CCB・fence・recovery・render の node | p005 | planned |
| [p007](phase007/phase.md) | display の node（claim・present）と `scanout` の import | WS256 p011、p004、import は p006 | in-progress（2026-10-19 P1、q1333。前半（boot framebuffer の display の ops、自分の resource の claim・present、console の引き継ぎ）の実装・build・host 試験 PASS。残り: import（p006 の後）と実機の目視） |
| p008 | Rogue の USC・PDS の encoder、kernel の中の試験の道、data master の道の coherency | p006 | planned |
| p009 | 最小の Venus の decode（compute） | p008 | planned |
| p010 | graphics（TA・3D・transfer・render pass・WSI） | p009、p007 | planned |
| p011 | Keiland の要る範囲の残り（WS270 と合わせる） | p010 | planned |
| p012 | 規約の全文の見直し | p003〜p011 | planned |

中身・LW・確かめは [design.md](design.md) §6。

## 決定（2026-10-19 22:04 ユーザー、クリック）

D1 (a)、D2 (a)、D3 (a)、D5 (a)、D6 A、D7 (a)（D4 は Guardrail で決まっている）。原文は Master の decisions-log。

## 資料

- StarFive の vendor kernel の clone（2026-10-19 ユーザー「~/にvisionfive-linuxとしてクローンしてくれるとうれしいです。」）: `~/visionfive-linux`（git@github.com:starfive-tech/linux.git、blob の部分 clone、branch `JH7110_VisionFive2_6.12.y_devel` 4cecf169f38e を checkout）。`drivers/gpu/drm/img/img-rogue/`（Imagination の DDK 1.19@6345021 の kernel の部分、Dual MIT/GPLv2、774 file）、JH7110 の glue は `services/system/rogue/sf_7110/`、BXE の core の定義は `hwdefs/rogue/km/cores/rgxcore_km_36.50.54.182.h`。同じ branch に mainline の `drivers/gpu/drm/imagination/` もある（JH7110 の compatible は無い）。DT は `arch/riscv/boot/dts/starfive/jh7110.dtsi` の `gpu@18000000`（compatible "img-gpu"）。参照の範囲は Guardrail の出典の規則（値は `Value(s) referenced:`、code・構造・名前は写さない）。

## 未決

- GPU の firmware の入手と license、userland の Vulkan の道（Mesa の imagination の driver の範囲と、BXE-4-32 の対応の状況は未確認）。
