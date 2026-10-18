<!-- awesome-plan project=zedbsd record=ws260-p014 -->

# ws260-p014 外の display・flip の node・backlight・段を xd9 へ、gen9-* を消す

Status: in-progress（2026-10-18 q1253-i02、P1。cleared は Q1）

- 親: [WS260](../ws.md)。Queue: q1253-i02。設計: [design.md](../design.md) §3.2。ユーザーの決定 3（log を揃える）・4（語を廃止、常に full）。

## 設計からの変え方（意図した違い）

- design §3.2 は「xd-screen・xd-display の口へ」だったが、display 9 の方針（firmware の panel を残して flip、外の display、段）は新しい `display/xd9-*.c/h` に置いた。xd-screen は display 12/13 の plan と light の状態機械で、その口を display 9 のために変えると 5330 側を壊す恐れがあるため（Q1 了承 2026-10-18）。register の列は全部 xd の部品（xd-output・xd-pipe・xd-dbuf・xd-hpd・xd-ddi・xd-clock・xd-power・xd-panel・xd-takeover）。

## 作った物

- `xd9-outputs.c/h`: 外の DDI B〜D（probe、EDID、mode、pipe と DPLL の割り当て、display buffer の移動の順、light・darken・flip・live）。`drv_i915_xd9_pick_mode()`・`drv_i915_xd9_wm_latency()`。xd の型（`i915_xd_io`・`i915_xd9_output`・`i915_xd9_dp_port`・`i915_xd9_pipe_plan`）を直に使う。
- `xd9-node.c/h`（gen9-node.c の書き直し: xd の窓、xd9-outputs、part は `drv_i915_xd_platform_choose_display9()`）、`xd9-flip.c/h`（gen9-flip.c の node の流れ）、`xd9-backlight.c/h`（xd9_pwm を直に）、`xd9-firmware.c/h`（cpuid と VT-d の page の map だけ。点検は xd-takeover）、`xd9-step.c/h`（段の上限。語を読まず常に full、失敗で下がる）。
- caller: bootfb.c（framebuffer の部分は触らない）・i915.c・device.c・irq.c・display.c を xd9 に。
- `platform/amd64/vmunix.mk`: AMD64_I915_SOURCES の gen9-*.c 11 個を xd9-*.c 6 個に（Q1 許可）。
- `src/kern/boot.c`・`include/kern/boot.h`: `i915.gen9.display=` の語を削除（未知の語は読み飛ばされるので、古い boot line でも起動は止まらない）。
- log の文言（決定 3）: `i915: gen9 node:`・`gen9 outputs:`・`gen9 display:` → `i915: xd9-node:`・`xd9-outputs:`・`xd9-flip:`・`xd9-display:`。照合する道具は無い（plan/tools・tests・userland を grep）。
- 試験: host-gen9-{modeset,outputs,golden,mode,backlight,flip}-test・host-gen9-mode-table・host-xd-display9-test を xd の API に。試験の register の名前は試験専用の `tests/display/gen9-fake-regs.h`（production と共有しない写し）。gen9-fake-hw に `i915_gen9_fake_xd()`（model に繋いだ xd の窓）。

## 確かめ（2026-10-18）

- golden: modeset 357・outputs 1323・backlight 17 行は前と同じ。table は 95,619 → 80,571 行: 消えた関数の行だけが減った（train_delay 2,048・train 12,000・wm_word 500・ddb_word 500。訓練の判定と WM/DDB の語は xd の内部で、それぞれ xd の host 試験と modeset の書き込みの golden が受ける）。他の 80,571 行は一致（乱数の消費は残して後の行を比べられるようにした）。table を記録し直した。
- gen9 host: modeset 77（AUX の pack の 2 check を外した。xd-aux の試験が受ける）・outputs 29・golden 14・flip 48・backlight 21・mode 61（train_delay・adjust_train・WM の語の 10 check を外した）、0 failures。xd9 host 24 checks 0 failures。
- `kbuild.sh p014`: 3 組 rc 0、warning 0。
- 未: gen9-*.c/h 22 個と display-regs-gen9*.h 2 個の削除は Q1 に依頼（host の lib が display/*.c を glob するので、削除まで gen9-display.c が host build を止める）。削除の後に host 試験を流し直す。
