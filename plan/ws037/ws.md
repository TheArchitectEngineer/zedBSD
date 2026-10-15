<!-- awesome-plan project=zedbsd record=ws037 -->

# WS037: nvrtx — NVIDIA GeForce RTX 2000 以降（Turing〜Blackwell）の GPU driver

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-14 開始。2026-10-15: パススルーで R8b〜R8d・R9v・R9k が PASS（desktop の描画）、方法 A（素の起動）で desktop の session 5/5（T1-684）、その後 T1-691 で P4m の間欠の失敗が再発。描画の速さは [WS241](../ws241/ws.md)、RTX 20/30/40 は [WS242](../ws242/ws.md)、NVDEC は [WS243](../ws243/ws.md)、compiler のまとめは [WS246](../ws246/ws.md)、出典の整理は [WS247](../ws247/ws.md) に独立。2026-10-15 記録の整合の点検で更新）
Primary Milestone: MG006
Related Milestones: なし
Objectives: O2
Parent: [Master](../master.md)
Queue: q692（p001）。2026-10-14 以降は Q1 の割当で進め、queue.md に行が無い（次の session で補う）
Resume point（2026-10-15 Q1、記録の整合の点検）: (1) T1-691 の P4m の間欠の失敗（MMU の fault 00110802 at 0x1d0000、retry でも直らない）の解析の続き（[phase007](phase007/phase.md) の終わりの節）。(2) reboot の時の GSP の停止はメモだけ（ユーザー「WSにメモしておいて、あまり深追いしない」）。(3) `nvrtx.disp.own=1` を既定にするかはユーザーの判断待ち（Master の open-decisions）。(4) 各 Phase の clearance の判定（p006 は cleared の候補）は未。runtime の段の既定 on と GSP の firmware の image への収録が済んだかは、この点検では確かめていない。
Target: ベータ2（2026-10-16 夕）。**既定は on**（2026-10-15 ユーザー、config/ci/config-amd64.mk と config/drivers/pci.drivers は y。ユーザーの config.mk は n でユーザーの意図どおり）。以前の「既定 off で入れる」（2026-10-14）と「ベータ4 以降」（2026-10-05）は置き換え。
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
| [p008](phase008/phase.md) | shader の compiler（SPIR-V → SM75、共有の front と IR、NAK（MIT）の事実）。WS037 の Phase に決めた（判断の項目 8・16） | in-progress（2026-10-15 P3: 段 A・B・C・E を実装。2026-10-15 P4: desktop の全 shader（41 module）が compile でき、IR の参照の interpreter と一致（texelFetch・texel offset・2D の一般の message を足した、[p008](phase008/phase.md) の 11 節）。build と host まで、GPU の確かめは p007 の投入の後） | p001 | 2h＋段 A〜E |
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

## RTX 30・40 の対応（2026-10-15 ユーザー、Q1 の記録）
ユーザー（原文）:「nvrtxドライバは、RTX 20/30/40/50までのSMで対応できるようにしたいです。」「20/30/40は、30/40の動作確認なしで、とりあえず対応しておいてください。firmwareも追加しておきます。あと、20/30/40の全モデルで認識できるようにしてください。50は別WSを作り、あとでnvrtxに追加します。」「この作業はいったん記録してから開始してください。50はまだ開始しないでください。」
- driver の側（P2、ws037-p009「RTX 30・40 の対応（driver）」）: 20/30/40 の全 chip（TU102・104・106・116・117、GA102・103・104・106・107、AD102・103・104・106・107）の PCI の device id と chip id の認識、chip の世代ごとの表（firmware の名前・class・QMD の版・起動の差）、firmware の package に GA10x・AD10x の GSP-RM・booter・bootloader を足す、起動・RM・class の差の切り替え。
- compiler の側（P4、phase008 の新しい節「SM86・SM89」）: SM86・SM89 の encode・latency・規則の差を切り替え、SM75 の出力は byte 単位で不変。
- 30/40 は実機の確かめなし（host 試験と build まで）。RTX 2070 の今の動き（方法 A で desktop）を壊さない。
- RTX 50（Blackwell、SM120）は [WS240](../ws240/ws.md)（planning、まだ開始しない）。

- 2026-10-15: RTX 30・40 の対応は [WS242](../ws242/ws.md)、描画の速さは [WS241](../ws241/ws.md) に独立（ユーザー）。
