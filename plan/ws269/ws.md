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
| [p005](phase005/phase.md) | 遅い段: FW の parser・MIPS の page table・FW の起動・health の kick・IRQ | p003、p004、WS256 p020 | in-progress（2026-10-19 P2、q1332 前半。実装・build・host 試験 PASS、実機は p004 の `ALIAS OK` の後。2026-10-20 P1 q1356: 遅い段が gpu1 の登録・失敗まで `hw.gpu.attaching` を数える必須項目を実装、build・host 試験 PASS） |
| [p006](phase006/phase.md) | coherency の確かめ（MIPS の道）・GPU の MMU・resource・CCB・fence・recovery・render の node・`share` | p005 | in-progress（2026-10-19 P2、q1337。実装・build・host 試験 PASS、実機は p005 の実機の後。display の node 向けの lookup の API は `render.h`。2026-10-20 P2 q1344: WS277 の R3（shareable の blob を常に SLC_BYPASS）・R5（`blob_create_placed`）を追加、build・host 試験 PASS、実機待ち） |
| [p007](phase007/phase.md) | display の node（claim・present）と `scanout` の import | WS256 p011、p004、import は p006 | in-progress（2026-10-19 P1 q1333 前半、P2 q1340 後半: import（p006 の lookup で retain、uncached の view から全面の CPU の copy、SHARED・FOREIGN、BLOB の present）の実装・build・host 試験 PASS。damage は ABI に欄が無く全面。残り: 実機の目視（未依頼）） |
| [p008](phase008/phase.md) | Rogue の USC・PDS の encoder、kernel の中の試験の道、data master の道の coherency | p006 | in-progress（2026-10-20 P2、q1345 前半: USC・PDS の encoder と host 試験 PASS。q1349 後半: driver の compute の program（`compute-program.c`）と試験の kernel の check（`compute-check.c`、`CONFIG_POWERVR_COMPUTE_CHECK`）、host 試験 PASS 91+70、VF2（試験・通常）・riscv64-virt の build warning 0。実機の 3 行（kernel・data master の coherency・temps の単位）は U29 の続きで実機待ち） |
| [p009](phase009/phase.md) | 最小の Venus の decode（compute） | p008 | in-progress（2026-10-20 P2、q1352: capset（1.3.269、flags 7、job の caps）・frame・job・executor（instance〜submit）・SPIR-V の subset（`data[x+k] = a·x + c`）・fill の USC の program、host 試験 PASS 69+109、VF2・riscv64-virt・amd64 の build warning 0。q1357: 試験の program `vkcompute`（host の lavapipe で PASS）と VF2 の試験の image の config（`tests/p009/config-visionfive2-vkcompute.mk`、build warning 0、libvulkan は config で選ぶだけで入った）。**実機待ち**。WS277 R7 の文面に食い違い（Q1 へ）） |
| [p010](phase010/phase.md) | graphics の設計の追補（[design-p010.md](design-p010.md) 版 2.1、分割と境と UAT の行）。実装は下の p010a〜o。依存は名前を付けた出力（O-P006 等、design-p010.md §10） | p010b、O-P006・O-P007・O-P009（design-p010.md §10） | in-progress（2026-10-20 P1 q1370: 版 2.1（review 2 の反映）、Q1 の差分の確認待ち） |
| [p010a](phase010a/phase.md) | UAT の段の行（`powervr: <段>: ok\|fail(<理由>)`、sysctl）と `gpu-selftest` の骨 | p009 | in-progress（2026-10-20 P1 q1364: stage.c・stage-log.c、sysctl `hw.gpu.powervr.stages`、firmware・render-node・compute の段、gpu-selftest（kernel・node・vkcompute の 3 段、残り skip(not built)）、VF2 の試験の image の config。host 試験 PASS、VF2・riscv64-virt・amd64 の build warning 0。**実機待ち**） |
| [p010b](phase010b/phase.md) | 資料の固定（Mesa・mainline の path と SHA、BXE の feature の分岐）。コードなし | p002 | cleared（2026-10-20 Q1、q1365・q1366） |
| [p010c](phase010c/phase.md) | image（LINEAR rowPitch 32・twiddled）・format・view・sampler、shared の道に入らせない host 試験、M9 の照合 | p010b、O-P009 | in-progress（2026-10-20 P1 q1368: image-state.c・vk-image.c・format の照会・vkimage と gpu-selftest の image の段。linear は SAMPLED だけ外す（review 2 Minor 2）。host 試験 PASS（executor 157・image 55）、M9 0 mismatches、VF2・riscv64-virt・amd64 の build warning 0。**実機待ち**） |
| p010d | render の FW の object（前半）: heap、global・local の free list、HWRT、GROW・RECONSTRUCTION の応答、CLEANUP | p010b、O-P006、O-P009 | planning（同。版 2.1 で static data area・cleanup の上限を含む） |
| p010n | render の FW の object（後半）: render context の static state と SR・PT、4 つの context の identity・recovery、geometry・FRAG_PR・fragment の kick、render check の clear | O-p010d、O-ENC（SR の 1 点、design-p010.md §5.4） | planning（同。版 2.1 で実機の確かめは p010e へ） |
| p010e | PBE の store・EOT・固定の program・kernel の render check の三角形 | O-p010c、O-p010n | planning（同） |
| p010f | transfer（copy・clear、IPF・FAST_2D） | O-p010c、O-p010d、O-p010n | planning（同。版 2.1 で transfer の context の recovery・sync の word を含む） |
| p010o | transfer の blit（FAST_SCALE） | O-p010f | planning（同） |
| p010g | render pass・framebuffer・load/store・clear・job の列 | O-p010e | planning（同） |
| [p010-compiler](phase010-compiler/phase.md) | SPIR-V から USC への compiler（spirv-fill の置き換え、compute・vertex・fragment、color の pack・blend は D-P3 (b) で compiler） | p008、p009 | in-progress（2026-10-20 P2 q1363: 読み・back end・key（format・blend）・compute の executor への差し替え、host 試験 PASS、VF2・riscv64-virt の build warning 0） |
| p010h | descriptor と compiler の境（`shader.h` の使用、PDS の data の program） | O-p010c、O-SHADER-ABI | planning（同） |
| p010i | graphics の pipeline と draw | O-p010g、O-p010h、O-VSFS | planning（同） |
| p010j | depth と vkdemo の offscreen | O-p010i | planning（同） |
| p010k1 | WSI の readback の道（clear だけ、compiler なし） | O-p010f、O-P007 | planning（同） |
| p010k2 | readback の道で vkdemo が画面に | O-p010k1、O-p010j | planning（同） |
| p010l | WSI の shared の道と p007 の import の試験（WS277 R6）、dc8200 の zero copy | O-p010k2、O-p010o | planning（同） |
| p010m | 仕上げ、T1 の回帰、p010 の受け入れ（vkdemo と gpu-selftest の clear・copy・draw、D-P7） | O-p010l | planning（同） |
| p011 | Keiland が使う Vulkan の機能の不足を driver 側で埋める（受け入れは機能の試験。desktop の目視は WS270 p006、WS269 p011 → WS270 p006） | p010m | planned |
| p013 | USC の encoder（`usc-encode.c`）の広げ: SMP・FITR/FITRP・EMITPIX・EOT・SR/PT・transfer に要る命令（持ち主は P2）。出力 O-ENC（main に merge 済みで host 試験 PASS） | p008 | planning（2026-10-20 P1 q1367 で作った。P2 の q1363 の範囲、phase.md と LW は P2） |
| p014 | SPIR-V から USC の compiler（compute の一般、vertex・fragment、pack と blend、compile の入力は design-p010.md §5.1）。出力 O-SHADER-ABI（`shader.h`）・O-VSFS（vkdemo・gpu-selftest の vertex・fragment） | p009、O-ENC | planning（同。q1363 が実行中） |
| p012 | 規約の全文の見直し | p003〜p011・p013・p014 | planned |

