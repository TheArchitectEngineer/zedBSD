<!-- awesome-plan project=zedbsd record=ws260-p006 -->

# ws260-p006 AUX・GMBUS・EDID を xd-aux・xd-gmbus へ

Status: in-progress（2026-10-18 q1253-i01、P1。cleared は Q1）

- 親: [WS260](../ws.md)。Queue: q1253-i01。設計: [design.md](../design.md) §3.2。

## 作った物

- `gen9-io.c/h`: `drv_i915_skl_xd_io()`（gen9 の register の表を 1 つの xd の窓口の hooks に繋ぐ。read・write は表の、wait は 1 ms 単位なら表の msleep、他は udelay。node の mutex が使い手を直列にする）。
- `xd-aux.c/h`: `drv_i915_xd_aux_edid_blocks()`（最初の segment の EDID の block を、block ごとの offset の書き込みと 128 byte の読みで 1 つの I2C の transaction に）。
- `xd-gmbus.c/h`: `drv_i915_xd9_gmbus_edid_blocks()`（同じ形の GMBUS 版。PCH の GMBUS unit の clock gating を転送の間だけ止める、display 9 の WA #0868）、`drv_i915_xd9_gmbus_pin()`（DDI B・C・D の pin 5・4・6）、`drv_i915_xd9_gmbus_pin_valid()`。`struct i915_xd_gmbus_request` に `display9`（pin の番号が display 9 の south の物）を足し、xd-hpd と host-xd-gmbus-test の組み立てでは 0 を入れた（display 11〜13 の pin の検査は変わらない）。
- `intel/display-regs-xd9.h`: display 9 の GMBUS の pin と GMBUS unit の clock gating の bit。
- `gen9-ddc.c`: aux_xfer・dpcd_read・dpcd_write・aux_edid・gmbus_edid・gmbus_pin を xd を呼ぶ殻にした（589 → 230 行）。aux_pack・aux_unpack は試験の model が使うので p014 まで残す。

## 確かめ（2026-10-18）

- golden の意図した違い: modeset・outputs の書き込みの列は、AUX（0x64000〜0x643ff）・GMBUS（0xc5100〜0xc5130）・SOUTH_DSPCLK_GATE_D を除くと全場面で同じ（`python3 -I plan/ws260/tests/golden-diff.py 旧 新 64000-643ff c5100-c5130 c2020-c2020` で全場面 filtered-same）。違いは xd の AUX・GMBUS の手順そのもの: DPCD の読みの前の DPCD_REV の wake-up read（DP の規格の勧め）、no answer の時の DPCD の 32 回の再試行、I2C-over-AUX の message ごとの address-only の開始、GMBUS の待ちの GMBUS4 の書き込みと最初の NAK の再試行。AUX の制御の語（0xfe40023f など）は同じ。golden を記録し直した（Q1 に報告）。
- host-gen9-modeset-test の 2 つの check を xd の手順に追従（deferral 2 回の後の DPCD の読みは wake-up read の 3 回と本体の 1 回で 4 transfer、sink の無い port は 32 message × 5 = 160 transfer で ETIMEDOUT）。80 checks PASS。outputs 29、golden 14 PASS。
- xd host 試験 all passed（gmbus の pin の検査も前のまま）。xd9 host 試験 all passed。`kbuild.sh p006b` 3 組 warning 0。
- 注: 外の DP の sink の無い port の DPCD の読みは、AUX の timeout が 1.6 ms なら最長で約 0.26 秒かかる（前は 5 transfer）。display 12・13 の xd と同じ振る舞い。
