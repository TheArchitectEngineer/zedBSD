<!-- awesome-plan project=zedbsd record=ws269 -->

# WS269 VisionFive 2 の GPU（Imagination PowerVR BXE-4-32）の driver

- ID: ws269、Master: [master.md](../master.md)、Primary Milestone: MG008（関係: MG006）
- Status: incomplete（2026-10-19 Q1 作成。設計 p001 を 2026-10-19 に P2 が開始、実装は WS256 の実機の確認の後）
- 起点（2026-10-19 ユーザー）: 「VisionFive2はまずフレームバッファ、SDカードを実装して、rootfsから起動できるところまで持って行きます。USBキーボードで操作できるようにします。LANとサウンドとNVMeも使えるようにします。NVMe SSDがボードにつけてあり、テスト可能です。ここまでのphaseを作ってください。そのあと、PowerVRのドライバを独立WSで作ります。最終的に、VF2でkeilandを動かして、OSを完全に動作させます。」
- 担当: P2（設計 p001、2026-10-19 q1316）。

## 現在の状態（2026-10-21、再開点）

- 板（VF2 の実機、板 96、SD image は main から作り直し中）:
  - kernel の自己試験（compute check、render check の terminate-state・三角形・transfer・job check）が全部 ok。
  - gpu-selftest は 13 ok / 0 fail / 0 skip。
  - vkcoherency は PASS。
  - vkmatrix（Keiland の Vulkan の範囲、15 case）は 15/15。
  - context reset は 0。
  - HDMI に vkdemo の cube の animation を出した。
- p010q は cleared。原因と直しの要約は [p010q](phase010q/phase.md) の先頭:
  - TA: 末尾の terminate の PPP state。
  - compute: Mesa の PDS と 3 kernel。
  - LOAD: 全画面の draw。
