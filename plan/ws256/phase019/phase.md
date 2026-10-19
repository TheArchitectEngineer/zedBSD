# ws256-p019 sound（3.5 mm の PWMDAC と DW AXI DMAC の cyclic、板の前の code）

Status: in-progress（q1293-i01、P1、2026-10-19。板の前の code は済み。実機の確かめ（ユーザーの耳）と §2.4 の短い coherency の確かめは未実施で、p022・p023 の後）

- 親: [WS256](../ws.md)。設計: [design-peripherals.md](../design-peripherals.md) §3.8（改訂 4）、§2.1・§2.4、§0.2（PWMDAC と DW AXI DMAC は Linux の手順の参考を許す、2026-10-19 13:39 ユーザーの決定）。
- 範囲（q1293 の依頼）: `src/drivers/dma/dw-axi-dmac.c`（mem→mem と cyclic の device への転送、p023 でも使う形）、PWMDAC の driver、audio の層への登録、VF2 の glue（clock・reset・pinctrl）、`visionfive2.c` からの attach。host 試験、build warning 0（VF2・virt）。HAL（hal.h）は変えない。QEMU は起動しない。

## 変えた物

| file | 内容 |
| --- | --- |
| `include/drivers/dma/dw-axi-dmac.h`・`src/drivers/dma/dw-axi-dmac.c`（新規） | 汎用の DW AXI DMAC。FDT の node から `interrupts`・`dma-channels`（8 まで）・`snps,data-width`・`snps,block-size`・`snps,priority`・`snps,axi-max-burst-len` を読み、`starfive,jh7110-axi-dma` なら CFG の新しい配置（request line は CFG の low の bit 11）。DMA の owner は `drv_fdt_dma_coherent(fdt, node, board_default)` の答えで作る（§2.4、層は逆にしない）。channel ごとに 64 byte の descriptor を 32 個（coherent の buffer）。API: `channel_get/put`、`copy`（同期、block の大きさで分けた鎖、共通の幅、poll で終わりを待つ、1 秒で ETIMEDOUT、error は EIO: **p023 の mem→mem はこれを使う**）、`cyclic_prepare/start/stop/position`（1 period = 1 descriptor、各 descriptor に LAST、鎖は環。割り込みで次の period から channel を enable し直し、使われた descriptor の VALID を立て直す）。stop は lock の中で running を落とし、channel の割り込みの signal を先に切ってから disable（止めた後の DONE で共有の線が上がったままにならない） |
| `src/drivers/platform/visionfive2/vf2-dmac.c/.h`（新規） | DMAC の板の glue: STG の `core-clk`・`cfgr-clk` を開き、名前の無い 2 本の reset（STG 5・6）を解除して汎用の driver に渡す。1 個だけ保持（同じ node なら同じ物を返す。p023 の試験の cdev もこれで取る） |
| `src/drivers/platform/visionfive2/vf2-pwmdac.c/.h`（新規） | PWMDAC（0x100b0000）。`dmas = <&dma 22>` の 22 は **DMAC の handshake（request line）**で、channel は DMAC が空きから渡す（Minor 1・2）。pinctrl-0（GPO 28・29 を GPIO 33・34）、apb・core の clock、reset 96 を解除、core の clock を 12.288 MHz に（8 bit の PWM で 256 cycle が 1 sample = 48 kHz）。CTRL は 0x4008（中央の duty・符号付き・停止）、start で 0x4009。audio の層に S16_LE・2 ch・48 kHz の 1 形式で登録（ring は DMAC の DMA の owner から）。frame（32 bit = L/R の 16 bit）をそのまま data register へ DMA する（変換の ring は無い）。prepare は fragment ごとに 1 period |
| `src/drivers/platform/visionfive2/vf2-crg.c/.h` | SYS の audio root（16、PLL2 ÷ 2〜8、TRM Figure 2-4・Table 2-66）を表に足し、PWMDAC core（158）の source を audio root に、SETTABLE に。`drv_vf2_reset_lookup_index`（名前の無い resets の位置で引く）を足した |
| `src/kern/platform/visionfive2.c` | `probe_soc` の PCIe の後で `drv_vf2_pwmdac_attach(&fdt, VF2_DMA_DEFAULT_COHERENT)`（失敗しても起動は続く） |
| `platform/riscv64/vmunix.mk`・`Makefile` | VF2 の source に dw-axi-dmac.c・vf2-dmac.c・vf2-pwmdac.c。`KERN_AUDIO_BACKENDS` に「board が visionfive2 なら y」を足し、riscv64 の既存の `$(KERN_AUDIO_SOURCES)` で audio.c が入る（新しい CONFIG は作らない） |
| `plan/ws256/tests/vf2-audio/`（新規） | host 試験（下）、`make-tone.py` と `tone-480hz.raw`（実機の確かめの音） |
| `plan/ws256/tests/vf2-soc/vf2-soc-host-test.c` | 158 の rate の期待を ENOTSUP から「PLL2 ÷ 2 ÷ 12 = 49.5 MHz」に直した（audio root を表に足したため） |

