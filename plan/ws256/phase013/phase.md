<!-- awesome-plan project=zedbsd record=ws256-p013 -->

# ws256-p013 VF2 の周辺の基盤（code、板の前）

Status: in-progress（2026-10-19 P2: code・host 試験・build（warning 0）まで済み。Q1 の判定待ち。実機の確かめは p022）

- 親: [WS256](../ws.md)。Queue: q1285-i01（P2）。
- 範囲: [design-peripherals.md](../design-peripherals.md) §3.2（改訂 4）と §2.4 の `drv_fdt_dma_coherent`。`src/drivers/platform/visionfive2/` の CRG（SYS・STG・AON、PLL から rate）・reset・pinctrl/GPIO・SYSCON、汎用の `drv_fdt_dma_coherent(fdt, node, board_default)`（親を辿る）、`visionfive2.c` からの初期化。PMIC（AXP15060）と DW I2C は外す（#22 の既定）。HAL（hal.h）は変えない。
- 完了の条件: host 試験（CRG の rate の計算、pinctrl の register の値、DT の解釈）が合格、build が warning 0（VF2・virt、fdt.c を変えるので arm64（rpi4）・amd64 も）、p022 で見る項目をこの文書に書く。Q1 が合格とする。
- 追加の範囲（2026-10-19、Q1 の依頼）: T1-729 の C で VF2 の root に `mkdir` が無く `make-homes` が失敗した件。`config/ci/config-visionfive2.mk` に基本の program を足し、build warning 0 を確かめる。

## 実装（2026-10-19、P2、commit 8333b36f0・03c0a9185・e9b58301f）

| file | 中身 |
| --- | --- |
| `src/drivers/generic/fdt.c`・`include/drivers/generic/fdt.h` | `drv_fdt_dma_coherent(fdt, node, board_default)`: node から根へ `dma-coherent` → 1、`dma-noncoherent` → 0、根まで無ければ board の既定、読めない tree は 0（非 coherent の側に倒す）。pinctrl・CRG が使う `drv_fdt_child`（直の子の列挙）と `drv_fdt_string_index`（`clock-names` などの位置） |
| `vf2-syscon.c/.h` | SYS・STG・AON の SYSCON を compatible で探して写す。read は lock 無し、update は mask の read-modify-write（変わる時だけ書く）。`starfive,sysreg`・`starfive,syscon`・`starfive,stg-syscon` の形（phandle と引数）の解釈 |
| `vf2-crg.c/.h` | 3 つの CRG。clock の表（下）にある clock だけを扱う。enable は今の mux の道を PLL・入力まで辿って閉じた gate だけを開く（冪等）。disable は自分の gate だけ、共有の bus の clock は EBUSY。rate は SYS SYSCON の PLL0〜2（読むだけ）から mux・分周を辿って計算。set_rate は SDCARD だけ（要求を超えない最小の分周、最大の分周でも超える時 ERANGE）。`assigned-clock-rates` の適用。reset は assert の bit を動かし status が追うのを待つ（来なければ ETIMEDOUT） |
| `vf2-pinctrl.c/.h` | SYS（64 pin）・AON（4 pin）の IOMUX。`pinctrl-0` の group（子の node の `pinmux` の GPIOMUX と bias・drive-strength・input・schmitt・slew）を、先に全部を検査してから、field ごとに読んで違う時だけ書く（書いた field の数を返す）。`*-gpios` の解釈、input・output・read・write。GPIO の割り込みは使わない |
| `src/kern/platform/visionfive2.c` | `kern_platform_init` で FDT を開き SYSCON → CRG → pinctrl の順に初期化。boot log に PLL0〜2・AXI_CFG0・SDCARD の rate、SD の card detect（GPIO 41 を input にして 1 回読む）を出す。`VF2_DMA_DEFAULT_COHERENT` 1（後の glue が `drv_fdt_dma_coherent` に渡す） |
| `platform/riscv64/vmunix.mk` | vf2 の object に上の 3 file（vf2 の行の comment から開発の番号を外した） |
| `config/ci/config-visionfive2.mk` | 追加の範囲: `ZEDBSD_USER_PROGRAMS` に `chmod chown cp dmesg id kill ln mkdir mv ps pwd rm rmdir sleep sync touch`（全部 basic・platform `*`） |

clock の表（書き換えできるのはこれだけ。VOUT・PD_VOUT・CPU・DDR・UART0 の clock は表に無いので触れない）:

