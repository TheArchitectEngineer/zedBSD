<!-- awesome-plan project=zedbsd record=ws207-p002 -->

# ws207-p002: BCM2711 の DMA と PWM の音（3.5 mm の jack）

Parent: [WS207](../ws.md)
Status: in-progress（q933、P1。実装と host の試験・build まで。Pi 4 の実機の確かめは Q1 経由で T1 に依頼する）
Phase disposition: normal
Queue: q933（[P1 の lane](../../agents/P1/queue.md)）

## 由来と範囲

[p001](../phase001/phase.md) の P1（再生は 3.5 mm の PWM から）。F1 は推しで始めてよいと Q1（2026-10-11、「F1 が違えば止める」）。
範囲: Pi 4 の headphone jack を `/dev/dsp` の再生の backend にする。録音（p003、UAC1）と HDMI（p004）は範囲の外。

## 設計

| 所 | 決め |
| --- | --- |
| 出口 | PWM1（DT の `pwm@7e20c800`、ARM 0xfe20c800）の 2 channel を FIFO から（`USEF1`・`USEF2`、PWM の algorithm、`MSEN` 0）。GPIO 40・41 を ALT0（GPFSEL4 の 2 つの field だけ書く） |
| clock | clock manager（DT の `brcm,bcm2711-cprman`）の PWM の clock を oscillator（DT の `clk-osc`、54 MHz）÷1 で。48 kHz なら range 1125（ちょうど）、44.1 kHz なら 1224（44,117.6 Hz、+0.04%）。firmware の mailbox は使わない |
| DMA | legacy の DMA（`brcm,bcm2835-dma`）の channel を `brcm,dma-channel-mask` から 4・5・6・2・0 の順で選ぶ（Pi 4 の DT では 4、SPI 0x54 = irq 116）。control block の輪（1 block = framework の 1 fragment、`INTEN`・`WAIT_RESP`・`DEST_DREQ`・`SRC_INC`・PERMAP 1 = PWM1）、宛先は FIF1 の bus address（DT の reg の値 0x7e20c800 + 0x18）。DMA の memory は bus の窓 0xc0000000 + 物理（1 GiB まで、`drv_dma_device_create_window`）、uncached |
| 変換 | framework の ring は 16 bit stereo で DMA は使えないので、duty word の ring を別に持つ（fragment 4096 byte → block 8192 byte）。割り込みごとに DMA が入った block と次の block を変換し直す（次の割り込みでもう一度変換するので、書き込みからの遅れは割り込みの遅れ程度）。duty = (sample + 32768) × range >> 16 |
| 位置と割り込み | position = (DMA の SOURCE_AD − duty の ring) / 2（sample の byte）。割り込みでは SOURCE_AD から今の block を求め、前回からの block の数だけ `drv_audio_interrupt` を呼ぶ（割り込みの取りこぼしで framework の数がずれない） |
| channel の順 | channel 1（GPIO 40）を右とした（`AUDIO_RIGHT_FIRST`、B+・Pi 3 の回路の GPIO 40 = 右からの推定）。**実機で未確認**: 左だけの音で確かめ、逆なら定数を 0 に |
| 音量 | hardware の音量は無い（`set_volume` 無し → ENOTSUP）。audiod が sample を拡大縮小する |
| 停止 | lock の下で running を 0 にし DMA を pause・reset（ACTIVE が落ちるまで最大 1000 × 10 µs）、次に PWM の DMAC と CTL を 0。DMA は driver の duty の ring だけを読み、割り込みは running が 0 なら framework の ring を読まない |
| 構成 | `CONFIG_DRIVER_BCM2711_AUDIO`（rpi4 で既定 y、menu の Audio の行）、`KERN_AUDIO_BACKENDS` に追加。`kern_platform_init` の PCIe の後で `drv_rpi4_audio_init(fdt)` |

## 変えた file

- `src/drivers/platform/rpi4/rpi4-audio.c`・`rpi4-audio.h`（新規、driver）
- `src/drivers/platform/rpi4/rpi4-audio-ring.c`・`rpi4-audio-ring.h`（新規、hardware に触れない計算: duty、channel の順、control block の輪、fragment と position）
- `src/kern/platform/rpi4.c`、`Makefile`、`platform/arm64/vmunix.mk`、`config/drivers/architecture/arm64.drivers`、`config/drivers/menu.list`
- 試験: `plan/ws207/tests/rpi4-audio-ring-test.c`・`run-rpi4-audio-ring-test.sh`

## 確かめ（2026-10-11 P1、host のみ）

| 確かめ | 結果 |
| --- | --- |
| `sh plan/ws207/tests/run-rpi4-audio-ring-test.sh`（通常と ASan/UBSan） | `rpi4-audio-ring: all PASS` ×2（duty の端と中央・全 sample で単調、左右の順、8 block の輪の TI 0x00010149・source・next の wrap、fragment と position の端と外） |
| `make ZEDBSD_CONFIG=plan/ws203/tests/config-rpi4.mk -j16 build/arm64/vmunix` | rc 0、warning 0、`arm64 vmunix check: PASS`、`drv_rpi4_audio_init` が kernel にある |
| 同じ config で `CONFIG_DRIVER_BCM2711_AUDIO=n BUILD=build/ws207-off` | rc 0、warning 0、PASS |
| `python3 plan/tools/style-check.py`（新しい 4 file と試験） | 指摘 0（`rpi4.c` の 3 件は以前からの物） |
| Pi 4 の DT（`vendor/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb` を dtc で） | `cprman@7e101000`（clocks の最初が `clk-osc` 54,000,000 Hz）、`pwm@7e20c800`（reg 0x7e20c800 0x28）、`dma-controller@7e007000`（mask 0x7f5、interrupts の 5 番目 = SPI 0x54）、`brcm,bcm2711-gpio` を確認 |

## 未実施

- **Pi 4 の実機（T1、Q1 経由。Pi 4 は P3 と共有、kernel の入れ替え・reboot は Q1 経由）**: 依頼の案は下。
- QEMU では確かめない（QEMU の raspi4b に PWM の音は無い）。

### T1 への依頼の案

- image: rpi4 の通常の config（`plan/ws203/tests/config-rpi4.mk`、`CONFIG_DRIVER_BCM2711_AUDIO` は既定 y）の build。
- 試験: (1) dmesg に `rpi4-audio: PWM1 at fe20c800, DMA channel 4, irq 116, clock 54000000 Hz`、`/dev/dsp0` がある。(2) `audio-loop`（WS204 の道具、`userland/tests/audio-loop`）か `music` で 1 kHz の音を 10 秒: underrun 0、position が進む（audio-loop の出力）。(3) ユーザーの耳で jack から音が出る、左だけの音が左から聞こえる（UAT）。
- 合否: (1)(2) が出れば PASS、(3) はユーザーの確認。`rpi4-audio: DMA error` の log が出たら FAIL。

## 残り

- 実機の結果で channel の順（`AUDIO_RIGHT_FIRST`）を確定。
- 規約の全文の見直しは p005。
