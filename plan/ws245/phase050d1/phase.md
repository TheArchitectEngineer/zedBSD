<!-- awesome-plan project=zedbsd record=ws245-p050d1 -->

# ws245-p050d1: 内蔵 panel の sink の取得と kernel の panel の hook を xd に

Status: in-progress（2026-10-17 P1、q1229-i08。Q1 の判断で p050d を 4 段 p050d1〜d4 に分けた 1 段目）
Parent: [WS245](../ws.md)
Queue: q1229-i08（P1）
設計: [design.md](../design.md) §6.2 の p050 の行、§2.9（AUX）、§2.11（panel power）、§3.4、§4.2。着手の手がかりは [phase050d](../phase050d/phase.md)「着手の手がかり」の 1〜3

## 範囲

- 新しい xd の部品（内蔵 panel の sink）: panel の電源の順序の状態（PPS の選び、遅延の program と待ち、VDD を誰が要るか、最後の電源の on/off と backlight off の時刻、遅れての VDD off）、sink の取得（PPS の用意 → VDD → DPCD の能力 → eDP の DPCD の版 → EDID を AUX の I2C で）、panel の操作（on・off・VDD on・VDD off・backlight の bit）、DPCD の read/write（AUX の power と VDD を保ったまま xd-aux の native の転送）。
- kernel の道の付け替え: `dp-sink.c` の `drv_i915_edp_device_init_connector/_fini` の eDP の部分、modeset の kernel の backend の hook（`drv_i915_edp_emit_dpcd_read/_write/_read_dpcd_caps/_panel`）を xd の部品に。遅れての VDD off の timer と AUX の power の参照は殻（kernel の環境）が xd に渡す。
- 残す物: 旧の Linux の eDP の world（`drv_i915_edp_begin/_init_late/_end/_panel_op`、`panel.c`・`aux.c` の Linux の転送、`edid-read.c`）は host の旧試験（host-dp-test・host-lcd-modeset-test）と ktest が使う間は残す（kernel からは呼ばれない）。消すのは p050d4（oracle の check を xd の試験へ移してから）。DP-ext（p052）が使う eDP device の lock・thread（`drv_i915_dp_kernel_*`）は残す。

## 受け入れ

- 新しい xd の host 試験（run-xd-host-tests.sh）: host-dp-test の eDP の oracle（PPS の選び、DPCD の能力と eDP の版、VDD を転送の間保つ、最初の転送の前の power-up の待ち、遅れての VDD off の時刻（5 × power cycle）、power cycle の待ち（T12）、AUX の retry の有限、native の DEFER・NACK、短い返事、I2C の DEFER・部分の read、短い EDID と全部 0 の EDID の拒み、port A 以外と Type-C の AUX の拒み）を xd の形で。
- build（既定と I915_TESTS=y の 5 組）warning 0、旧 host 試験（dp 72・lcd 120）と harness out50c が前の段と同じ（kernel の道だけを変えるので）。R11 の数。
- 5330 の回帰は全部の段の後に 1 回（ユーザーの決定 2026-10-16）。

## 記録

（実装の後に書く）