| 生成器 | 番号 | clock | 種類 | 源 |
| --- | --- | --- | --- | --- |
| SYS | 5 | bus_root | mux、共有 | osc / PLL2 |
| SYS | 7・8・11 | axi_cfg0・stg_axiahb・apb_bus | 分周、共有 | 5 → 7 → 8 → 11 |
| SYS | 9・12 | ahb0・apb0 | gate、共有 | 8・11 |
| SYS | 92・94 | SDIO1 AHB・SDCARD | gate（94 は分周 2〜15、set_rate 可） | 9・7 |
| SYS | 96 | NOC_BUS_STG_AXI | gate、共有（PCIe 2 本） | 未知 |
| SYS | 108・109・111 | GMAC0 GTXCLK（分周 1〜15）・PTP（分周 1〜31）・GTXC（遅延線、108 を通す） | gate | PLL0・未知・108 |
| SYS | 112 | IOMUX APB | gate、共有 | 未知 |
| SYS | 157・158 | PWMDAC APB・CORE（分周 12〜256） | gate | 未知（p019 が足す） |
| STG | 8〜13・27・28 | PCIe0/1 の AXI MST0・APB・TL、DMA1P AXI・AHB | gate | 未知 |
| AON | 2・3 | GMAC0 AHB・AXI | gate | 未知・SYS 8 |
| AON | 4・5・6 | GMAC0 RMII RTX（分周 1〜30）・TX（mux）・TX_INV（反転、5 を通す） | — | 入力 gmac0_rmii_refin・SYS 108 / AON 4・5 |

- 出典（block ごとに `Value(s) referenced:`・`Procedure referenced:`、file の先頭には書かない）: register の offset・bit・分周の範囲・reset の register・PLL の式は TRM（JH7110 TRM Preliminary V2 §2.5.6・§2.8.1〜2.8.4・§2.8.7・§2.8.8）、clock の番号・GPIOMUX の形は Linux v6.12 の binding の header（GPL-2.0 OR MIT、値）、clock の源と PLL・reset の手順は OpenBSD `stfclock.c`・`stfpinctrl.c`（ISC、手順）。GPL の手順は使っていない（design §0.2 の表で CRG・IOMUX は「不要」）。code・comment・構造・symbol の名前は写していない。
- 推測で置いた所:
  - reset の status の bit は 1 = 解除と読んだ（TRM Table 2-240・2-244 の既定: JTAG の線が assert 0・status 1）。違えば deassert が ETIMEDOUT になり、p022 で見える。
  - 源を「未知」にした clock は rate を ENOTSUP で返す（gate は動く）。必要になった Phase が源を足す（design §4.2「各 Phase は自分の行だけを足す」）。
  - pinmux の PINMUX（function 選択、bits 8〜9 が 0 でない）は ENOTSUP。VF2 の DT で p014〜p024 の device が使う group は全部 GPIOMUX。
- 書いていない物: GPIO の割り込み、PMIC・I2C、PLL の変更、使わない clock を止める処理、`/dev/vf2-clktest`（p022 は boot log の rate で足りる。要れば p022 で足す）。

## 試験（2026-10-19、P2、host と build だけ。QEMU・実機は無し）

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| host 試験 | `plan/ws256/tests/vf2-soc/run.sh build/p013-host` | exit 0、PASS 93・FAIL 0（log build/tmp/p013-host.log） |
| 既存の ECAM の host 試験（fdt.c の回帰） | `plan/ws256/tests/ecam/run.sh build/p013-host/ecam` | exit 0、`OK: 0 failure(s)` |
| VF2 の vmunix | `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p013-vf2 -j16 vmunix` | exit 0、warning・error 0（build/tmp/p013-vf2-2.log）、vf2-*.c 3 file を compile |
| VF2 の rootfs・disk image（追加の範囲） | 同じ config で `rootfs`、`disk-image` | exit 0、warning・error 0（build/tmp/p013-vf2-rootfs.log・p013-vf2-disk.log）。root の /bin に mkdir・chown など 16 個が入った |
| virt の vmunix | `make ZEDBSD_CONFIG=config/ci/config-riscv64-virt.mk BUILD=build/p013-riscv64-virt -j16 vmunix` | exit 0、warning 0 |
| arm64（rpi4）の vmunix | `make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p013-rpi4 -j16 vmunix` | exit 0、warning 0（fdt.c を compile） |
| amd64 の vmunix | `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p013-amd64 -j16 vmunix` | exit 0、warning 0（amd64 は fdt.c を build しない） |

