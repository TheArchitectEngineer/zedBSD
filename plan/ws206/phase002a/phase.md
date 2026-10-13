<!-- awesome-plan project=zedbsd record=ws206-p002a -->

# ws206-p002a: Pi 4 の GPIO の helper と mailbox の GPIO の helper

Parent: [WS206](../ws.md)
Status: in-progress（q932、P1、2026-10-13。実装・host 試験・build は済み（dc2504ff2）。clearance は T1 の QEMU raspi4b の起動の回帰の結果を Q1 が判定してから）
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

## 記録（2026-10-13 P1、agent/p1 dc2504ff2、base main 752de34d2）

- 実装: `rpi4-gpio.c`・`.h`（新規、`platform/arm64/vmunix.mk` で常に build）、`rpi4-firmware.c`・`.h` に expander の 3 つの helper、
  `src/kern/platform/rpi4.c` の `prepare_shared_blocks()`（SD の初期化の後、GENET・PCIe・audio・GPU の前に tree を開いて
  `drv_rpi4_firmware_init` と `drv_rpi4_gpio_init`、失敗は log して続ける）。PCIe と GPU の `drv_rpi4_firmware_init` の呼び出しは残した（READY なら何もしない）。
- W3 からの違い（設計の範囲の中の技術の判断）:
  - GPIO の map は DT の大きさによらず 0xf4 に固定（Pi 4 の DT は `reg = <0x7e200000 0xb4>` で pull の register に届かない、dtc で確かめた）。DT の大きさが 0x18（GPFSEL0〜5）未満なら EINVAL。
  - spinlock は static の初期化子（`audio.c` の registry と同じ形）。init の競合で `spin_init` を 2 度呼ばないため。`get_function` も同じ lock の下で読む。
  - mailbox の EBUSY の 50 ms の再試行は入れない（review-3 軽微 3）。
  - 向きの helper は `drv_rpi4_firmware_get_gpio_direction(line, enum drv_rpi4_firmware_gpio_direction *)`。列挙型は INPUT=1・OUTPUT=2（firmware の 0・1 と別の数）、firmware の 0・1 以外は EIO。
  - 答えの検査: 最初の語（firmware が GPIO の番号を 0 で上書きすれば成功）が 0 でなければ EIO。get と向きは 8 byte 未満の答え、set は 4 byte 未満の答えも EIO。
    答えの形は wiki と Linux の expander の driver の振る舞い（code は写していない）から。**Pi の実機では未確認**（p002b で確かめる）。
- コマンドと結果（host、QEMU・実機は使っていない）:
  - `sh plan/ws206/tests/gpio-host-test.sh build/ws206-p002a/gpio <main>/vendor/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb` → unit（ASan・UBSan）PASS、smp（TSan、8 thread × 4000 の変更、map 1 回）PASS。
  - 試験が誤りを捕まえることの確かめ（scratchpad の写しで、tree は変えていない）: pull の up と down の取り違え → FAIL、`update_field` の lock を外す → FAIL（lock の外の register の読み書き）、128+line を外す → FAIL。
  - `sh plan/ws206/tests/firmware-smp-host-test.sh build/ws206-p002a/firmware-smp <dtb>` → PASS（race 1 準備・16000 answer、retry 2 準備・14000 answer）。
  - `make -f plan/ws048/tests/host-test.mk` の `firmware-host-test` → 200163 checks passed。
  - `make -o <worktree>/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix` → rc 0、warning 0（3 file を touch して再 build も 0）。
  - `make -o <worktree>/build/amd64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/vmunix` → rc 0、warning 0。
  - `git diff --check` → 空。
- 未実施: QEMU raspi4b の `boot-test.sh`、Pi の起動の回帰（T1、Q1 に依頼）。expander の答えの実機の形。
- 試験の file `plan/ws206/tests/gpio-host-test.*` は WS206 の未完了の Phase（p002b・ws215-p003 の helper の回帰）から参照する。WS の終わりに Master の Tools に登録するかを Q1 が判断。
