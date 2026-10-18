<!-- awesome-plan project=zedbsd record=ws244-p008 -->

# ws244-p008 display の残りの言及の掃除

Status: in-progress（2026-10-19 q1260-i02 P1、範囲は済み。cleared は Q1）

- 親: [WS244](../ws.md)。範囲: display/ の comment に残る Linux・drm の関数・型の名前と、無くなった file への参照。log の文字列（"intel_bios_init:…" ほか）は p009。

## 結果（2026-10-19 P1）

- 探し方: `python3 -I build/tmp/p007-tools/check.py 'Linux|linux|drm_|\bintel_[a-z_]+|Derived|Structure-faithful|reference.s' src/drivers/gpu/i915/display/*` で referenced の段落の外の言及を列挙（61 行、うち clock の「reference」・port の reference count の物は対象外）。
- 直した file: internal.h（VBT・OpRegion・eDP・LCD の検査の古い comment。「Structure-faithful port of intel_bios_init()」「Linux-derived」「the reference's …」の説明を自前の説明に、intel_pps_*() などの注を動作の説明に、OpRegion の offset と DMC の arena の大きさに `Values referenced: Linux v6.19 …(intel_opregion.c / intel_dmc.c)`、無くなった vbt.c・takeover.c・modeset.c・diagnostics.c・modeset-internal.h への参照を今の file（vbt-shell.c・vga.c・output.c・edp-device.c・run-log.c）に）、xd-dp.h・xd-dp.c（drm_* の名前の注を外した）、dp-ext-kern.h（`Procedure referenced: Linux v6.19 …/intel_dp_link_training.c (intel_dp_get_link_train_fallback_values)`）、interrupts.c・vbt-shell.c・present.c。
- 引用元の名前は Linux v6.19（build/ws244-linux619）で grep して在ることを確かめた。
- code は不変（comment を除いた行を前の版と diff して差なし）。
- build（`sh build/tmp/kbuild.sh p008`）: 既定・vke2・execution の 3 組 rc=0、warning 0。

## 残り

- log・trace の文字列に残る Linux の関数名（vbt-shell.c の "intel_bios_init:…"、run-log.c の "drm_dp_read_dpcd_caps" など）は p009。
