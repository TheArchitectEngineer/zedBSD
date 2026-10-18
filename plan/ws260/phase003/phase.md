<!-- awesome-plan project=zedbsd record=ws260-p003 -->

# ws260-p003 xd-platform に display 9 の記述

Status: in-progress（2026-10-18 q1253-i01、P1。cleared は Q1）

- 親: [WS260](../ws.md)。Queue: q1253-i01。設計: [design.md](../design.md) §2.1・§3.2。

## 作った物

- `intel/display-tables-xd9.h`（新規、xd-platform.c だけが include）: display 9 の 11 行（Skylake・Kaby Lake・Coffee Lake の H/S・U・Y、Comet Lake の H/S・U）。PCI id は tree の pci-ids.h の 4 つの list を、Gen9 の mode set（gen9-mode.c）の ULT・ULX の list と同じ分け方で power class ごとに分けた。port は DDI A〜D（自前の buffer、AUX A〜D、south の hotplug pin A〜D）と DDI E（A の lane を共有、AUX 無し、駆動しない）。3 pipe、1 slice の 892 block、WM 8 level、south は SKL・KBL が SPT の族、CFL・CML が CNP の族。well・PLL・CDCLK・swing・VBT の map は空（後の段で埋める）。stepping の表は持たない（display 9 で xd が stepping で分ける段は無い）。値の block ごとに `Values referenced:`（PRM と tree の pci-ids.h）。
- `xd-platform.h/.c`: `I915_XD_PHY_DDI`、`I915_XD_PLL_MAP_DPLL_CTRL2`、`I915_XD_KIND_SKL/KBL/CFL/CML`。`drv_i915_xd_platform_choose_display9()`・`_display9_count()`・`_display9_row()`（display 9 の行だけから選ぶ）。今の `drv_i915_xd_platform_choose()` は display 11〜13 の行だけのまま（Gen9 の id は今までどおり ENODEV。xd の起動は display 9 を選ばない＝「選ぶだけで呼ばない」）。選ぶ処理は表を引数に取る static の `i915_xd_platform_choose_in()` に移した（振る舞いは同じ）。
- 試験: `tests/display/host-xd-display9-test.c`（24 checks: 全 Gen9 id が display 9 の 1 行だけに載り display 11〜13 に無い、行の power class が `drv_i915_skl_sku()` と一致、SPT の仮定・読み・CNP の拒否、CMP の読み、TGL の id は ENODEV、行の形）。runner `plan/ws260/tests/run-xd9-host-tests.sh`。

## 確かめ（2026-10-18）

- `sh plan/ws260/tests/run-xd9-host-tests.sh build/tmp/ws260-p003-a`: xd_display9_host_test 24 checks, 0 failures。
- `sh plan/ws245/tests/run-xd-host-tests.sh build/tmp/ws260-p003-xd`: all passed（display 11〜13 は変わらない）。
- `sh plan/ws260/tests/run-golden.sh build/tmp/ws260-p003-g`: 4 つとも same。
- `sh plan/ws260/tests/kbuild.sh p003`: 3 組とも rc 0、warning 0。

## 残り

- p014 で display 9 の行を xd の起動の選択に入れる（その時に `drv_i915_xd_platform_choose()` と表を 1 つにするかを決める。host-xd-platform-test の「Gen9 の 0x5917 は ENODEV」の check と行の数 11 も追従）。
