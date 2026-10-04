<!-- awesome-plan project=zedbsd record=ws141-p001 -->

# ws141-p001: 文書（Linux の vc4・v3d の初期化の順と command の投入の順、正本と license、我々の interface への対応）

Status: in-progress（q691-i01、P2。commit する文書はそろい design-reviewer の review を反映済み。clearance は Q1 の判定と判断の項目の扱いの後）
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

## 結果（2026-10-04、q691-i01、P2 generation11〜12）

- 正本（commit しない、`plan/ws141/temp/`）: Linux v6.19（`05f7e89a…`）の vc4・v3d・周辺の sparse clone、Mesa 25.3.6（`06f9e283…`）の `src/broadcom`、BCM2711 ARM Peripherals の PDF、固定の firmware（`vendor/raspberrypi-firmware` の `3d301dd9`）の DTB と overlay（`fwdtb/`、dtc で dts に戻した）、Raspberry Pi の firmware の wiki（`fw-wiki/`）。
- 作業の文書（commit しない）: `temp/v3d-init-and-submit.md`（634 行）、`temp/vc4-display-init.md`（537 行）、`temp/vc4-hdmi-2711.md`（678 行）。どれも完了。実機の観測は無い。
- commit する文書: [rpi4-gpu-license-audit.md](../rpi4-gpu-license-audit.md)（正本と file ごとの path・SHA-256・license、BLOB の確認）、[rpi4-gpu-design.md](../rpi4-gpu-design.md)（構成、方針、段 P0・N0〜N2・P1〜P5・H0〜H10・V0〜V10、interface の対応表、段の印、骨格、BLOB と表、危険、判断の項目 17）。
- design-reviewer の review（2026-10-04）: 高 3（resource_map の口と非一貫の cache、display と V3D の device の分割、N2 の位置の食い違い）・中 11・低 13。全部を design に反映した（判断の項目 11〜17 を追加、N2 を N1 の直後に固定、割り込みの口を `kern_irq_*` に、console の 80 桁と領域、QEMU での安全な抜けと boot の parameter、BO の連続の run と clean/invalidate、OOM の worker と pool、PTE の uncached、DTB の hash の誤記と overlay の注記、mailbox の 8 word の上限、wiki の出典の追加）。commit する 2 文書に GPL の code・comment・Linux の識別子は無い（review の観点 1）。
- ユーザーの決定: 判断の項目 1（register の offset と bit は事実として使う、2026-10-04、Guardrail の改訂 main `0f7d70c`）。
- 残り: 判断の項目 2〜17 は未決（Q1 がユーザーに確かめる）。計画に無い依存: 判断の項目 14（EDID の mailbox は `rpi4-firmware.c` の変更が要る、WS141 の範囲の外）。計画の訂正: interface の実名は `struct drv_gpu_ops`、HDMI の DDC の Linux の driver は `drivers/i2c/busses/i2c-brcmstb.c`。
- 検証: design-reviewer の review だけ（code・build・QEMU は無い）。
