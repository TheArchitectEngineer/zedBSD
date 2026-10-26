<!-- awesome-plan project=zedbsd record=ws244-p009 -->

# ws244-p009 license の package を外す・規約の見直し・log の文言・実機の試験の依頼

Status: in-progress（2026-10-26 q1598 P4: 5330 の実機の回帰 PASS（下の「q1598」）、package の directory は main で削除済み。SV7 は未実施（10.0.10.27 に届かない）。cleared は Q1）

- 親: [WS244](../ws.md)。ユーザーの決定 2（2026-10-18、クリック）: 書き直しの後 `userland/base/licenses/i915-driver/` は「外す」。

## 結果（2026-10-19 P1）

### 1. license の package

- Makefile の `CONFIG_DRIVER_PCI_I915=y` で `i915-driver-license` を選ぶ block を外した。tools/release/license-components.json の `i915-intel` の項目を外した（JSON として読めることを確かめた）。
- 先に i915 の tree に外部の MIT の表示が残らないことを確かめた: `grep -rln 'Intel Corporation|Permission is hereby granted|SPDX-License-Identifier: MIT|Derived from the Linux' src/drivers/gpu/i915` が tests/fixtures の 3 file（Mesa の compiler の出力の fixture）だけ → この 3 file の先頭を Zlib と説明にし、`Values referenced: Mesa main @ ab691a1c… (the output of …)` にした。直した後は 0 件。
- `python3 -I tools/release/license-inventory.py --config config/ci/config-amd64.mk --scratch build/tmp/p009-lic`: i915 の項目の不足なし。open items 2 件は amd-navi-firmware・nvidia-gsp-firmware の未登録（この Phase の外、前から）。`no SPDX line: render/vulkan-codec.inc` は生成物（gen_vk_server_codec.py）で、生成器の側の変更が要るので触っていない。
- **削除は Q1 へ**: `userland/base/licenses/i915-driver/`（LICENSE・Makefile）。どこからも選ばれなくなった。
- 写しの記録 plan/ws129/（licenses.md・licenses-index.txt・licenses-generated.md）に i915-intel の行が残る。WS129 の記録なので P1 は触っていない（Q1 の判断）。

### 2. log・trace の文言の Linux の関数名

- i915 の .c の文字列の中の Linux の関数名（intel_irq_install・intel_engines_init・__intel_gt_reset・intel_context_create・switch_to_kernel_context・i915_request_create・wa_list_srm・intel_bios_init:・drm_dp_read_dpcd_caps・gen12_ctx_gt_mocs_init・gen9_wa_init_mcr・i915_vma_pin_ww ほか）を自前の短い名前（"irq install"・"engine init"・"sanitize:gt reset"・"context create"・"kernel context switch"・"request create"・"workaround store"・"vbt:"・"dpcd caps read"・"Gen12 context MOCS: BLIT_CCTL"・"MCR steering (Gen9)"・"scratch pin" ほか）にした。同じ文字列を比べる code・試験（tests/execution の ktest-gt.c・draw-test.c・eu-test.c・fhd-render.c）は同じ置き換えで追従。tree の外（plan/tools・tests/・tools/）にこれらの文言を照合する物は無い（grep で 0 件）。
- 同じ file の comment の Linux の関数名の注（device.c・defaults.c・migrate.c・verify-workarounds.c・intel/mocs.h・intel/workarounds.h）も外した。migrate.c の Linux の呼び出しの写しの comment は自前の説明に。

### 3. 規約の全文の見直し

- WS244 の p007〜p009 の変更は comment と文字列だけ。plan/coding-style.md の comment の規則（複数行の形、referenced は block の直前の複数行 comment、file の先頭に出典を書かない）で目視した。新しく書いた comment の行は 80 桁以内（前からある表の行を除く）。

### build（2026-10-19）

- 既定・I915_TESTS=y・I915_TEST_SET=vke2・execution の 4 組 rc=0、warning 0。

## T1 への実機の試験の依頼（文面、Q1 から送る）

> WS244（i915 の Linux 由来の code の書き直し、p002〜p009）の実機の回帰。main の tip の amd64 の既定の image（config/ci/config-amd64.mk）で、5330（Gen12、内蔵 LCD）と SV7（Gen9）を素の起動。確かめ: (1) 起動して login prompt（boot-test.sh の画面）と desktop が出る、(2) 内蔵 LCD の表示（PP_STATUS・backlight の PWM・frame counter・SURFLIVE を ssh で読む、guardrail の「内蔵 LCD の表示」の代わりの証拠）、(3) dmesg の i915 の行に失敗の段（"failed"・"rc=-"）が無い。log の文言が変わった（例 "intel_irq_install" → "irq install"、"intel_bios_init:" → "vbt:"）ので、旧い文言で grep する判定は使わない。GPU の描画（vkcube 相当）も 1 回。

## 残り

- Q1: `userland/base/licenses/i915-driver/` の削除、plan/ws129 の写しの扱い、T1 への依頼、実機の結果の判定。

## q1598（P4、2026-10-26）: 5330 の実機

- main 684a5243f（WS244 の p002〜p009 を全部含む）の image（`plan/ws245/tests/config-5330.mk`）で 5330 を素の起動（WS245 の q1597 と同じ 1 回、`plan/ws245/tests/5330-run.sh`、証拠 `plan/ws245/tests/evidence/p4-main-684a5243f-20261026.md`）。
  - (1) 起動と desktop: `kern.boot.login: graphical`、SESSION start、compositor の画面（keiland-shot の PNG、`plan/ws245/temp/5330/p4-main-684a5243f-20261026.png`、git の外）に desktop と Settings の初回の画面。
  - (2) 内蔵 LCD: survey `lit`（PP_STATUS on、backlight の PWM on、frame counter +325、SURFLIVE が resident の buffer、CRC 2 回同値、underrun 0）、backlight-probe の get/set 100/get が 100。
  - (3) dmesg の i915 の行に `failed`・`rc=-` が 0（grep で数えた）。新しい文言（"irq install"・"vbt:" など）の下で起動の段が全部通る。
  - GPU の描画: compositor の present が GPU の submit で通る（`perf:` の行 4、例 40 submits・24 batches、per submit 3.60 ms）。vkcube 相当の単独の program は流していない（未実施）。
- `userland/base/licenses/i915-driver/` は main に無い（Q1 が削除済み、89f0c3d9d）。
- 未実施: SV7（Gen9、10.0.10.27 は 2026-10-26 23:4x に No route to host）。plan/ws129 の写しの扱いは Q1。
