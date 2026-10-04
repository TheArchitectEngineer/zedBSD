<!-- awesome-plan project=zedbsd record=ws141 -->

# WS141: Raspberry Pi 4 のグラフィックス driver（VideoCore VI: HVS・pixelvalve・HDMI の display と V3D 4.2）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG008
Parent: [Master](../master.md)
Queue: q691（p001、17 時以降）
Resume point: p001 の文書（[rpi4-gpu-design.md](rpi4-gpu-design.md)・[rpi4-gpu-license-audit.md](rpi4-gpu-license-audit.md)）がそろい review を反映済み（2026-10-04、q691-i01）。p002 は判断の項目 3（disabled の node）・11（device の分割）の答えの後。判断の項目 1 は決定（事実として使う）。
<!-- awesome-plan-current:end -->

## 単一目標

Raspberry Pi 4（BCM2711、VideoCore VI）で、zedBSD の自前の GPU driver により display（firmware の framebuffer からの引き継ぎ、HDMI の mode set、scanout）と 3D（V3D 4.2 の job の実行）を成立させ、zedBSD の GPU の interface（`struct drv_gpu_interface`、i915 と同じ口）に載せて desktop を表示する。

## ユーザーの指示（2026-10-04）

「完全に独立した作業として、Raspberry Pi 4のグラフィックドライバを作成します。i915のときと同じで、Linuxドライバの初期化順やコマンド投入順などをまずドキュメントにして、正本も参照しながら、i915と同じように、我々のインタフェースに適合させていきます。フレームバッファにどこまで進んだかのメッセージを表示することで、少しずつデバッグしながら先に進めます。DRM部分の書き換えは、i915を参考にします。これは独立したWSで、17時以降に着手します。」

## 進め方（i915 の WS029・WS084 と同じ型）

1. **文書が先**: Linux の vc4（display: HVS・pixelvalve・HDMI・firmware KMS）と v3d（V3D 4.2）の driver の**初期化の順と command の投入の順**を、まず文書にする（p001）。正本（Linux の source の tag と path、Mesa の broadcom、Broadcom の公開の文書、device tree の binding）を版と hash 付きで記録し、参照しながら進める。
2. **我々の interface に合わせる**: DRM の部分（mode set・plane・buffer object・job の submit・fence）は、i915 の zedBSD の書き換え（`src/drivers/gpu/i915/`、`drv_gpu_interface`、resident display）を手本にする。
3. **段ごとの印を framebuffer に**: firmware が用意した framebuffer（`src/hal/arm64/bsp-rpi4/framebuffer.c`）に、driver が「どの段まで進んだか」の印（例 `v3d: P2 clocks ok`、`hvs: N1 readout done`）を表示し、実機で少しずつ debug する（i915 の N0・N1・P2 の段の印と同じ考え）。

## ライセンスの扱い（2026-10-04 ユーザーの決定、前の「ライセンスの境界」を置き換える）

2026-10-04 user「vc4/v3dドライバのソースから作る文書は作業ファイルにして、リポジトリにコミットせず、GPLコードを参考に書き写してもいいことにします。基本は手順を書き写しますが、表現が難しいときはコードを書き写してもいいです。我々のコードを生成する前に、定数はすべて、一括で、独自の名前に変更します。最後にライセンスに問題がないか、コードに類似がないかを監査します。これにより字面でも設計でもGPLコードを含めず、独自ライセンスとします。ファームウェアに相当しそうなBLOBがソースコード中にある場合は、userland/firmwareに移してファイルからロードすることにします。」

1. **作業の文書は repository に入れない**: vc4・v3d の GPL の source から作る文書（初期化の順・command の順の書き写し）は `plan/ws141/temp/`（`.gitignore` の `plan/ws*/temp/`）に置き、**commit しない**。手順の書き写しが基本、表現が難しい所は code の書き写しも可。
2. **定数の一括の改名**: zedBSD の code を書く前に、作業の文書の定数（register・bit・field の名前）を**全て一括で独自の名前に変える**（対応表も temp に置き、commit しない）。zedBSD の code は改名の後の文書から書く。
3. **最後の監査**: WS の最後に、license の問題が無いか、GPL の code と**字面でも設計でも類似が無いか**を監査する（類似の検出の道具と目視、[p007](../ws.md)）。結果を commit できる形（類似の検出の結果の要約、GPL の file の一覧と hash）で残す。
4. **BLOB**: source の中に firmware に相当しそうな BLOB（byte の配列など）があれば、`userland/firmware/` に移し、file から load する（RTL8822B・i915 の firmware と同じ形）。license は個別に確かめる。
5. **register の定義と packet の形の出典**（2026-10-04 ユーザー「これはそうしたいですね。」）: zedBSD の code の register の定義・command の packet の形は、**MIT の Mesa（`src/broadcom/`、例 `cle/v3d_packet.xml`）、Broadcom の公開の文書、device tree の binding** から取り、**file ごとに license を監査**する（path・SHA-256・license の表、[i915-license-audit](../ws029/i915-license-audit.md) の形）。GPL の vc4・v3d は手順の理解と作業の文書のためだけに使う。
6. zedBSD の code は独自の license（Zlib）。repository には GPL の code・作業の文書を入れない。

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
| [p001](phase001/phase.md) | 文書: Linux の vc4・v3d の初期化の順と command の投入の順、正本の一覧と license の監査、BCM2711 の display と V3D の構成、我々の interface への対応表、段の印の設計 | in-progress（q691、文書と review 済み、判定待ち） | なし | 4〜6h |
| p002 | **定数の一括の改名**（作業の文書、temp）の後に、段の印の仕組み（framebuffer に進み具合を書く debug の口）と driver の骨格（FDT の attach、MMIO の map、clock・power の mailbox、IRQ） | planning | p001 | 4h |
| p003 | display（[design](rpi4-gpu-design.md) の N0〜N2・P1〜P3・P5、P4 は後）: firmware の framebuffer の readout と引き継ぎ、HVS の plane、pixelvalve・HDMI の mode set、vblank と page flip（i915 の resident display を手本に） | planning | p002 | 6h〜 |
| p004 | V3D: power・MMU・buffer object、bin/render の control list と CSD の job、reset、fence | planning | p002 | 6h〜 |
| p005 | `drv_gpu_interface` への統合と desktop の表示（Keiland の compositor） | planning | p003・p004 | 4h〜 |
| p006 | 実行器（Vulkan・compiler）の方針の決定（別 WS にするか） | planning | p004 | 2h |
| p007 | 規約の全文の確認と最終の確認。**license と GPL の code との類似の監査**（字面・設計、道具と目視）、BLOB の移動の確認 | planning | 全て | 3〜4h |
