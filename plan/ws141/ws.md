<!-- awesome-plan project=zedbsd record=ws141 -->

# WS141: Raspberry Pi 4 のグラフィックス driver（VideoCore VI: HVS・pixelvalve・HDMI の display と V3D 4.2）

<!-- awesome-plan-current:start -->
Status: planned
Primary Milestone: MG006
Related Milestones: MG008
Parent: [Master](../master.md)
Queue: q691（p001、17 時以降）
Resume point: p001 から（2026-10-04 作成）。他の WS と独立に進める。
<!-- awesome-plan-current:end -->

## 単一目標

Raspberry Pi 4（BCM2711、VideoCore VI）で、zedBSD の自前の GPU driver により display（firmware の framebuffer からの引き継ぎ、HDMI の mode set、scanout）と 3D（V3D 4.2 の job の実行）を成立させ、zedBSD の GPU の interface（`struct drv_gpu_interface`、i915 と同じ口）に載せて desktop を表示する。

## ユーザーの指示（2026-10-04）

「完全に独立した作業として、Raspberry Pi 4のグラフィックドライバを作成します。i915のときと同じで、Linuxドライバの初期化順やコマンド投入順などをまずドキュメントにして、正本も参照しながら、i915と同じように、我々のインタフェースに適合させていきます。フレームバッファにどこまで進んだかのメッセージを表示することで、少しずつデバッグしながら先に進めます。DRM部分の書き換えは、i915を参考にします。これは独立したWSで、17時以降に着手します。」

## 進め方（i915 の WS029・WS084 と同じ型）

1. **文書が先**: Linux の vc4（display: HVS・pixelvalve・HDMI・firmware KMS）と v3d（V3D 4.2）の driver の**初期化の順と command の投入の順**を、まず文書にする（p001）。正本（Linux の source の tag と path、Mesa の broadcom、Broadcom の公開の文書、device tree の binding）を版と hash 付きで記録し、参照しながら進める。
2. **我々の interface に合わせる**: DRM の部分（mode set・plane・buffer object・job の submit・fence）は、i915 の zedBSD の書き換え（`src/drivers/gpu/i915/`、`drv_gpu_interface`、resident display）を手本にする。
3. **段ごとの印を framebuffer に**: firmware が用意した framebuffer（`src/hal/arm64/bsp-rpi4/framebuffer.c`）に、driver が「どの段まで進んだか」の印（例 `v3d: P2 clocks ok`、`hvs: N1 readout done`）を表示し、実機で少しずつ debug する（i915 の N0・N1・P2 の段の印と同じ考え）。

## ライセンスの境界（重要）

- **Linux の vc4・v3d の driver は GPL-2.0** で、i915（参照した file は MIT、[i915-license-audit](../ws029/i915-license-audit.md)）と違う。Master の方針（[設計方針](../master-design-policy.md) §2.1「clean and independent」）により、**GPL の code は写さない**。
- 文書には、初期化の順・register の意味・command の流れを**事実として自分の言葉で**書く。code・comment・構造体の定義を写さない。
- register の定義・command の packet の形は、MIT の Mesa（`src/broadcom/`、例 `cle/v3d_packet.xml`）、Broadcom の公開の文書、device tree の binding（GPL/MIT の dual のものは確かめる）から取り、file ごとに license と hash を監査の表に記録する（p001）。
- 境界の判断に迷う所はユーザーに確かめる（p001 の判断の項目）。

## 範囲

- display: firmware の framebuffer の引き継ぎ、HVS（plane の合成）、pixelvalve（timing）、HDMI（mode・EDID・audio は範囲の外）、vblank、scanout の page flip。
- 3D: V3D 4.2 の power・clock（firmware の mailbox）、MMU、bin/render の control list、CSD（compute）の job、reset、fence。
- zedBSD の GPU の interface への統合と、desktop（Keiland の compositor）の表示。Vulkan の実行器（compiler）は別の Phase または別の WS（i915 の WS031 にあたる）で決める。
- 範囲の外: Raspberry Pi 5、DSI の LCD、HDMI の音、video の decode。

## 前提・関係

- [WS048](../ws048/ws.md)（rpi4 の FDT・PCIe・mailbox）、[WS044](../ws044/ws.md)（rpi4 を開発に使える形に、console）。firmware の mailbox は `src/drivers/platform/rpi4/rpi4-firmware.c`。
- HAL の API の変更が要るときは、差分を plan に置いて承認を得る（AGENTS.md）。
- 試験: 実機（Raspberry Pi 4、ユーザー）。QEMU の raspi4b は HVS・V3D を emulate しないので、display の印と host の試験（command list の生成など）で補う。

## Phase 一覧

| Phase | 目的 | Status | 依存 | 目安 |
| --- | --- | --- | --- | --- |
| [p001](phase001/phase.md) | 文書: Linux の vc4・v3d の初期化の順と command の投入の順、正本の一覧と license の監査、BCM2711 の display と V3D の構成、我々の interface への対応表、段の印の設計 | planned | なし | 4〜6h |
| p002 | 段の印の仕組み（framebuffer に進み具合を書く debug の口）と driver の骨格（FDT の attach、MMIO の map、clock・power の mailbox、IRQ） | planning | p001 | 4h |
| p003 | display: firmware の framebuffer の readout と引き継ぎ、HVS の plane、pixelvalve・HDMI の mode set、vblank と page flip（i915 の resident display を手本に） | planning | p002 | 6h〜 |
| p004 | V3D: power・MMU・buffer object、bin/render の control list と CSD の job、reset、fence | planning | p002 | 6h〜 |
| p005 | `drv_gpu_interface` への統合と desktop の表示（Keiland の compositor） | planning | p003・p004 | 4h〜 |
| p006 | 実行器（Vulkan・compiler）の方針の決定（別 WS にするか） | planning | p004 | 2h |
| p007 | 規約の全文の確認と最終の確認 | planning | 全て | 3h |