host 試験の中身（`plan/ws256/tests/vf2-soc/`: `vf2-soc.dts` は VF2 の DT の形の木、`vf2-soc-host-test.c` は fake の register の上で実物の 4 file を動かす）:
- PLL: 整数 mode（PLL0 24 MHz × 125 / 2 = 1.5 GHz、PLL2 1188 MHz）、分数 mode（PLL1 24 MHz × 88.875 / 2 = 1066.5 MHz）、dacpd ≠ dsmpd は EIO、power down は 0。
- rate: bus_root の mux（PLL2・osc）、axi_cfg0 / 3 = 396 MHz、stg_axiahb、ahb0、SDIO1 AHB、SDCARD、apb0、GMAC0 GTX = PLL0 / 12 = 125 MHz、AON の TX の mux の 2 通り、RMII RTX = 50 MHz / 2、範囲外の selector は EIO、分周 0 は EIO、源の無い clock は ENOTSUP、表に無い clock は ENOENT。
- set_rate: SDCARD 50 MHz → 分周 8（49.5 MHz、gate は保つ）、100 MHz → 4、源より速い要求 → 最小の 2、400 kHz → ERANGE で不変、共有の bus は ENOTSUP。mmc1 の `assigned-clock-rates`。
- gate: SDCARD の enable は自分の gate だけ、SDIO1 AHB の enable は上の AHB0 も開く、2 回目は書かない、TX_INV は AON TX と SYS GTX を開き反転の bit を保つ、disable は自分だけ、共有の AHB0・NOC は EBUSY、CPU root は ENOENT で不変。
- DT: mmc1・gmac0・pcie0 の clock と reset の名前の解決、PLL の node を指す clock は ENODEV、SYSCON の 3 つの property。
- reset: SYS 65（0x300 の bit 1）、STG 15、AON 1、status が追わない時 ETIMEDOUT、範囲外 ERANGE。
- pinctrl: mmc1 の 6 pin の DOUT・DOEN・GPI（pin + 2）・pad（pull-up・12 mA・input・schmitt・slew）の値、2 回目は 0 field、pcie0 の clkreq、6 mA は書く前に EINVAL。GPIO 41 の card detect（input、active low の読み）、PERST 26（先に level、隣の pin は不変）、範囲外の pin、AON の pin。
- `drv_fdt_dma_coherent` の 6 通り: 親の dma-coherent、node の dma-noncoherent、親の dma-noncoherent、node の dma-coherent、根まで無く既定 1、既定 0。

## p022 で見る項目（実機、ユーザーが framebuffer を目で見る: 2026-10-19 の決定 #10・#15）

boot の画面（kernel の log）に次の行が出る。値を U-Boot の `md.l` の値から計算した期待と照合する。

1. `visionfive2: pll0 … Hz (0), pll1 … Hz (0), pll2 … Hz (0)` — U-Boot で `md.l 0x13030018 8`（SYS SYSCON 0x18〜0x34）を読み、PLL の式（TRM §2.5.6: osc 24 MHz × (fbdiv + frac/2^24) / prediv / 2^postdiv1、dacpd = dsmpd = 1 なら frac 無し）で計算した値と一致する。期待の目安は PLL0 1.5 GHz か 1 GHz（firmware 次第）、PLL2 1188 MHz。括弧の中が 0 でない時はその errno。
2. `visionfive2: axi_cfg0 … Hz (0), sdcard … Hz (0)` — `md.l 0x13020014 3`（bus_root 0x14・axi_cfg0 0x1c）と `md.l 0x13020178 1`（SDCARD）の分周から計算した値と一致する（PLL2 1188 MHz・分周 3 なら 396 MHz、SDCARD は U-Boot が 50 MHz 近くにしていれば分周 8 で 49.5 MHz）。
3. `visionfive2: SD card present (gpio 41)` — SD から起動した時に present。挿抜の確かめは SD 以外（NVMe 等）から起動できる時に、抜いて起動して absent。
4. 出てはいけない行: `system control blocks unavailable`・`clock generators unavailable`・`pin controllers unavailable`・`no device tree`（どれかが出たら DT の compatible か reg の読み違い）。
5. 画面（GOP の framebuffer）が起動の後も消えない（VOUT の clock・PD_VOUT に触れていない事の確かめ）。
6. PERST（GPIO 26・28）は p013 では動かさない。出力の確かめは p020 の最初（PERST を assert → 解除して link が上がる）に回す。

## 見つけた事（範囲の外）

- T1-729 の C の `homes: could not make /home/kei` は `mkdir`・`chown` が root に無いため（`make-homes` が使う）。**`config/ci/config-riscv64-virt.mk` と `plan/ws256/tests/config-riscv64-virt*.mk` の `ZEDBSD_USER_PROGRAMS` も VF2 と同じ `cat echo login ls sh uname` なので、virt の root でも同じ失敗になるはず**（未確認）。virt の config は今回変えていない。直すなら Q1 が同じ行を足す。
- 同じ走りの `net: UP: No such file or directory`・`oneshot networking failed` は program の不足ではないと見る（推測、走りは見ていない）: `UP` の要求を出すのは `net startup` の loopback（`lo`）の設定（`userland/base/net/main.c` の `configure_loopback`）で、その答えが ENOENT だったと読める。VF2 には NIC が無い（p018 の前）が、lo は kernel の `net_init` の `loopback_init` が作るはずなので、riscv64 の kernel で network の core が起動していない（または networkd が lo を見つけない）可能性がある。原因は未調査。virt でも出るかを T1 の次の走りで見ると切り分けられる。

## 残り

- Q1 の判定と merge。
- p022（実機、#15・p005 の後）: 上の 1〜5。
- p014・p018・p019・p020 が自分の clock の源と行を表に足す（PWMDAC の源、GMAC0 PTP、STG の rate が要る時）。
