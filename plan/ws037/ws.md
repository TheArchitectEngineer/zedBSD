<!-- awesome-plan project=zedbsd record=ws037 -->

# WS037: nvrtx — NVIDIA GeForce RTX 2000 以降（Turing〜Blackwell）の GPU driver

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-14 ユーザー「事情が変わりました。16日夕方までに利用できるトークン量が、Max 20xプラン1週間分、増えました。つまり、RTX対応ができる可能性があります。やりましょう。」でブロックとベータ4 の指定を解き再開、目標はベータ2（10/16 夕）。既定 off で入れ、間に合わない部分は drop）。以前: planning（要検討・ブロック、2026-10-05）
Primary Milestone: MG006
Related Milestones: なし
Objectives: O2
Parent: [Master](../master.md)
Queue: q692（p001）、2026-10-14 から P1（Q1 の割当「P1: RTX」）
Resume point（2026-10-14 P1）: p001 の判断の項目を締めた（ユーザーの判断の要る点は Q1 の回答 A〜D で決着）。firmware の package `nvidia-gsp-firmware`（既定 off）と p003（骨格・N0・N1、既定 n）を commit、build と host 試験まで。p004 の 4a〜4e（P0〜P3・P7）も実装済み（2026-10-14、build と host 試験まで）。p005 の copy の channel の道（P4〜P6）も実装済み。方式 B（awe@10.0.10.3 の Debian の VFIO、T2）で R0・R1 PASS、R2 は P7（FRTS だけの region に booter_unload）、R3 は P2b（init done が来ない、待ちの timeout も効かない）で止まった。2026-10-14 P1 が原因の解析と直し（P7 は GSP を止めた後だけ teardown、待ちは時計で測る、P2b の切り分けの印と dump の番地）を commit `1c1732db7`。再試験で R2 PASS、R3 は GSP の libos の init で停止（dump の解読で RMARGS の region が 0x48 だった所で止まった）。nouveau と P0〜P2 を照合し、違いの RMARGS の大きさを 0x1000 に合わせた（`714ab8477`）。その後 R3 は GSP-RM が動き、CPU sequencer の CORE_RESUME（opcode 8）が未実装で止まった。sequencer の全 opcode と RPC の番号・照合を nouveau と同じにした（`870aecf93`）。R3・R4 PASS（GSP-RM が実機で起動、P3 の objects・P7 の片付けまで）。R5 は P4 の SET_PAGE_DIRECTORY が NOT_SUPPORTED: RM の確保の RPC の header を 44 byte と誤っていた（正は 32）ため vaspace が外部所有にならなかった。直しと handle を nouveau の値に（`b8803ff5e`）。次は T2 の R5〜R7（[p005](phase005/phase.md) の「R5 の結果と直し」）。 2026-10-15 P1: R6・R7・R7g・P8a は実機 PASS（T1）。p006 の P8b（GOP の head の引き継ぎ・自前の surface・flip、`nvrtx.disp=1`）を実装（`b893faeb6`・`3d0f51784`）、R8b（rombar=1）は T 待ち（[p006](phase006/phase.md)）。 同日 P1（新しい世代）: R8b は FAIL（core の notifier なし・信号なし・unload 42）→ nouveau の初回の commit の順（disable → DFP_ASSIGN_SOR → enable）に直した（`476ca3ba5`、再試験 T1-645 待ち）。p005 の P4m（VRAM の allocator と BAR1、`7a60b2445`）、p006 の P8c（`nvrtx.resident=1` で GSP を残し GPU の device と display の ops を登録、`2c4d1a39f`、R8c は R8b の後）を実装、build と host 試験まで。 同日 P1: P8c の続き（DISPLAY_EVENTS（topology の sequence、固定の 1）・DISPLAY_CONTROL（refresh は head の frame counter、power は EOPNOTSUPP）・cursor の plane（PIO channel 0xc57a）・SHARE（blob の backing の参照）・scanout の表（SHARED と COPY、stride 64・offset 256））を `2791fc6e3`・`e6699aa70` で build と host 試験まで（probe `display-scanout`、run R8d は R8c の後、[p006](phase006/phase.md) の「P8c の続き」）。
Target: ベータ2（2026-10-16 夕）に既定 off で入れる（2026-10-14 ユーザー、上の Status）。以前: **ベータ4 以降**（2026-10-05 user「WS037, WS044,WS048,WS141, WS112, WS118, WS124, WS125, WS126, WS119, WS096, WS097, WS039, WS038, WS144, WS143, WS146,WS147, WS152,  WS119, WS080, は、ベータ4以降としてください。…WS027, WS015, WS047, WS028, WS017,  WS077, はキャンセルします。」）
<!-- awesome-plan-current:end -->

