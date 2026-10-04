<!-- awesome-plan project=zedbsd record=ws037 -->

# WS037: nvrtx — NVIDIA GeForce RTX 2000 以降（Turing〜Blackwell）の GPU driver

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: なし
Objectives: O2
Parent: [Master](../master.md)
Queue: q692（p001）
Resume point: p001 から（2026-10-04、番号の予約から作り直し）。他の WS と独立。**実機は RTX 2070**（ユーザーが host を後で伝える）。host がわかったら [host.md](host.md) を埋め、p002 を始める。p001 は host 無しで始められる。
Target: **ベータ4 以降**（2026-10-05 user「WS037, WS044,WS048,WS141, WS112, WS118, WS124, WS125, WS126, WS119, WS096, WS097, WS039, WS038, WS144, WS143, WS146,WS147, WS152,  WS119, WS080, は、ベータ4以降としてください。…WS027, WS015, WS047, WS028, WS017,  WS077, はキャンセルします。」）
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
| [p001](phase001/phase.md) | 文書: 初期化の順・GSP の RPC・channel の command の順（作業の文書は temp）、NAK の世代の差、正本と license の監査、interface の対応表、段の印の設計、試験の道 | in-progress（q692、文書あり、review の反映が残り） | なし（host 無しで可） | 6h |
| [p002](phase002/phase.md) | 試験機（RTX 2070）の調査（読むだけ）と試験の道（A: USB から素で起動、B: VFIO）、今の zedBSD が RTX 2070 の GOP に出ること | planned（**host の情報待ち**） | [host.md](host.md) | 3〜4h |
| [p003](phase003/phase.md) | 定数の一括の改名、driver の骨格（PCI・BAR・`CONFIG_DRIVER_PCI_NVRTX`）、段の印 N0（chip の ID を読むだけ） | planned | p001・p002 | 4h |
| p004 | GSP の起動: VBIOS の FWSEC（FRTS）、booter、GSP-RM の firmware（既定 off の firmware の package、`/lib/firmware/nvidia/tu102/`）の load、RISC-V の GSP の起動、RPC の初期化と停止（[design](nvrtx-design.md) の段 P0〜P3・P7） | planning | p003 | 6h〜 |
| p005 | VRAM の allocator・MMU・BAR1/BAR2・GR の golden context と TU10x の scrubber・channel・GPFIFO・semaphore の fence・回復（段 P4〜P6） | planning | p004 | 6h〜 |
| p006 | display: GOP の引き継ぎ（readout）、GSP の RM の display で mode set・scanout・page flip（i915 の resident display を手本、段 P8）。text console の付け直し | planning | p005（VRAM の allocator と BAR1 の map） | 6h〜 |
| p007 | 3D（TU102_A 系の class）・compute の投入と `drv_gpu_ops` への統合（段 P9）、desktop の表示 | planning | p005・p006・kernel の Venus の executor と compiler（p008 の方針、[design](nvrtx-design.md) 8.0） | 6h〜 |
| p008 | kernel の Venus の executor と shader の compiler（SPIR-V → SM75、NAK の構成を参考）の方針、別 WS にするか（design の判断の項目 8・16） | planning | p001 | 2h |
| p009 | 規約の全文の確認、license と GPL の code との類似の監査、BLOB（GSP の firmware）の確認 | planning | 全て | 4h |

各段の印（GOP の framebuffer）と段の名前の正は [design](nvrtx-design.md) の 3 節: N0 発見と ID → N1 GOP の readout → P0 GSP の起動の準備 → P1 devinit 待ち・FWSEC-FRTS → P2 booter と GSP の起動 → P3 RM の object → P4 VRAM・MMU → P5 channel・fence → P6 回復 → P7 停止 → P8 display の引き継ぎ → P9 最初の 3D の job。i915 の N0・N1・P2 と同じ考え。
