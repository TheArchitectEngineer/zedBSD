<!-- awesome-plan project=zedbsd record=ws141-p001 -->

# ws141-p001: 文書（Linux の vc4・v3d の初期化の順と command の投入の順、正本と license、我々の interface への対応）

Status: planned
Disposition: normal
Parent: [WS141](../ws.md)
Queue: q691（17 時以降）
実行者: kernel-and-driver-designer（code は書かない）

## 範囲

1. **正本の一覧と license の監査**: Linux（tag を固定、例 v6.19）の `drivers/gpu/drm/vc4/`・`drivers/gpu/drm/v3d/`、Mesa の `src/broadcom/`（MIT）、device tree の binding（`brcm,bcm2711-hvs`・`-pixelvalve`・`-hdmi`・`v3d`）、Broadcom の公開の文書、Raspberry Pi の firmware の mailbox の文書。file ごとに path・SHA-256・license を表に（[i915-license-audit](../../ws029/i915-license-audit.md) の形）。**GPL の file は「読んで事実を自分の言葉で書く」だけ**、MIT の file は定義の取り込みの候補。
2. **初期化の順**: Linux の vc4（firmware KMS と full KMS の 2 つの道の違い、HVS・pixelvalve・HDMI・clock・power domain の順）と v3d（clock・reset・MMU・IRQ の順）を、段（N0・N1・P1・P2 … の i915 と同じ名付け）に分けて書く。
3. **command の投入の順**: V3D の bin と render の control list（CL）の流れ、tile の割り当て、CSD、job の完了の IRQ と fence。HVS の display list の書き方と page flip。
4. **我々の interface への対応表**: `struct drv_gpu_interface`・resident display・buffer object・fence と、vc4・v3d の概念の対応（i915 の `src/drivers/gpu/i915/` の何を手本にするか）。
5. **段の印の設計**: framebuffer（`src/hal/arm64/bsp-rpi4/framebuffer.c`）に進み具合の印を書く口の設計（HVS を触った後は firmware の framebuffer が消えうるので、印の出し先をどう保つか）。
6. **判断の項目**: ユーザーに確かめる点（license の境界の迷う所、firmware KMS の道を先にするか full KMS か、実機の試験の手順）。

## ライセンスの扱い（2026-10-04 ユーザーの決定、[ws.md](../ws.md) の節）

- GPL の vc4・v3d から作る文書は **`plan/ws141/temp/` に置き、commit しない**（書き写し可、表現が難しい所は code の書き写しも可）。
- commit する物は、GPL を含まない物だけ: 正本の一覧と license の監査の表（path・SHA-256・license）、我々の interface への対応表（zedBSD の言葉で）、段の印の設計、判断の項目。
- register の定義・packet の形の出典は MIT の Mesa（`src/broadcom/`）・Broadcom の公開の文書・device tree の binding とし、file ごとに license を監査の表に（2026-10-04 ユーザー）。
- 定数の一括の改名は p002 の最初（code を書く前）。BLOB は `userland/firmware/` へ移す候補として一覧にする。

## 受け入れ

- `plan/ws141/temp/`（commit しない）に初期化の順・command の順の作業の文書。
- commit する `plan/ws141/` の文書（例 `rpi4-gpu-design.md`・`rpi4-gpu-license-audit.md`）に、正本の一覧と監査の表・対応表・段の印の設計・BLOB の候補・判断の項目がそろい、GPL の code・comment・定数の名前を含まない（design-reviewer の review で確かめる）。

## 検証

design-reviewer の review。code・build・QEMU は無い。
