<!-- awesome-plan project=zedbsd record=ws278-p009 -->

# ws278-p009 ファンと温度

- 親: [ws278](../ws.md)、担当: P3（2026-10-22）
- Status: in-progress（実装・build・板の確認済み、Q1 の判定待ち）
- 出典: 2026-10-22 ユーザー「そういえばPi5のファンが回ってないんですが、何か制御を忘れてたりしませんか？」「GPIOヘッダではなくて、専用の4ピンファンヘッダに接続しています。」

## 原因

zedBSD に Pi 5 のファン・温度の code が無かった。firmware は RP1 の PWM1 の clock を止めたまま（CLK_PWM1_CTRL 0x01000000、enable も source も無し）kernel に渡すので、誰も PWM を出さずファンは止まっていた。

## 目的と受け入れの条件

- BCM2712 の AVS monitor の温度を読み、sysctl hw.thermal に出す（Pi 4 には温度の sysctl が無いので、ACPI と同じ hw.thermal を使う）。
- 専用の 4 pin のファン端子（RP1 の PWM1 channel 3、RP1 の GPIO 45、tach は RP1 側が PWM1 の +0x3c に rpm を書く）を Linux の pwm-fan と同じ trip・level・hysteresis で回す。温度が読めない時は最大の level。
- `CONFIG_DRIVER_RPI5_FAN`（既定 y、config-rpi5.mk で y）。Pi 4 は不変。
- build（rpi5・rpi4）warning 0、板で温度・duty・rpm・level の変化を見る。

## 設計

- `src/drivers/platform/rpi5/rpi5-fan.c`・`.h`（新規）。`kern_platform_refresh_devices()` から `drv_rpi5_fan_start()`（RP1 の後）。
- 値は firmware が渡す device tree から読む（`pwm-fan` の pwms・cooling-levels・rpm-offset、`brcm,bcm2711-thermal` の親の reg、cpu-thermal の coefficients・polling-delay・trips・cooling-maps）。無い部分は Linux の bcm2712-rpi-5-b.dts・bcm2712-ds.dtsi の値（slope -550、offset 450000、trip 50/60/67.5/75 °C の hysteresis 5 °C → level 1〜4、levels 0/75/125/175/250、period 41566 ns、反転）。
- PWM1 の clock: RP1 の clock controller で aux source を xosc（50 MHz）、divider 1、enable（clk-rp1.c）。channel: ctrl を既定（0x101）、duty・range（2078）・極性、global の enable と SET_UPDATE（pwm-rp1.c）。level 0 は Linux と同じく channel を止める。pin: GPIO 45 を pull-down、function 0（pwm1）。`rp1-gpio` に `drv_rp1_gpio_set_pull()` を足した。
- thread が polling-delay（1000 ms）ごとに温度を読み、trip は温度で crossed、温度 − hysteresis 未満で解除、crossed の最高の level へ。level の変化で 1 行、5 回後に rpm を 1 行。

参照（Raspberry Pi Linux rpi-6.12.y）: arch/arm64/boot/dts/broadcom/bcm2712-rpi-5-b.dts・bcm2712-ds.dtsi・rp1.dtsi、drivers/pwm/pwm-rp1.c、drivers/hwmon/pwm-fan.c、drivers/thermal/broadcom/bcm2711_thermal.c、drivers/clk/clk-rp1.c、drivers/pinctrl/pinctrl-rp1.c。firmware の bcm2712d0-rpi-5-b.dtb（3d301dd）を dtc で開き、同じ値を確かめた。

## 確認（2026-10-22 P3）

- host: `make ZEDBSD_CONFIG=config/ci/config-rpi5.mk BUILD=build/rpi5 build/rpi5/vmunix -j16` rc=0、warning 0、vmunix check PASS（sha256 847a2ad4…）。`make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p3-rpi4 build/p3-rpi4/kernel.elf -j16` rc=0、warning 0、rpi5-fan は build されない。`git diff --check` 問題なし。
- 実機（Pi 5、kei@10.0.30.5、/boot/boot0/vmunix を置き換え、元の GPU の試験の kernel は vmunix.gputest、vmunix.good は不変）:
  - `rpi5-fan: PWM clock was control 01000000 divider 1.00000000; now the crystal`
  - `rpi5-fan: 58400 mC (status 000106c8), fan level 1 of 4 (duty 611 of 2078, channel 3), 5 trips from the device tree`
  - `rpi5-fan: 60600 mC, level 1 to 2 (duty 1019 of 2078)` → `level 2 at 3877 rpm`
  - `rpi5-fan: 54550 mC, level 2 to 1 (duty 611 of 2078)` → `level 1 at 2352 rpm`（55 °C = 60 − 5 の hysteresis で下がった）
  - `sysctl hw.thermal`: `name=bcm2712 cpu-thermal kind=zone cpu=1 temperature_mc=52350 … critical_mc=110000`
- 未実施: 温度の firmware（vcgencmd 相当の mailbox）との比較（値は 52〜61 °C で妥当な範囲）、level 0（45 °C 未満）と level 3・4（67.5・75 °C）への変化、温度が読めない時の最大への fallback。

## 残り

- ファンの rpm・level を sysctl に出すか（今は log だけ）。
- critical（110 °C）で電源を切る処理は無い（level 4 を保つだけ）。
