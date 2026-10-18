<!-- awesome-plan project=zedbsd record=ws260-p010 -->

# ws260-p010 DDI buffer・signal level・iboost・DP_TP・link training を xd へ

Status: in-progress（2026-10-18 q1253-i01、P1。cleared は Q1）

- 親: [WS260](../ws.md)。Queue: q1253-i01。設計: [design.md](../design.md) §3.2。

## 作った物

- `display-regs-xd9.h`: DDI_BUF_CTL・DDI_BUF_TRANS・DP_TP_CTL/STATUS（display 9 は DDI ごと）・DISPIO_CR_TX_BMU_CR0 の field。
- `xd-ddi.c/h`: `drv_i915_xd9_ddi_buffers()`（translation の行）、`drv_i915_xd9_ddi_levels()`（行の iboost を balance leg に、buffer の語の行）、`drv_i915_xd9_ddi_buffer_off()`、`drv_i915_xd9_dp_train()`（訓練は xd の `drv_i915_xd_dp_train()`・`drv_i915_xd_dp_train_end()` で、DDI を source の hooks（prepare: 動いている transport を止めて pattern 1 と buffer、levels: 第 1 lane の level の行（既存の `drv_i915_xd_combo_level_row()`）、pattern: DP_TP_CTL の field、idle: idle pattern と DDI A 以外の待ち）、sink は DDI の AUX（xd-aux の DPCD）。TPS3 あり・TPS4 なし・lane ごとの level なし・repeater は探さない）、`drv_i915_xd9_dp_sink_power()`。`struct i915_xd9_dp_port`。
- `gen9-link.c`: 全部を xd を呼ぶ殻に（530 → 約 150 行）。clock recovery・channel EQ・訓練の調整の自前の手順を消した。
- host-gen9-modeset-test: 訓練の試行回数（xd は数を出さない）の 2 check を、訓練の結果の swing の check に直し、失敗の回数の check を外した（失敗は EIO と後始末の check が残る）。

## 確かめ（2026-10-18）

- golden の意図した違い: modeset・outputs の書き込みの列は AUX・GMBUS・SOUTH_DSPCLK_GATE_D を除くと全場面で同じ（`golden-diff.py … 64000-643ff c5100-c5130 c2020-c2020` で全場面 filtered-same。DDI buffer・iboost・DP_TP の書き込みの値と順は前と同じ）。違いは AUX の DPCD の交換だけ（xd の訓練は caps の読み直しなど DPCD の読み書きが少し増える）。golden を記録し直した。
- gen9 host: modeset 79・outputs 29・golden 14 PASS。xd・xd9 host 試験 all passed。`kbuild.sh p010` 3 組 warning 0。
