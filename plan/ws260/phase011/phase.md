<!-- awesome-plan project=zedbsd record=ws260-p011 -->

# ws260-p011 transcoder・pipe・plane の点灯と消灯を xd へ

Status: in-progress（2026-10-18 q1253-i02、P1。cleared は Q1）

- 親: [WS260](../ws.md)。Queue: q1253-i01（前の世代が途中まで）、q1253-i02（仕上げ）。設計: [design.md](../design.md) §3.2。

## 作った物

- `display-regs-xd9.h`: TRANS_MULT・TRANSCONF・TRANS_DDI_FUNC_CTL・PIPE_MISC・PIPE_FRMCNT、第 1 plane の PLANE_*、CHICKEN_TRANS の training override。
- `xd-pipe.c/h`: `drv_i915_xd9_pipe_configure()`（source・depth・M/N・timing は xd の `drv_i915_xd_pipe_source/m_n/timing`）、`_pipe_func()`、`_pipe_start()`・`_pipe_stop()`、`_plane_on()`・`_plane_off()`、`_pipe_wait_vblank()`。
- `xd-ddi.c/h`: `drv_i915_xd9_ddi_boost()`（`_ddi_levels()` から分けた）、`drv_i915_xd9_ddi_hdmi_start()`（HDMI の行を 10 番目に、boost、Display WA #1143、buffer on）。
- `xd-output.c/h`: `struct i915_xd9_output` と `drv_i915_xd9_output_on()`・`_output_off()`（DPLL、DDI clock、IO well、DP の buffer と訓練、transcoder の clock・MSA、pipe、DDI function、transcoder、HDMI の buffer、plane。失敗は始めた物を止める）。
- `gen9-modeset.c`: output_on/off・plane_on/off・wait_vblank を xd を呼ぶ殻に。transcoder の timing・M/N・DDI function・HDMI buffer・iboost の自前の手順を消した（625 → 約 380 行）。WM の書き込みと hotplug は p012。

## 確かめ（2026-10-18）

- `sh plan/ws260/tests/run-golden.sh build/tmp/ws260-p011g`: modeset 357・outputs 1323・backlight 17・table 95,619 行、4 つとも same（書き込みの列は前と同じ。golden は記録し直していない）。gen9 host: modeset 79・outputs 29・golden 14、0 failures。
- `run-xd-host-tests.sh`: all passed。`run-xd9-host-tests.sh`: 24 checks 0 failures。gen9 flip 48・backlight 21・mode 71、0 failures。
- `sh plan/ws260/tests/kbuild.sh p011`: default・vke2・execution の 3 組 rc 0、warning 0。