出典（block ごとに Procedure/Values referenced: を書いた。code・comment・構造・symbol の名前は写していない）: Linux v6.12 `drivers/dma/dw-axi-dmac/dw-axi-dmac.h`（5ae1dd41…）・`dw-axi-dmac-platform.c`（7cf548a4…）、`sound/soc/starfive/jh7110_pwmdac.c`（320929b0…）、`drivers/clk/starfive/clk-starfive-jh7110-sys.c`（541500ab…、audio root と PWMDAC core の source の値だけ）、JH7110 TRM（Figure 2-4、Table 2-66・2-208）。取得物は P1 の worktree の `build/tmp/ws256-docs/linux-p019/`（git に入れない）。

Linux と違えた所（推測を含む）:
- AXI の burst length の上限は `snps,axi-max-burst-len`（16）から 1 を引いて書く（AXI の len は beat 数 − 1）。Linux v6.12 は値をそのまま書く。
- cyclic の位置は「今の period の先頭」（period の粒度）。audio の層は割り込みの数で floor を持つので足りる。
- 割り込みは running の channel だけを見る（copy の poll の status を割り込みが消さないため）。

## 確かめ

| 何 | コマンド | 結果 |
| --- | --- | --- |
| host 試験（新規） | `plan/ws256/tests/vf2-audio/run.sh build/tmp/p019-audio` | **ALL PASS（58 項目、FAIL 0）** |
| vf2-soc の回帰 | `plan/ws256/tests/vf2-soc/run.sh build/tmp/p019-soc` | PASS 94、FAIL 0 |
| VF2 の vmunix | `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p019-vf2 -j16 vmunix` | exit 0、`warning:`・`error:` 0（build/tmp/p019-vf2-2.log）。dw-axi-dmac.o・vf2-dmac.o・vf2-pwmdac.o・audio.o を compile |
| virt の vmunix | `make ZEDBSD_CONFIG=config/ci/config-riscv64-virt.mk BUILD=build/p019-virt -j8 vmunix` | exit 0、`warning:`・`error:` 0（build/tmp/p019-virt.log） |
| rpi4・amd64 の build | 未実施（Makefile の変更は board が visionfive2 の時だけ y を足す式で、他の board では `n` が並ぶだけ） | — |

