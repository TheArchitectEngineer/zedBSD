<!-- awesome-plan project=zedbsd record=ws278-p007 -->

# ws278-p007 Pi 5 の Bluetooth（uarta）

- 親: [ws278](../ws.md)、Queue: q1543 / q1543-i02（P3、2026-10-21）
- Status: in-progress（実装と build まで。実機の確認は WS278 の最後の一括の試験）

## 目的と受け入れの条件

Pi 5 の内蔵 Bluetooth（CYW43455 の HCI、BCM2712 の uarta）を HCI の class の `bluetooth0` にし、bluetoothd が patch（`BCM4345C0.hcd`）を当てて使えるようにする。

- build（warning 0）。
- 実機（一括の試験）: `rpi5-bt: UART at 107d50c000, irq 308, clock 96000000 Hz, speed up to 3000000 baud`、`rpi5-bt: UART and BT_ON pins routed`、`rpi5-bt: controller ready after about … ms`、`rpi5-bt: in step …`、bluetoothd の patch の後に `bt` で scan、device が見える。

## 設計

- `src/drivers/platform/rpi5/rpi5-bt.c`: `rpi4-bt.c`（PL011）を写して書き直した（ユーザーの「写して書き直す」）。worker・hunt・送信の ring・transition・class との繋ぎは同じ。UART の層を 16550 互換（`brcm,bcm7271-uart`、reg-shift 2 = 32 bit の register、clock 96 MHz）に書き直した: 割り込みは IER の受信（timeout を含む）と送信 FIFO の空、受信は LSR の DR の間 RBR を読み、LSR の誤り（framing・parity・break・overrun）を ring の entry の誤り bit（PL011 の DR と同じ並び）にする、送信は THRE で最大 16 byte、速度は DLAB で divisor（= clock / (16 × baud) を丸め、誤差 1.5 % まで。3 Mbaud = 2、115200 = 52）、MCR の RTS・DTR・OUT2・AFE（自動の RTS/CTS）、CTS の待ちは MSR、送信の終わりは LSR の TEMT。
- device tree: `brcm,bcm43438-bt` の親の uarta（reg・interrupts（SPI 276 → 308、level high）・clock-frequency）、`max-speed`、`local-bd-address`、`shutdown-gpios`（gio の pin 29、flag 0 = active high）。pin は uarta の pinctrl-0（uarta_24_pins の uart0 と pull、bt_shutdown_pins の GPIO 29 を gpio）を `rpi5-pinctrl.c` で当てる。電源は `rpi5-gio.c`。
- Pi 4 にあった mailbox の clock の読み・設定、QEMU の判定、serial console との共用の確かめ（Pi 5 の debug UART は uart10 で別）、GPIO 14/15 の開放は無い。
- hunt・history・link の共通の部分は `rpi4-bt-link.c` をそのまま使う（PL011 の divisor の関数は使わない）。HCI の class（`bt-hci.c`・`bt-hci-proto.c`）を rpi5 の board でも build する（vmunix.mk の条件に rpi5 を足した、rpi4 の条件は同じ）。
- patch: bluetoothd は local name `BCM4345C0` で `/lib/firmware/brcm/BCM4345C0.hcd` を選ぶ。Pi 5 も同じ chip なので `brcmbt-firmware` の package をそのまま使う（`config-rpi5.mk` の USER_PROGRAMS に足した）。
- source の comment から開発の管理情報（BUG・試験の回の番号）を落とした（写した時に Guardrail の規則を当てた）。出典: 16550 の register map、Zephyr の h4_ifx_cyw43xxx.c（電源の時間、Apache-2.0）。

## 変えた所

- 新: `src/drivers/platform/rpi5/rpi5-bt.c/h`。
- `src/kern/platform/rpi5.c`（refresh で BT の probe）、`platform/arm64/vmunix.mk`（rpi5 の source、HCI の class の条件）、`config/ci/config-rpi5.mk`（brcmbt-firmware）。

## 確認（2026-10-21 P3、host）

- rpi5 kernel build: rc=0、warning 0、vmunix check PASS。
- 未実施: host の試験（UART の層は register の手順だけで、host の偽物で確かめる意味が薄い）、実機。

## 危険

- R18: bcm7271 の UART の FIFO の大きさ・AFE の扱いが 16550A と違えば、送受信の取りこぼしや CTS の待ちの失敗。log の `rpi5-bt: the line at … LCR …, MCR …, LSR …, MSR …` で register を見る。
- R19: firmware の uarta の設定（dts の `skip-init`）を上書きする。driver は自分で divisor を設定するので影響しない見込み。

## 1 回目の実機の結果と対処（2026-10-22 P3）

- 実機: `rpi5-bt: controller's CTS did not rise in 500 ms`、`rpi5-bt: controller did not come up (42)`。
- 2026-10-22 ユーザー「BTはPi4でも動作していないため、いったんドライバを無効にしてください。その上で、電源を解決しましょう。」→ `CONFIG_DRIVER_RPI5_BT`（Makefile の既定 n、`config-rpi5.mk` で n）を足し、n の時は `rpi5-bt.c`・`rpi4-bt-link.c`・HCI の class を build せず、`rpi5.c` は probe を呼ばない。code は残す。Pi 4 の `config-rpi4.mk` は前から `CONFIG_DRIVER_BCM2711_BT := n`。CTS の調べは今回しない（Wi-Fi の電源の後）。
- Status: in-progress のまま（driver は無効、再開は Wi-Fi の chip の電源が解けてから）。
