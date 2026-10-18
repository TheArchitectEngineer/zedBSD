<!-- awesome-plan project=zedbsd record=ws260-p007 -->

# ws260-p007 backlight を xd-panel へ

Status: in-progress（2026-10-18 q1253-i01、P1。cleared は Q1）

- 親: [WS260](../ws.md)。Queue: q1253-i01。設計: [design.md](../design.md) §3.2。

## 作った物

- `xd-panel.c/h`: display 9 の south の PWM（`struct i915_xd9_pwm`、`drv_i915_xd9_pwm_read`（SPT の族の period と duty を 1 語に詰めた形と CNP の族の別 register の形）、`_decode`（floor は period の 4/255 を切り上げ）、`_duty`・`_percent`（既存の `drv_i915_xd_backlight_scale()` で floor〜period と 0〜100 % を対応、端は明示）、`_set`（詰めた形は読んだ period を保って下半分だけ）、`_pause`・`_resume`（sleep の消灯と復帰、control を保つ）、`drv_i915_xd9_panel_lit()`（PW1 と eDP の transcoder の DDI function）。
- `intel/display-regs-xd9.h`: PWR_WELL_CTL2 の PW1 の state、TRANS_DDI_FUNC_CTL_EDP と enable。PWM の register は既存の `I915_XD_SBLC_PWM_*`（同じ番地）。
- `gen9-backlight.c`: 全部を xd を呼ぶ形に書き直した（register は xd の窓口 `struct i915_xd_io` を device の MMIO に繋いで、PCH の族は `drv_i915_xd_platform_choose_display9()` で）。kern の backlight device の登録・`i915.gen9.display=` の段の確かめは p014 まで残る殻の glue。log の文言は xd の形（`i915: backlight: …`、display 12・13 の backlight.c と同じ接頭）に揃えた（照合する試験・道具は無いことを grep で確かめた）。
- 試験: host-gen9-golden-test の device に PCI の product（0x5917・0x3e92）を入れた（PCH の族の仮定が platform ではなく part から来るため）。

## 確かめ（2026-10-18）

- golden（`run-golden.sh build/tmp/ws260-p007-g1`）: 4 つとも same（backlight の start・get・set・suspend・resume の書き込み、PWM の decode と対応の表）。
- gen9 host: backlight 21・flip 48・modeset 80・outputs 29・golden 14・host-boot-gen9-display 15・host-gen9-display-step 22、全部 0 failures。xd host 試験 all passed。`kbuild.sh p007` 3 組 warning 0。