- 安定性（p015、2026-10-21）: 1 回の起動で連続 10 回と電源の入れ直し 3 回が全部 13/13・15/15・PASS・reset 0。
- Keiland: WS270 p006a の 2b の desktop の SD image（beta2.md U36）を作った。板の確認は SD の書き込みの後。
- 再開点: 次の Phase（[proposal-next-phases.md](proposal-next-phases.md) の N2 安定性・N3 Keiland の範囲・N4 規約の全文の見直しの 2 回目・N5 docs・N6 594 MHz）。ID は Q1 が、ユーザーの判断の後に割り当てる。

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
| [p010a](phase010a/phase.md) | UAT の段の行（`powervr: <段>: ok\|fail(<理由>)`、sysctl）と `gpu-selftest` の骨 | p009 | in-progress（2026-10-20 P1 q1364: stage.c・stage-log.c、sysctl `hw.gpu.powervr.stages`、firmware・render-node・compute の段、gpu-selftest（kernel・node・vkcompute の 3 段、残り skip(not built)）、VF2 の試験の image の config。host 試験 PASS、VF2・riscv64-virt・amd64 の build warning 0。**実機待ち**）。2026-10-20 P2 q1376: vkstage（clear・copy・draw・sample・depth・present）と gpu-selftest の接続、host 試験 PASS、build warning 0、実機・QEMU 未実施 |
| [p010b](phase010b/phase.md) | 資料の固定（Mesa・mainline の path と SHA、BXE の feature の分岐）。コードなし | p002 | cleared（2026-10-20 Q1、q1365・q1366） |
| [p010c](phase010c/phase.md) | image（LINEAR rowPitch 32・twiddled）・format・view・sampler、shared の道に入らせない host 試験、M9 の照合 | p010b、O-P009 | in-progress（2026-10-20 P1 q1368: image-state.c・vk-image.c・format の照会・vkimage と gpu-selftest の image の段。linear は SAMPLED だけ外す（review 2 Minor 2）。host 試験 PASS（executor 157・image 55）、M9 0 mismatches、VF2・riscv64-virt・amd64 の build warning 0。**実機待ち**） |
| [p010d](phase010d/phase.md) | render の FW の object（前半）: heap、global・local の free list、HWRT、GROW・RECONSTRUCTION の応答、CLEANUP | p010b、O-P006、O-P009 | in-progress（2026-10-20 P1 q1373: render-target.c・render-target-layout.c、FW の record の offset を v7.3 の check header と static assert、global 16 MiB・local 32 page、HWRT 4 個まで、GROW（delta 0）・RECONSTRUCTION・UPDATE_STATS、CLEANUP の上限 50 回・2 秒、PDS・USC・TRANSFER_FRAG の space。host 試験 PASS（target 77）、VF2・riscv64-virt・amd64 の build warning 0。GENERAL の static data area は BUG-342 の側） |
| [p010n](phase010n/phase.md) | render の FW の object（後半）: render context の static state と SR・PT、4 つの context の identity・recovery、geometry・FRAG_PR・fragment の kick、render check の clear | O-p010d、O-ENC（SR の 1 点、design-p010.md §5.4） | in-progress（2026-10-20 P1 q1375-i01: 部分。q1377-i01: 残りの 1〜5（session の render context、SR・PT の PDS と USC、117・101 の kick と sync の word、全 context の recovery、時間切れの段・`ta: fail(free list)`・空の render・render check の骨）を実装。host 試験 p010 kick 81/0・program 38/0 ほか全部 0 failed、p009 162/0、p006 68/0、compiler 318/0、M9 57 語 0 mismatches、VF2・riscv64-virt・amd64 の build warning 0。受け入れの判定は Q1、実機は p010e。compute の CDM の cswitch の state は未着手。q1388-i01: compute の CDM の cswitch state） |
| [p010e](phase010e/phase.md) | PBE の store・EOT・固定の program・kernel の render check の三角形 | O-p010c、O-p010n | in-progress（2026-10-20 P2 q1378-i01 前半: render-program.c（EOT・pixel event・clear の background・三角形の USC と PDS・VDM・PPP・PBE の word と clip）、host 試験 26 PASS、M9 87 word 0 mismatch、VF2 build warning 0。q1380-i01（P1）: render-command.c（fragment・geometry の register）と render check の kick・fence・読み返し・段ごとの行、試験の image で有効、host 試験 command 22/0 ほか全部 PASS、M9 63 語 0 mismatch、3 つの build warning 0。実機は未確認（UAT 待ち）） |
| [p010f](phase010f/phase.md。q1383-i01（P1）: transfer context・kick・`transfer` の段、transfer-job.c、vk-transfer.c（copy 4 種と clear、同じ大きさの blit）、kernel の transfer check) | transfer（copy・clear、IPF・FAST_2D） | O-p010c、O-p010d、O-p010n | in-progress（2026-10-20 P2 q1379-i01 前半: transfer-program.c（transfer の register・copy の USC・shared の word・clear の色）、Mesa の道に従い FAST_SCALE と背景の object（設計の FAST_2D と食い違い、Q1 の確認待ち）、host 試験 15 PASS、M9 77 word 0 mismatch、VF2 build warning 0。後半（context・kick・実機）は p010n の merge の後） |
| p010o | transfer の blit（FAST_SCALE） | O-p010f | in-progress（2026-10-20 P2 q1381-i01 前半: transfer-program.c に blit の program・shared の word（scale・flip・filter）・register、host 試験・M9 PASS、記録は [phase010f](phase010f/phase.md) の q1381 の節。vkCmdBlitImage への組み込みは後半。q1385-i01（P1）: blit を transfer-job・vk-transfer に組み込み（記録は phase010f）） |
| [p010g](phase010g/phase.md。q1383-i01（P1）: render-job.c で draw の無い render pass を kick（clear・load の background、PBE の store）、vk-queue の ENOTSUP を置き換え) | render pass・framebuffer・load/store・clear・job の列 | O-p010e | in-progress（2026-10-20 P2 q1382-i01 前半: render-pass.c（pass・job の begin・clear attachments）と vk-pass.c（render pass・framebuffer・begin・clear の record・job の列）、2 subpass は OUT_OF_HOST_MEMORY、部分の area は depth の STORE だけ拒否、host 試験 15 PASS、VF2 build warning 0。kick への受け渡しは p010e の後半の後） |
| [p010-compiler](phase010-compiler/phase.md) | SPIR-V から USC への compiler（spirv-fill の置き換え、compute・vertex・fragment、color の pack・blend は D-P3 (b) で compiler） | p008、p009 | in-progress（2026-10-20 P2 q1363、p013・p014 の最初の試行の詳細の記録: 読み・back end・key（format・blend）・compute の executor への差し替え、host 試験 PASS、VF2・riscv64-virt の build warning 0） |
| [p010h](phase010h/phase.md) | descriptor と compiler の境（`shader.h` の使用、PDS の data の program） | O-p010c、O-SHADER-ABI | in-progress（2026-10-20 P2 q1374: descriptor-set.c（set の並び・buffer 16・image 32（meta と PCK_INFO）・sampler 48（gather）・combined 80）、data-program.c（要求の表から shared の DOUTD/DOUTW の program、vertex の DDMAD と index の DOUTW の program）、set の memory は CPU の写し（heap は p010i で P1 の API に）、compute の inline は据え置き（理由は phase.md）。host 試験 data 83/0・executor 162/0、VF2・riscv64-virt・amd64 の build warning 0。Q1 の判定待ち） |
| [p010i](phase010i/phase.md) | graphics の pipeline と draw | O-p010g、O-p010h、O-VSFS | in-progress（2026-10-20 P2 q1384-i01 前半: render-draw.c（固定の state・key・coefficient の PDS・viewport・scissor・PPP・VDM）と vk-draw.c（graphics pipeline の decode と compile、draw の record）、job への draw の付加、host 試験 30 PASS、M9 41 word 0 mismatch、VF2 build warning 0。GPU の memory への配置と kick への受け渡しは後半。q1385-i01（P1）: render-job が draw を kick に載せる（PDS・USC・set・push・scissor・PPP・VDM）、実機は未確認。q1388-i01: render-job の draw の host 試験 16/0） |
| [p010j](phase010j/phase.md) | depth と vkdemo の offscreen | O-p010i | in-progress（2026-10-20 P2 q1387-i01 前半: render-depth.c（ZLS の word・clear の値）、render-pass.c の depth の受け入れ（部分の area は load を足す、版 2.3）、vkstage の depth の pipeline の compile、host 試験 12 PASS、M9 12 word 0 mismatch、VF2 build warning 0。fragment の command への受け渡しは後半。q1388-i01（P1）: ZLS を render-job の fragment の command に、depth の ENOTSUP を外した） |
| [p010k1](phase010k1/phase.md) | WSI の readback の道（clear だけ、compiler なし） | O-p010f、O-P007 | in-progress（2026-10-20 P2 q1386-i01: libvulkan の readback の道が PowerVR で通る順を確かめた（vk の features の変更は不要）、vkstage present を semaphore・fence・3 秒に、VF2 の image build warning 0。実機は P1 の q1385 の後） |
| [p010p](phase010p/phase.md) | format の広げ（純粋な関数の側） | O-p010f | in-progress（2026-10-20 P2 q1389-i01: R32_SFLOAT・R16G16B16A16_SFLOAT・float の texture code、clear の half/raw の pack。host 全 PASS、f16 は Mesa と一致、VF2 warning 0。q1518-i01（P2、2026-10-21）: raw 64/128 bit の copy（transfer）と R16G16B16A16_SFLOAT の colour attachment（compiler の half の pack、PBE の F16）を実装。host 全 0 failed、vkmatrix に copy-64・copy-128・render-f16（lavapipe 18/18）、VF2 warning 0。板は Q1。q1388-i01（P1）: gpu-selftest の present（vkdemo、depth が通るまで skip(depth)）、vkdemo を VF2 の試験の image に） |
| [p010q](phase010q/phase.md) | 実機の前の点検（cache・MMU・SLC・順・時間切れの後） | O-p010n、O-p010e、O-p010f | cleared（2026-10-21 Q1。板 96: vkmatrix 15/15、gpu-selftest 13/13、context reset 0。結論と残件の行き先は phase.md の先頭。以前の経過: 2026-10-21 N1: clear の条件をそろえた（TA・3D・compute・transfer・present・gpu-selftest 13/13・vkcoherency PASS）。vkmatrix（板 96）の後に Q1 が判定する。残件の行き先は phase.md の「clearance の記録」。2026-10-21: 板で TA・3D・render check・job check が通り（板 78、q1484 の terminate state）、gpu-selftest は 12/13。残り: compute の既定の PDS（P2 q1496）、fence kernel の lockup、mesa-form の probe の整理、compute-check の診断の行の整理、BUG-344。結論の要約は phase.md の先頭。以前の経過: 2026-10-20 P1 q1395-i01: 全部の kick の道を mainline・Mesa に照らした表、kick の拒否（quarantine・stopping・lost・isolated・abandoned）を render・transfer・compute に足した。build warning 0、p009 175/0、p010 0 failed。q1398-i01: vkstage・vkdemo の frame の host 試験を p009 に足し、pipeline の static な viewport・scissor が使われていなかった誤りを render-job.c で直した。p009 509/2（Keiland の古い期待 2 つ、P2 q1397）、p010 0 failed、VF2 warning 0。実機は未。q1399-i01 P2: P1 の job と P2 の関数の約束の点検、render の CLEAR の値が float の bit のまま背景に入っていた誤りを render-pass.c で直した（format の clear の word に）。p009 511/0、VF2 warning 0。P1 へ vertex の DMA の words と blit の odd stride。q1401-i01 P1: vertex の DMA を format の大きさに（不足は既定）、奇数 stride の linear の dst への blit を行ごとに。p009 511/0、p010 0 failed、VF2 warning 0） |
| [p010k2](phase010k2/phase.md) | readback の道で vkdemo が画面に | O-p010k1、O-p010j | planning（2026-10-20 P1 q1391-i01: host の llvmpipe で vkdemo の期待の画（背景 101828、中央 e06895 @0 ms）と present の判定・UAT の文面の案） |
| [p010l](phase010l/phase.md) | WSI の shared の道と p007 の import の試験（WS277 R6）、dc8200 の zero copy | O-p010k2、O-p010o | in-progress（2026-10-20 P2 q1390-i01: external の image・export・import・identity を受ける、export/import は SLC_BYPASS、linear の SAMPLED は 1 か所の切り替えで off のまま。p009 の試験は切り替え 0・1 で PASS、VF2 warning 0。実機は未実施） |
| p010m | 仕上げ、T1 の回帰、p010 の受け入れ（vkdemo と gpu-selftest の clear・copy・draw、D-P7） | O-p010l | planning（同） |
| [p011](phase011/phase.md) | Keiland が使う Vulkan の機能の不足を driver 側で埋める（受け入れは機能の試験。desktop の目視は WS270 p006、WS269 p011 → WS270 p006） | p010m | in-progress（2026-10-20 P2 q1392-i01・q1394-i01: 突き合わせの表、16 draw を越える pass の継続、Keiland に似た frame の host 試験で graphics pipeline が全部拒まれていた不具合（module の compute の status）を直した。host 240 passed、VF2 warning 0。P1 へ: vk-queue の継続 job の順と QUEUE_JOBS_MAX（両方 P1 が直した: QUEUE_JOBS_MAX 64、begin の位置で継続 job を順に）。q1518-i01: 表の残りを見直し、driver 側の未対応は無くなった（Keiland の format は 8-bit の 2 つで、F16 の attachment と raw の copy は p010p で追加）。板の確認は Q1（vkmatrix 18 case）） |
| [p013](phase013/phase.md) | USC の encoder（`usc-encode.c`）の広げ: SMP・FITR/FITRP・EMITPIX・EOT・SR/PT・transfer に要る命令（持ち主は P2）。出力 O-ENC（main に merge 済みで host 試験 PASS。中身は design-p010 版 2.1 §5.4 で確定） | p008 | in-progress（2026-10-20 P2: 最初の試行 q1363（記録は p010-compiler）で compiler の命令を追加、host 試験 PASS。q1369 で phase.md。q1372 で SR の shared register の burst が今の encoder で書けることを host 試験で確認、O-ENC の判定は Q1） |
| [p014](phase014/phase.md) | SPIR-V から USC の compiler（compute の一般、vertex・fragment、pack と blend、compile の入力は design-p010.md §5.1）。出力 O-SHADER-ABI（`shader.h`、最初の出力、O-ENC に依らない）・O-VSFS（vkdemo・gpu-selftest の vertex・fragment） | p009（O-VSFS は O-ENC も） | in-progress（2026-10-20 P2: 最初の試行 q1363（記録は p010-compiler）、host 試験 PASS。q1369 で phase.md。q1371 で `shader.h` に改名。q1372 で VertexIndex・InstanceIndex、shader-spirv.c を 4 つに分割、未実装の要否の表（残りは p011 へ記録）、host 試験 PASS、build warning 0。gpu-selftest の shader が残り） |
| [p015](phase015/phase.md) | 安定性（連続実行・電源の入れ直し・reset からの回復・長時間） | p010q | in-progress（2026-10-21 Q1: 板で連続 10 回と電源の入れ直し 3 回が全部 13/13・15/15・coherency PASS・reset 0。板 98: vkreset で lockup → context reset → 次の process の compute・draw が通る。長時間はユーザーの確認待ち） |
| [p016](phase016/phase.md) | VF2 の GPU の文書（docs/reference/visionfive2-gpu.md: Vulkan の範囲・memory・firmware と license・起動の段・試験の program） | p015 | in-progress（2026-10-21 P1 q1521-i01: 文書を書いた。q1520 の後に F16・128 bit の記述を見直す） |
| [p012](phase012/phase.md) | 規約の全文の見直し | p003〜p011・p013・p014 | in-progress（2026-10-20 P1 q1396-i01: P1 の file の前半、19 か所を直した（関数の結果の直の return、条件の中の呼び出し、file の先頭の出典、source の plan/ の参照）。build warning 0、p009 175/0、p010 0 failed。P2 q1397-i01: P2 の file の後半を同じ型で直した（compiler の return・条件の中の呼び出し・出典の位置・design の参照、vkstage）、p009 240/0、p010・compiler 0 failed、VF2 warning 0） |

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

