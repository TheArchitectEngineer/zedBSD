<!-- awesome-plan project=zedbsd record=ws037 -->

# WS037: nvrtx — NVIDIA GeForce RTX 2000 以降（Turing〜Blackwell）の GPU driver

<!-- awesome-plan-current:start -->
Status: planned
Primary Milestone: MG006
Related Milestones: なし
Objectives: O2
Parent: [Master](../master.md)
Queue: q692（p001）
Resume point: p001 から（2026-10-04、番号の予約から作り直し）。他の WS と独立。
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
2. zedBSD の code を書く前に、作業の文書の**定数を全て一括で独自の名前に変える**（p002 の最初）。
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

| Phase | 目的 | Status | 依存 | 目安 |
| --- | --- | --- | --- | --- |
| [p001](phase001/phase.md) | 文書: 初期化の順・GSP の RPC・channel の command の順・display・MMU、NAK の世代の差、正本と license の監査、最初の対象の世代、interface の対応表、段の印の設計 | planned | なし | 6h |
| p002 | 定数の一括の改名の後に、driver の骨格（PCI の attach、BAR の map、GOP の framebuffer の段の印）と GSP の firmware の置き場所（`userland/firmware/`） | planning | p001 | 4h |
| p003 | GSP の boot と RPC | planning | p002 | 6h〜 |
| p004 | MMU・channel・GPFIFO・fence・reset | planning | p003 | 6h〜 |
| p005 | display（GOP の引き継ぎ、mode set、page flip） | planning | p003 | 6h〜 |
| p006 | 3D・compute の class の投入と `drv_gpu_interface` への統合、desktop の表示 | planning | p004・p005 | 6h〜 |
| p007 | shader の compiler の方針（SPIR-V → SM70 系、別 WS にするか） | planning | p001 | 2h |
| p008 | 規約の全文の確認、license と GPL の code との類似の監査、BLOB の確認 | planning | 全て | 4h |
