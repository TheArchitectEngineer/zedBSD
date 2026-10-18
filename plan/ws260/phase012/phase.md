<!-- awesome-plan project=zedbsd record=ws260-p012 -->

# ws260-p012 WM・DDB の書き込みと順序、hotplug の sense を xd へ

Status: in-progress（2026-10-18 q1253-i02、P1。cleared は Q1）

- 親: [WS260](../ws.md)。Queue: q1253-i02。設計: [design.md](../design.md) §3.2。

## 作った物

- `display-regs-xd9.h`: plane 2・3 の CTL・SURF・BUF_CFG、NV12 BUF_CFG、CURBASE、CUR_WM_TRANS、line time の mask、SPT〜CMP の SDEISR の DDI B〜D・PCH_PORT_HOTPLUG の enable・SFUSE_STRAP の検出の bit。
- `xd-dbuf.c/h`: `drv_i915_xd9_wm_write_pipe()`（第 1 plane の 8 level・transition・range・NV12、cursor の level（off）・range、line time。WM・DDB の語は xd の `i915_xd_wm_write_level`・`i915_xd_ddb_write`）、`_wm_arm_pipe()`（plane 2・3、surface の書き直し、vblank）、`_wm_arm_pipes()`（昇順・降順で 1 pipe ずつ）。
- `xd-hpd.c/h`: `drv_i915_xd9_hpd_enable()`・`_present()`・`_connected()`（DDI B〜D）。
- `gen9-modeset.c`: write_pipe_wm・hotplug_enable・ports_present/connected を殻に。`drv_i915_skl_xd_plan()`（skl の WM を xd の plan へ）。display-regs-gen9*.h の include を外した。
- `gen9-outputs.c`: rewrite を xd の `_wm_arm_pipes()` を呼ぶ殻に、arm を消した。

## 確かめ（2026-10-18）

- `run-golden.sh build/tmp/ws260-p012g`: modeset 357・outputs 1323・backlight 17・table 95,619 行、4 つとも same（記録し直しなし）。gen9 host: modeset 79・outputs 29・golden 14、0 failures。
- xd host all passed、xd9 host all passed、gen9 flip 48・backlight 21・mode 71、0 failures。
- `kbuild.sh p012`: 3 組 rc 0、warning 0。
- 注: 旧 `drv_i915_skl_ddb_word()` は end <= start を 0 にしたが、xd は end == 0 だけを 0 にする。plan の出す範囲では同じ（golden が同じ）。