## 知見と制約（hardware と firmware の事実、2026-10-20〜21、板 47〜96）

- 「板 N」は VF2 の実機の起動の番号。dmesg は Q1 の scratchpad にあり、git には入っていない。
- commit は main に merge 済みの SHA。
- 詳しい経過は [phase010q](phase010q/phase.md)（P1）と [phase008](phase008/phase.md)（P2）の各 Queue の節にある。

### 1. 電源と clock

- **PMU の hardware event の mask**: JH7110 の PMU の hardware event の turn-on mask（PMU 0x04 の bit 7）が GPU を mask していると、GPU の job は fault を出さずに止まる。
  - 印は firmware の trace の POW「HW Request … Aborted … retrying」。
  - `vf2-gpu.c` の power_up は、domain を入れた後に event mask を 0 にする。power_down は切る前に 0xffffffff にする。vendor の runtime resume・suspend と同じ（Q1、2026-10-20）。
- **POW_RASCALDUST**: firmware の system flags の bit 4 を立てる。firmware が Rascal と dust の電源を管理する（Q1 の直し）。
- **PDS・USC の exec base**: POW_RASCALDUST の下では、電源が切れると exec base が消えるので、kick ごとに送り直す。
- **clock**:
  - vendor の既定は 594 MHz（PLL2 ÷ 2）。我々の既定は 396 MHz（÷ 3）。
  - `POWERVR_VF2_CORE_594` で 594 MHz にできる。CRG の gpu_core の divider の最小を 2 にした（q1477・q1478、7440a6a75）。
  - 594 MHz にしても TA の lockup は変わらない（板 73）。
