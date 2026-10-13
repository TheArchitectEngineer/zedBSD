<!-- awesome-plan project=zedbsd record=ws206-p002a -->

# ws206-p002a: Pi 4 の GPIO の helper と mailbox の GPIO の helper

Parent: [WS206](../ws.md)
Status: in-progress（q932、P1、2026-10-13）
Phase disposition: normal
Queue: q932（[P1 の lane](../../agents/P1/queue.md)）

## 由来と承認

- 2026-10-12 ユーザー「RPi4のWiFiとBluetoothを実装し、RPi4は完了です」。2026-10-13 朝のユーザーの答え（E2・E4・E6、D1〜D4）と「実機動作OK」の後、Q1 が P1 に p002a の実行を依頼（2026-10-13）。
- 設計: [p001](../phase001/phase.md) の W3（第 3 版）と [review-3](../phase001/review-3.md) の 8(a)〜(c)・軽微 3。

## 範囲

1. `src/drivers/platform/rpi4/rpi4-gpio.c`・`.h`（新規、rpi4 で常に build）: W3 の API
   `enum drv_rpi4_gpio_function`・`enum drv_rpi4_gpio_pull`、`drv_rpi4_gpio_init`・`set_function`・`get_function`・`set_pull`・`pull_from_dt`。
   GPFSEL と pull の register の read-modify-write は 1 つの irqsave の spinlock（`LOCK_RANK_DEVICE`、leaf）の下。pin 0〜57。
   DT の `brcm,pull`（0 なし・1 down・2 up）と register の符号（00 なし・01 up・10 down）の変換は 1 箇所ずつ。init は ABSENT→PREPARING→READY。
2. `rpi4-firmware.c`・`.h` に追記: `enum drv_rpi4_firmware_gpio`（BT_ON=0・WL_ON=1）、`drv_rpi4_firmware_get_gpio`・`set_gpio`
   （tag 0x00030041・0x00038041、128+line の変換は helper の中、列挙型の外は EINVAL）、向きを読む read-only の
   `drv_rpi4_firmware_get_gpio_direction`（GET_GPIO_CONFIG 0x00030043、review-3 の 8(a)）。答えの最初の語（状態の語、0 が成功）を確かめる。
   EBUSY の再試行は入れない（`claim_buffer` が既に約 2〜4 秒待つ、review-3 軽微 3、W3 の「50 ms まで再試行」を置き換える）。
3. `kern_platform_init`（`src/kern/platform/rpi4.c`）から `drv_rpi4_firmware_init` と `drv_rpi4_gpio_init` を呼ぶ（review-3 の 8(b)）。
   どちらも register に触らない（map と buffer の確保だけ）ので QEMU raspi4b でも害が無い。失敗は log して起動を続ける。
4. host 試験 `plan/ws206/tests/gpio-host-test.c`・`.sh`。

範囲の外: 今の Pi の振る舞い（`rpi4-audio.c` の GPFSEL4、HAL の `led.c`・`uart.c`）は変えない。WL_ON・BT_ON はこの段では動かさない（使い手が無い）。
SDIO の host（p002b）、Bluetooth の UART（ws215-p003）。HAL の API・UAPI は変えない。

## 受け入れ

- host 試験が PASS: register の位置と bit（pin 0〜57 の全部と全ての function、隣の bit を変えない）、範囲の外の pin と function の EINVAL、
  pull の 2 つの変換（列挙型 → register の符号、DT の数 → 列挙型、生の数 0・4 の EINVAL）、init 前の ENODEV、
  read-modify-write の並行（pthread の 8 CPU が同じ register の別の pin を書き、更新が失われない、TSan で race の報告なし）、
  mailbox の GPIO の helper（tag、128+line、列挙型の外の EINVAL で mailbox に何も送らない、state の 0・1 以外の EINVAL、
  状態の語が 0 でない・答えが短い時の EIO、向きの 0・1 以外の EIO、mailbox 未準備の ENODEV）。
- 既存の `plan/ws206/tests/firmware-smp-host-test.sh` が変わらず PASS。
- rpi4 の vmunix の build が warning 0。amd64 の build が壊れない（この変更は arm64 の file だけ）。
- `git diff --check` が空。

## コマンド

```sh
sh plan/ws206/tests/gpio-host-test.sh <worktree>/build/ws206-p002a/gpio-host-test
sh plan/ws206/tests/firmware-smp-host-test.sh <worktree>/build/ws206-p002a/firmware-smp
make -o <worktree>/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix
```

## T1・Pi で見る点（この段では実機の確かめは無い、使い手が入る p002b・ws215-p003 で）

- QEMU raspi4b の `boot-test.sh` の起動の回帰（`kern_platform_init` の 2 つの init が QEMU で害が無いこと）。p002b の T1 の依頼に含める。
- Pi の起動の回帰と、dmesg に `rpi4-gpio`・`rpi4-firmware` の失敗の log が無いこと。
- p002b で: `drv_rpi4_firmware_get_gpio`・`get_gpio_direction` で WL_ON・BT_ON の今の状態と向き、`get_function` で GPFSEL3（pin 30〜39）の今の値、答えの長さと状態の語の形。

## 記録

（実行の後に追記）