## 単一目標

zedBSD の自前の GPU driver **nvrtx**（`src/drivers/gpu/nvrtx/`）で、NVIDIA の RTX 2000 以降（Turing SM75・Ampere SM86・Ada SM89・Blackwell SM120、GTX 16xx も Turing として含む）の display と 3D・compute を成立させ、zedBSD の GPU の interface（`struct drv_gpu_interface`、i915 と同じ口）に載せる。最初の対象の世代は p001 で決める（予約の時の対象は RTX 2070）。

## ユーザーの指示

- 2026-09-23（予約）: 「RTX 2070をターゲットにして、NVIDIA GPUのドライバを実装する。」別契約のエージェントが担当する予定の番号の予約だった。
- 2026-10-04: 「rtx 2000以降を対象に、nvrtxというドライバを開発します。やりかたはVC4と同じです。WSを作ってください。」→ この WS を nvrtx として作り直す（Q1）。
- 範囲の外: GTX 10xx 以前（Pascal 以前）。理由: shader の ISA が別の系統（SM50 系の 64 bit 命令と制御語）で、Pascal は reclocking の署名付き firmware が公開されず低クロックのままになる（2026-10-04 Q1 の説明、ユーザー了承）。

## 進め方（[WS141](../ws141/ws.md) の vc4 と同じ）

1. **文書が先**: nouveau（GSP の道）の初期化の順、GSP の RPC の流れ、channel（GPFIFO・push buffer）の command の投入の順、display（NVDisplay）、MMU、Mesa の NVK・NAK の世代の差（Turing〜Blackwell）を文書にする（p001）。
2. **我々の interface に合わせる**: DRM の部分は i915 の zedBSD の書き換え（`drv_gpu_interface`・resident display・buffer object・fence）を手本にする。
3. **段の印**: UEFI の GOP の framebuffer に「どの段まで進んだか」の印を出し、少しずつ debug する（i915 の N0・N1・P2 と同じ）。
4. **試験**: 開発の host（centris）に NVIDIA の GPU を挿す予定（2026-10-04 ユーザー、WS139 U5）。i915 と同じく VFIO の passthrough で QEMU の guest に渡して試す（[WS029](../ws029/ws.md) p006 の道）。host の操作は試験の担当と Q1。

## ライセンスの扱い（WS141 と同じ方式、2026-10-04 ユーザー「やりかたはVC4と同じです」）

1. GPL の source（nouveau の GPL の部分があれば、open-gpu-kernel-modules の GPL の側）から作る作業の文書は `plan/ws037/temp/` に置き、**commit しない**。手順の書き写しが基本、表現が難しい所は code の書き写しも可。
2. zedBSD の code を書く前に、作業の文書の**定数を全て一括で独自の名前に変える**（p003 の最初、code を書く前）。
3. register の定義・method（class の命令）・packet の形は、**MIT の source** から取り、file ごとに path・SHA-256・license を監査の表にする: nouveau（`drivers/gpu/drm/nouveau/`、大部分 MIT。file ごとに確かめる）、Mesa の NVK・NAK（`src/nouveau/`、MIT）、NVIDIA の open-gpu-kernel-modules・open-gpu-doc の MIT の側。
4. **BLOB**: GSP の firmware（`gsp-*.bin`、NVIDIA の再配布の license）と、source の中の firmware に相当する BLOB は `userland/firmware/`（例 `userland/firmware/nvidia-gsp/`）に置き、file から load する。license を個別に確かめる。
5. WS の最後に、license と GPL の code との字面・設計の類似を監査する。zedBSD の code は Zlib。