- **起動列**: DDK 1.19 の RGXStart と行ごとに一致する。MIPS では MMU_CBASE_MAPPING を書かない（q1475、ea6fa44dd）。

### 2. firmware

- **firmware の版**: linux-firmware の `rogue_36.50.54.182_v1.fw`（1.1.OS@6976702、mainline の ABI）を使う。
  - DDK 1.19 の firmware（6345021）とは別物。
  - command の layout は mainline のもので、geometry の command は 112 byte。IMG の TA の command は 64 byte の別の形（q1465）。
- **config**: system flags 0x20010（POW_RASCALDUST と CSW MEDIUM）、OS config flags 0xF（TDM・GEOM・3D・CDM の context switch）。
  - Linux の vendor の driver と同じ値（q1463、c69e90e34）。
  - context switch を切っても結果は同じだった（板 59・66）。
- **KCCB の完了の待ち**: firmware は KCCB の return slot を書かない。os_data の `kccb_cmds_executed` を待つ（session の stop の cleanup も同じ。q1406〜、q1450）。
- **EVENT_STATUS の bit 4**（ISP_END_MACROTILE）:
  - firmware の起動前は 0（板 70）。
  - 最初の 3D の job の後は立ったままになるが、害は無い（q1477）。
- **host から読む register**: BIF・CLK_CTRL・CAT_BASE などは 0 に見える。
  - Linux でも同じで、正常。host の probe では判定できない（板 55、q1460）。