中身・LW・確かめは [design.md](design.md) §6。

## 決定（2026-10-19 22:04 ユーザー、クリック）

D1 (a)、D2 (a)、D3 (a)、D5 (a)、D6 A、D7 (a)（D4 は Guardrail で決まっている）。原文は Master の decisions-log。

## 資料

- StarFive の vendor kernel の clone（2026-10-19 ユーザー「~/にvisionfive-linuxとしてクローンしてくれるとうれしいです。」）: `~/visionfive-linux`（git@github.com:starfive-tech/linux.git、blob の部分 clone、branch `JH7110_VisionFive2_6.12.y_devel` 4cecf169f38e を checkout）。`drivers/gpu/drm/img/img-rogue/`（Imagination の DDK 1.19@6345021 の kernel の部分、Dual MIT/GPLv2、774 file）、JH7110 の glue は `services/system/rogue/sf_7110/`、BXE の core の定義は `hwdefs/rogue/km/cores/rgxcore_km_36.50.54.182.h`。同じ branch に mainline の `drivers/gpu/drm/imagination/` もある（JH7110 の compatible は無い）。DT は `arch/riscv/boot/dts/starfive/jh7110.dtsi` の `gpu@18000000`（compatible "img-gpu"）。参照の範囲は Guardrail の出典の規則（値は `Value(s) referenced:`、code・構造・名前は写さない）。

## 未決

- GPU の firmware の入手と license、userland の Vulkan の道（Mesa の imagination の driver の範囲と、BXE-4-32 の対応の状況は未確認）。

## ユーザーの判断（推奨で進めている、2026-10-20 07:24 のユーザーの不在の指示による）

design-p010.md §9 の D-P1〜D-P7（版 2）: D-P1 free list は global 固定 16 MiB、FW の grow の要求には delta 0 で応え RECONSTRUCTION にも応える（尽きた時の SPM は p011）、D-P2 gpu-selftest は VF2 の試験の image だけに入れ、起動の script が Keiland の前に流す、D-P3 は (b) compiler が pack と blend（Q1 の判断、版 2）、D-P4 submit は ioctl の中で待つ（2 秒、時間切れの後は HWRT・free list を core に残す。版 2.1）、D-P5 段の行は英語の短い語。ユーザーが戻ったら確かめる。
- D-P6（2026-10-20 Q1 が足した、design-p010-review-1 B1）: p010a で main に入った read-only の sysctl `hw.gpu.powervr.stages`（`include/uapi/sysctl.h` の HW_GPU_STAGES 11、`src/kern/sysctl.c`）は新しい UAPI。design.md §2「新しい UAPI は作らない」と食い違う。推奨: 試験の段の報告だけの読み取りの leaf として残す（代わり: 外して gpu-selftest は kern.msgbuf の `powervr: ` の行を読む）。ユーザーが戻ったら確かめる。
- D-P7（2026-10-20 版 2、review 1 の M5）: p010 の受け入れから venus-frame を外し、vkdemo の画と gpu-selftest の clear・copy・draw にする（design.md §6 の文面の変更。VF2 では gpu0 が display、gpu1 が render で、venus-frame は 1 つの fd で Venus と present をする）。推奨: そうする。ユーザーが戻ったら確かめる。D-P3 は review の B4 で (b)「compiler が pack と blend を行う」に変える（Q1 の判断、P2 に伝えた）。
