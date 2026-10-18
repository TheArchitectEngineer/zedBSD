<!-- awesome-plan project=zedbsd record=ws260-p008 -->

# ws260-p008 power well を xd-power へ

Status: in-progress（2026-10-18 q1253-i01、P1。cleared は Q1）

- 親: [WS260](../ws.md)。Queue: q1253-i01。設計: [design.md](../design.md) §3.2。

## 作った物

- `display-tables-xd9.h`: display 9 の well の表 `i915_xd9_wells`（PWR_WELL_CTL2 の MISC IO 0、DDI A/E IO 1、DDI B〜D IO 2〜4、PW1 14（PG1 の fuse、DMC が DC で消す）、PW2 15（PG2 の fuse、pipe B・C、transcoder A〜C、AUX B〜D、VGA、audio））。行に well を入れた（7 個）。request と state の bit は display 11〜13 と同じ並び（既存の `I915_XD_PWR_WELL_REQUEST/STATE`）。
- `xd-power.c/h`: `drv_i915_xd9_power_pw2_on()`（DC5・DC6 を止め、上がっていなければ既存の `drv_i915_xd_power_well_enable()` で request・state・fuse）、`drv_i915_xd9_power_ddi_io()`（on は well_enable、off は既存の `drv_i915_xd_power_well_disable()`）。`display-regs-xd9.h` に well の index。
- `gen9-modeset.c`: `drv_i915_skl_pw2_on()`・`drv_i915_skl_ddi_io_power()` を xd を呼ぶ殻にし、使わなくなった定数を消した。
- host-xd-display9-test: 行の well が 7 個・DDI buffer の組があることを足した。

## 確かめ（2026-10-18）

- golden（`run-golden.sh build/tmp/ws260-p008-g2`）: 4 つとも same。
- gen9 host: modeset 80・outputs 29・golden 14 PASS。xd9・xd host 試験 all passed。`kbuild.sh p008` 3 組 warning 0。