- **trace の decode**: `plan/tools/vf2/sfdec.py <mainline の pvr_rogue_fwif_sf.h> <dmesg>` を使う（Master の Tools に登録）。
  - id は number | group<<12 | 引数の数<<16 | 0x70000000。
- **context reset の通知（105）**: その context の session だけを lost にする。
  - device を lost にするのは、firmware の fault（112）、restart の要求、restart の失敗だけ（q1450、1511943ec、板 81）。

### 3. TA・3D（要点）

- **TA の stream の終わり**（TA の lockup の原因と修正）: VDM の control stream は、終わりに PPP_STATE0/1 の block で terminate の PPP state（3 語）を指してから、STREAM_TERMINATE で終える。
  - 3 語は次のとおり。
    - header: pres_terminate（bit 25）
    - TERMINATE0: clip の right（bit 18〜26）と bottom（bit 0〜8）。どちらも 32 px の block の数 −1
    - TERMINATE1: 0
  - Mesa の `pvr_sub_cmd_gfx_build_terminate_ctrl_stream` と同じ形。
  - この block が無いと、firmware の HWR が lockup になる（fault は出ない）。
  - 板 77 で TA が初めて完走し、1e92b88cf で全 stream に入れた。板 78 で三角形と job check が通った。
- **geometry の register**（q1482、4bb158b50。IMG の capture と一致）:
  - TE_PSG 0x20000（completeonterminate）
  - PPP_CTRL 0x202
  - TPU の border colour table の address
  - resume の size は Mesa の VDMCTRL_PDS_STATE0
  - flags 3（FIRST|LAST。SINGLE_CORE は立てない）