host 試験の中身（`vf2-audio-host-test.c`: fake の register の model。channel を enable すると次の step で descriptor の鎖を辿り、VALID を見て、memory へ copy するか converter の data register への word の log に積み、VALID を落として書き戻し、link を辿り、LAST で channel を止めて DONE を立て、signal があれば登録された handler を呼ぶ。DMA の address は host の pointer）:
- bring-up: DMAC の clock 27・28 と reset 5・6、CFG = 3（enable と割り込み）、firmware が残した channel 2 を止める、irq 73、DMA の owner は 1 個で板の既定（coherent）。PWMDAC の clock 157・158、reset 96、core の divider 50（PLL2 1228.8 MHz ÷ 2 ÷ 50 = 12.288 MHz、47999〜48000 sample/s）、pin の dout（GPIO 33 = 28、34 = 29）。
- audio: 1 形式（S16_LE・2・48000）、44.1 kHz は EINVAL、capture は ENOTSUP、512 KiB の period は E2BIG。CFG low = 0xF | 22 << 11、high = 1（memory→device、hardware の handshake、priority 0）。8 個の descriptor（各 period の source、data register、1024 beat、環の link、control 0x45240 / 0xC00F87C0）。20 period（2.5 周）で位置が period ごとに進み、framework の割り込み 20 回、EOI 20 回、converter に 20 × 1024 word が ring の順で届く。stop で CTRL 0x4008・channel disable・signal off、その後は何も動かない。再 start は最初の period から（VALID の立て直し）。
- copy: 1 MiB（8 byte の beat、2 block）、奇数の 100003 byte（1 byte の beat、端を壊さない）、33 block は E2BIG、CFG high = priority 1 << 20（新しい配置）。

## 実機の確かめ（未実施、p022・p023 の後。判定は #24 の形、ユーザーの耳）

前提: p022（clock・reset の実機の確かめ）と p023（coherency の確かめ）の後。§2.4 の「短い版」（DMAC の cyclic の 1 周で src→dst）は p023 の試験の cdev が mem→mem を持った後に足す（この Phase の code には入れていない: 未実施）。

1. VF2 の image に `tone-480hz.raw` を `/tone.raw` として入れる（試験の config の `--file /tone.raw=plan/ws256/tests/vf2-audio/tone-480hz.raw`）。
2. 起動の画面（kernel の log）に次の行が出る:
   - `dw-axi-dmac: 16050000, 4 channels, irq 73, coherent`
   - `vf2-pwmdac: sample clock F Hz, R samples/s` — PLL2 が 1228.8 MHz なら F = 12288000 前後、R = 48000 前後。U-Boot が PLL2 を 1188 MHz にしていれば F = 12122448、R = 47353（約 1.4 % 低い音で鳴る。記録して Q1 へ）。
   - `vf2-pwmdac: 100b0000, DMA request 22`
   - 出てはいけない行: `visionfive2: sound unavailable (E)`、`vf2-pwmdac: no DMA controller`、`vf2-dmac: clock … stays off`・`reset … stays asserted`、`vf2-pwmdac: pins not routed`・`sample clock not set`、`dw-axi-dmac: channel N stays busy`。
3. 3.5 mm の jack にイヤホンか小さいスピーカーを繋ぎ（音量は小さめから）、root の shell で:
   - `cat /tone.raw /tone.raw /tone.raw /tone.raw /tone.raw /tone.raw /tone.raw /tone.raw /tone.raw /tone.raw > /dev/dsp0`（約 1 秒の 480 Hz の正弦波。引数の数を増やせば長くなる）
   - file を入れない時の代わり: `cat /dev/urandom > /dev/dsp0`（白色雑音、大きいので音量に注意、Ctrl-C で止める）。雑音は coherency の不具合でも鳴るので、判定は tone で行う。
4. 判定: 480 Hz の澄んだ音が左右から途切れずに鳴れば合格。無音・ぶつ切れ・雑音が混じる時は、症状と log の行と `dmesg` の `dw-axi-dmac: channel 0 error …` の有無を記録する（period の境目ごとの小さな click は、LAST ごとに channel を enable し直す方式の限界の可能性: 21 ms ごと）。
5. 未確認: `/dev/dsp0` の権限（root で打つ）、audiod を通す道（VF2 の image に audiod は入っていない）。

## 残り

- 実機の確かめ（上）。§2.4 の短い coherency の確かめ（p023 の cdev の後）。
- 規約の全文の見直しは WS の最後の Phase。