## 範囲

- GSP の boot（firmware の load、RISC-V の GSP の起動）と RPC、MMU、channel と GPFIFO、fence（semaphore）、reset。
- display: GOP の framebuffer の引き継ぎ、NVDisplay の mode set・scanout・page flip（GSP 経由の部分を含む）。
- 3D・compute の class（世代ごとの class の番号と共通の method）、`drv_gpu_interface` への統合、desktop の表示。
- shader の compiler（SPIR-V → SM70 系の ISA）: NAK の構成（共通の IR と最適化、世代ごとの encoder）を参考に、別の Phase か別の WS（i915 の WS031 にあたる）で決める。
- 範囲の外: Pascal 以前、video の encode・decode、複数 GPU、SLI。

## Phase 一覧

最初の対象は **RTX 2070（TU106、Turing）**（2026-10-04 ユーザー「RTX 2070の実機があり」）。Ampere 以降は後で足す（class の番号と GSP の firmware の版の差分）。

| Phase | 目的 | Status | 依存 | 目安 |
| --- | --- | --- | --- | --- |
| [p001](phase001/phase.md) | 文書: 初期化の順・GSP の RPC・channel の command の順（作業の文書は temp）、NAK の世代の差、正本と license の監査、interface の対応表、段の印の設計、試験の道 | in-progress（文書と review の反映済み、判断の項目を 2026-10-14 に締めた。clearance は Q1） | なし（host 無しで可） | 6h |
| [p002](phase002/phase.md) | 試験機（RTX 2070）の調査（読むだけ）と試験の道（A: USB から素で起動、B: VFIO）、今の zedBSD が RTX 2070 の GOP に出ること | planned（host 10.0.30.5 に Linux を入れて方式 B、host の準備は Q1） | [host.md](host.md) | 3〜4h |
| [p003](phase003/phase.md) | 定数の一括の改名、driver の骨格（PCI・BAR・`CONFIG_DRIVER_PCI_NVRTX`）、段の印 N0（chip の ID を読むだけ） | in-progress（実装・build・host 試験済み、実機は T1 待ち） | p001・p002 | 4h |
| [p004](phase004/phase.md) | GSP の起動: VBIOS の FWSEC（FRTS）、booter、GSP-RM の firmware（既定 off の firmware の package、`/lib/firmware/nvidia/tu102/`）の load、RISC-V の GSP の起動、RPC の初期化と停止（[design](nvrtx-design.md) の段 P0〜P3・P7）。4a〜4e に分け、どこで切っても既定が変わらない | in-progress（4a〜4e 実装・build・host 試験済み、実機（方式 B）は T1 待ち） | p003 | 6h〜 |
| [p005](phase005/phase.md) | VRAM の allocator・MMU・BAR1/BAR2・GR の golden context と TU10x の scrubber・channel・GPFIFO・semaphore の fence・回復（段 P4〜P6） | in-progress（R5〜R7・R7g は実機 PASS。2026-10-15: P4m（VRAM の allocator と BAR1、`7a60b2445`）を実装、build と host 試験まで、実機は R8b・R8c の P4m の行で。BAR2 は未） | p004 | 6h〜 |
| [p006](phase006/phase.md) | display: GOP の引き継ぎ（readout）、GSP の RM の display で mode set・scanout・page flip（i915 の resident display を手本、段 P8）。text console の付け直し | in-progress（2026-10-15: P8a 実機 PASS。P8b は R8b FAIL → nouveau の初回の commit に直した `476ca3ba5`（T1-645 待ち）。P8c（GPU の device・display の ops、`nvrtx.resident=1`）実装 `2c4d1a39f`、R8c は R8b の後） | p005（VRAM の allocator と BAR1 の map） | 6h〜 |
| [p007](phase007/phase.md) | 3D（TU102_A 系の class）・compute の投入と `drv_gpu_ops` への統合（段 P9）、desktop の表示 | in-progress（2026-10-15 P3: 段 P9g（`nvrtx.draw=1`、P9c の channel に 3D の object、単色の quad を system memory と VRAM の target に、texture の quad）を実装・build・host 試験済み、R7d は T 待ち。`drv_gpu_ops` への統合は未） | p005・p006・kernel の Venus の executor と compiler（p008 の方針、[design](nvrtx-design.md) 8.0） | 6h〜 |
| [p008](phase008/phase.md) | shader の compiler（SPIR-V → SM75、共有の front と IR、NAK（MIT）の事実）。WS037 の Phase に決めた（判断の項目 8・16） | in-progress（2026-10-15 P3: 段 A（compute の最小の lowering と encoder）を実装、build と host 試験（39 件）まで。GPU の確かめは p007 の投入の口の後） | p001 | 2h＋段 A〜E |
| p009 | 規約の全文の確認、license と GPL の code との類似の監査、BLOB（GSP の firmware）の確認 | planning | 全て | 4h |