- **HWRT の record**: 全 field が Mesa・mainline と一致する（q1480、259097c5e）。
- **region headers**: 専用の window の 0xDBF0000000（256 MiB）に置く（6079f96cc）。
- **free list の entry**: PM_META_PROTECT（bit 62）と SLC bypass で map する（1e117cef4）。
- **border colour table**: session ごとに 256 KiB を 0 で埋めて置き、SLC bypass にする（840811785、c47af3678）。
- **LOAD**:
  - 3D の背景の object から、shared register の texture state で sample する道は動かない。sample は 0 を返す。probe 1〜5 の形のどれでも同じだった（板 92〜95）。
  - そこで LOAD は、compiler の texture の shader による全画面の draw で行う（q1514、40bca42a6）。
  - 板 96 で vkmatrix 15/15 になった。16 draw を越える pass の job の分割も、これで通った。

### 4. compute（要点）

- **workgroup の PDS program**: Mesa の bytes をそのまま使う（data 8 語・code 10 語）。中身は次のとおり。
  - task word
  - workgroup id の DOUTW を cf0〜cf2 に
  - local id の DOUTW を unified store に
  - base workgroup の add32 ×3
  - DOUTU の cc
  - 以前の PDS には local id の DOUTW と cc が無く、fault するか一部だけを書いた。Mesa の PDS で 128 wg が 4096/4096 になった（板 87、P2 の q1496 566676bef、P1 の q1498 38776e4af）。
- **CDM の stream**: K0、kernel、fence kernel、terminate の順に並べる。
  - K0 は shared register を確保する。
    - usc_common 64 byte、target ALL、sd NONE、common_shared、max_instances 8、1 instance。
    - program は halt だけ。
  - kernel は sd_type USC。
  - fence kernel は sd PDS、common_shared、max_instances 8。
  - K0 が無いと、fence kernel が lockup する（板 91）。
  - fence が無いと、kick の完了が store より先になる（P2 の q1503 57d3029e1）。
  - commit は 5a22f829e と 748d362be、確認は板 93。
  - 1 submission の中の dispatch の順序は、各 dispatch の後の fence kernel で守られる（q1502）。
- **temps の field**: ceil(temps/4) にする。
  - 旧の手書きの kernel は 1 register 多く要った（P2 の q1492・q1499、板 87・88）。
- **max_instances**: 0 は 32 の意味になる（q1492）。

### 5. USC（shader）の制約（板 95 で確定、P2 の q1505）

| 項目 | 板 95 の結果 | 扱い |
| --- | --- | --- |
| store の data を address の register の対の直後に置く | 離すと 0/32（対の直後の register の値が store される） | **唯一の hardware の制約**。compiler は TEMP_STORE_DATA（r8）を TEMP_ADDRESS（r6:r7）の直後に置く（Mesa と同じ） |
| in-place の 64 bit の add | 32/32 | 制約ではない |
| madd の source を次の群で書き換える | 32/32 | 制約ではない |
| madd の source に特殊 register の定数を使う | 32/32 | 制約ではない |
| madd の結果を次の群で読む | 32/32 | 制約ではない |
| nop（`02 80 6e 00`）と wdf（`02 80 6a`）の encoding | Mesa 26.2.5 と同じ | ― |

