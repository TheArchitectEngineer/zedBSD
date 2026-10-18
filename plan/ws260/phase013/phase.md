<!-- awesome-plan project=zedbsd record=ws260-p013 -->

# ws260-p013 firmware の点検と flip の register を xd へ

Status: in-progress（2026-10-18 q1253-i02、P1。cleared は Q1）

- 親: [WS260](../ws.md)。Queue: q1253-i02。設計: [design.md](../design.md) §3.2。

## 作った物

- `display-regs-xd9.h`: transcoder の place（EDP は 0xf000、`I915_XD9_TRANS_OFFSET`）、PW2 の state、DC_STATE_EN、FBC_CTL、PSR_CTL（eDP）、TRANS_HTOTAL/VTOTAL/LINK_M1/N1、DDI function の mode・eDP の input、PIPESRC、PLANE_SURFLIVE、plane control の format・tiling・rotation・async、stride、256 KiB の整列、PS_CTRL、DE_PIPE の IMR/IIR/IER と flip done・fault、master の pipe の bit。
- `xd-takeover.c/h`: display 9 の readout（`struct i915_xd9_fw_registers`・`struct i915_xd9_fw_readout`、`drv_i915_xd9_fw_read()`・`_fw_decode()`・`_fw_log()`・`drv_i915_xd9_dp_pixel_khz()`）、resume の確かめ `drv_i915_xd9_fw_runs()`、GT の起動の前の点検 `drv_i915_xd9_fw_check()`（plane の重なりと VT-d。VT-d の読みと壁は xd の `i915_xd_fw_read_vtd`・`i915_xd_fw_vtd_walls` を使う）。
- `xd-clock.c/h`: `drv_i915_xd9_dpll_rate_port_khz()`（DPLL_CTRL1 の rate の code から port clock、表は p004 の物）。
- `xd-pipe.c/h`: `drv_i915_xd9_plane_flip()`・`_plane_surf_live()`・`_plane_fault()`、`drv_i915_xd9_flip_irq_clear()`・`_unmask()`・`_mask()`・`_ack()`。
- `gen9-flip.c`: readout・decode・log・latch・arm・irq・resume の register を xd を呼ぶ形に（約 590 行減）。node の流れ（buffer・GGTT・present・lease）は p014 で xd-screen へ。flip の状態に xd の窓（`io`、interrupt の handler 用の `irq_io`）を持つ。
- `gen9-firmware.c`: 368 → 約 130 行。cpuid と VT-d の page の map だけを env の hooks として残し、点検は xd。
- 試験: host-gen9-flip-test・host-gen9-mode-table を xd の名前に。
- log の文言（ユーザーの決定 3）: 移した readout と点検の行は `i915: xd9-firmware:` に。照合する道具・試験は無い（grep で確かめた。plan/ws237/tests/host-gen9-display-step.c は gen9-display の段の語の試験で p014 で扱う）。

## 確かめ（2026-10-18）

- `run-golden.sh build/tmp/ws260-p013g`: 4 つとも same（table の firmware の readout、SV7 と変形 2000 を含む）。gen9 host: modeset 79・outputs 29・golden 14・flip 48・backlight 21・mode 71、0 failures。xd host all passed、xd9 host all passed。
- `kbuild.sh p013`: 3 組 rc 0、warning 0。
- flip の latch・arm・irq の書き込みの列は golden に無い（GGTT・buffer・completion の stand-in が要る）。代わりに旧と新の書き込みの順を手で照らした: arm（stride が違う時だけ stride、surf、posting read）、irq の enable（IIR 2 回、IER、IMR、posting read）、disable（IMR、posting read、IER、IIR 2 回）、handler（master の bit、IIR の読みと書き戻し）は同じ。実機の確かめは p015 の SV7。
- 違い: display の register の読み書きが `drv_i915_raw_read32/write32` から xd の窓（`drv_i915_read32/write32`。display の番地は forcewake の domain の外なので動作は同じ）になった。
