<!-- awesome-plan project=zedbsd record=ws260-p009 -->

# ws260-p009 DPLL の enable/disable と DDI clock の選択を xd-clock へ

Status: in-progress（2026-10-18 q1253-i01、P1。cleared は Q1）

- 親: [WS260](../ws.md)。Queue: q1253-i01。設計: [design.md](../design.md) §3.2。

## 作った物

- `display-tables-xd9.h`: display 9 の DPLL の表 `i915_xd9_plls`（DPLL 0〜3: select、enable の register、CFGCR1/2）。行に入れた。
- `display-regs-xd9.h`: DPLL_CTRL2 の field、DPLL の enable・CFGCR1・DPLL_STATUS の lock。
- `xd-clock.c/h`: `drv_i915_xd9_dpll_enable()`（CTRL1 の field、CFGCR1/2、enable、lock の待ち。DPLL 0 は CD clock を作るので動かさない）、`drv_i915_xd9_dpll_disable()`、`drv_i915_xd9_ddi_clock()`（DPLL_CTRL2 の DDI の select と override、clock off）。PLL の行は既存の `drv_i915_xd_dpll_desc()` で引く。
- `gen9-modeset.c`: `drv_i915_skl_dpll_on/off()` と static の DDI clock を xd を呼ぶ殻にし、`i915_skl_dpll_ctl()` と lock の定数を消した。
- host-xd-display9-test: 行の PLL が 4 個。

## 確かめ（2026-10-18）

- golden（`run-golden.sh build/tmp/ws260-p009-g1`）: 4 つとも same。gen9 host（modeset 80・outputs 29・golden 14）PASS、xd9・xd host 試験 all passed、`kbuild.sh p009` 3 組 warning 0。