各段の印（GOP の framebuffer）と段の名前の正は [design](nvrtx-design.md) の 3 節: N0 発見と ID → N1 GOP の readout → P0 GSP の起動の準備 → P1 devinit 待ち・FWSEC-FRTS → P2 booter と GSP の起動 → P3 RM の object → P4 VRAM・MMU → P5 channel・fence → P6 回復 → P7 停止 → P8 display の引き継ぎ → P9 最初の 3D の job。i915 の N0・N1・P2 と同じ考え。

## 要検討・ブロック（2026-10-05、2026-10-14 に解除）

2026-10-05 にユーザーの指示で要検討の状態にしてブロックした。2026-10-14 のユーザーの指示（上の Status）で解除。

## 2026-10-14 夜 ユーザーの判断: ベータ2 に入れる範囲

「* R6 の「channel を残したまま止める」場合の片付け * 3D の初期状態（golden context） * display * shader の compiler ここまでベータ2に入れます。作業を続けてください。」
→ 実機（VFIO）で R0〜R6 の GPU の仕事（copy engine の fill と fence）まで通った後の判断。順: R6 の片付け → 5f（golden context と scrubber）→ p006（display）→ p007・p008（3D と shader の compiler、SPIR-V → SM75、Mesa の NAK（MIT）を参考）。既定 off（`CONFIG_DRIVER_PCI_NVRTX` 既定 n）のまま入れ、通った段まで。担当: P1 が WS238 の後に R6 の片付け〜display、shader の compiler は先に空いた P（P2 の BUG-312 の後か P3 の WS237 の後）。
- 2026-10-14 夜 ユーザー「ESP/FAT32分離が終わったら、RTX対応を2エージェントで実施してOKです。displayとcompilerは平行にできそうです。」→ P1: R6 の片付け・golden context・display、P3: shader の compiler（WS237 を止めて移る）。P2 の BUG-312 の後の予告は取り消し。

## 引き継ぎ（2026-10-15 夜、Q1、サーバの再起動の前）

- main に入った物: P4m（VRAM の heap・BAR1）、P8b の直し（nouveau と同じ最初の commit、DFP_ASSIGN_SOR）、P8c（resident・GPU device・display）、display の event・control・cursor・share（R8d）、p007 の draw（P9g）、P1 の SPH の初めの直し（7ce6e4019）。
- 実機: R7c PASS。R7d FAIL（`Graphics Exception: Shader Program Header`、ESR 0x405840=0xa0000・0x405848=0x80000）。R8b の再試験（T1-645）・R8c（T1-647）・R8d（T1-652）は未実施（ユーザーが monitor を見る）。
- 2026-10-15 ユーザー: nvrtx を既定で on。CONFIG は y 済み。runtime の段の既定 on・opt-out・方法 A（USB から素で起動）・GSP の firmware の image への収録は P1 の作業中の patch（plan/agents/p1/wrapup-20261015.patch、base 0e33f93cf）で未完。
- R7d の SPH の直しは P3 の patch（plan/agents/p3/wrapup-20261015.patch、base e1e8cad65、P1 の 7ce6e4019 と同じ file を変えるので手で合わせる）。