- compiler は群の間に間隔を入れない（Mesa と同じ）。
- 以前「hazard」に見えた現象（半分の lane の値が壊れる）の原因は、PDS の不備（local id の DOUTW が無い）だった。
- Mesa の pco の命令列は `plan/ws269/tests/p008/mesa-pco-fill-bxe-4-32.txt` にある（q1481）。

### 6. memory と cache

- core は CPU の cache を snoop しない。
- CPU は非 cache の view でだけ触る。
- CPU が書くか読む buffer は、GPU の SLC も迂回する（border table も含む）。
- clean・invalidate は要らない。memory の型は HOST_COHERENT にする（P2 の q1508）。
- vkcoherency（buffer 1000 回・image 200 回）で stale 0・wrong 0（板 95）。

### 7. userland の Vulkan

- **queue family**: 1 つで、GRAPHICS・COMPUTE・TRANSFER を持つ（863b44cf1、板 81）。
- **API の版**: libvulkan は API 1.0 だけを公開する。
- **PPM の header**: libc の fscanf は 1 行しか読まない（BUG-344）。gpu-selftest は PPM の header を自前で読む（b70559027、板 83）。
- **試験の結果**: gpu-selftest 13/13、vkmatrix 15/15、vkcoherency PASS（板 89〜96）。

### 8. 板の運用と道具

- **VF2 の板の起動**:
  - SSH で /boot/boot0/vmunix を置き換え、読み戻して確かめる。
  - SwitchBot の plug（`plan/tools/switchbot.py`）で電源を入れ直す。
  - dmesg の ring は 32 KiB なので、起動の間に繰り返し読む。
  - 道具は `VF2_HOST=user@host VF2_PASSWORD=… plan/tools/vf2/run.sh VMUNIX OUTDIR`（Master の Tools）。
    - host と password は環境変数 VF2_HOST・VF2_PASSWORD で渡す。
    - Master の Tools への登録は Q1。
- **診断の switch**:
  - `POWERVR_DIAGNOSTICS`
  - `POWERVR_RENDER_CHECK_FULL_TRACE`
  - `POWERVR_FIRMWARE_BOOT_TRACE`
  - `POWERVR_RENDER_LOAD_PROBE`（a4f8663f2 など）
- **Linux（Debian、DDK 1.19、IMG の Vulkan）の取得**:
  - `plan/ws269/tests/p010-linux/` の capture*.py（gdb で bridge の kick の command を取る）と walk.py がある。
  - kick の command は取れた（TA 64 byte、CDM 48 byte）。
  - StarFive の kernel では /dev/mem で RAM を読めず、kcore も無いので、stream の本体は読めない（q1495）。

### 9. 除外した仮説

| 仮説 | 結果 | 板・Queue |
| --- | --- | --- |
| page catalogue の set・register range | set や range を変えても同じ lockup | 47・55・57 |
| context switch の flags | 同じ | 59・66 |
| static switch state を 0 にする | 同じ | 49 |
| border colour table が無い | 入れても同じ（入れたままにした） | 61 |
| resume size を 0 にする、single-core の flag | 同じ | 62・63・76 |
| PM_META_PROTECT が無い | 入れても同じ（入れたままにした） | q1471 |
| region headers の heap | 同じ | 67 |
| 594 MHz | 同じ | 73 |
| 起動前の EVENT_CLEAR | 起動前の EVENT_STATUS は 0 | 70 |
| CLK_CTRL・register_check の副作用 | 退行の原因は fence の除去だった | 92、q1503 |
| HWRT・起動列・geometry の register | Mesa・DDK・IMG の capture と一致 | q1480・q1475・q1482 |
| LOAD の address・layout、BGRA の sample、描いた直後の image の sample | どれも正しい | q1510・q1511 |

- **Linux との比較**:
  - TA の kick までの firmware の trace の並びは、Linux と同じだった。違いは firmware の外の stream にあった。
  - IMG の TA の command の値（ppp_ctrl 0x202、te_psg 0x20000、flags 3）も、我々と同じだった（q1465・q1482）。
