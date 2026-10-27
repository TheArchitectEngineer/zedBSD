<!-- awesome-plan project=zedbsd record=ws304-design -->

# WS304 の設計: Raspberry Pi 5 の pcie1（M.2 HAT）と NVMe

- 版: 2（2026-10-27、P3、q1618-i01）。版 1 に design-reviewer の指摘（H1・M1〜M7・L1〜L11）を反映した（§10）。
- 範囲: BCM2712 の外の PCIe x1（pcie1、`pcie@1000110000`）の root complex を起こし、その下の NVMe を既存の `pci-nvme` で使う。MSI は MIP1。root の file system は SD のまま（NVMe からの起動は範囲の外）。
- 実機は p003（新しい SwitchBot の後）。p001・p002 は host 試験と build まで。

## 1. 出典の扱い

Guardrail の「MIT・GPL の出典のデータの引用」に従う。値は block ごとに `Value(s) referenced:`、手順は `Procedure referenced:` を複数行の comment で書き、code・comment・構造・symbol の名前は写さない。file の先頭には出典を書かない。読んだ物（2026-10-27 に取得、scratchpad に置き commit しない）:

| 出典 | 使う物 |
| --- | --- |
| Raspberry Pi Linux rpi-6.12.y `drivers/pci/controller/pcie-brcmstb.c`（GPL-2.0+） | BCM2712 の register の offset と bit、start の手順（bridge の reset、SerDes、MISC_CTRL、inbound の窓と UBUS の remap、post_setup の MDIO と UBUS・AXI の値、CLKREQ#、PERST#、link の待ち、clkreq-mode "safe"） |
| 同 `drivers/reset/reset-brcmstb.c`（GPL-2.0） | SW_INIT の bank（0x18 byte）・SET 0x0・CLEAR 0x4・STATUS 0x8、deassert の後の 100〜200 µs |
| 同 `drivers/reset/reset-brcmstb-rescal.c`（GPL-2.0） | rescal の START 0x0 bit 0・STATUS 0x8 bit 0、100 µs ごと 1 ms までの待ち |
| 同 `drivers/irqchip/irq-bcm2712-mip.c`（GPL-2.0） と OpenBSD `sys/arch/arm64/dev/bcm2712_mip.c` 1.1（ISC） | MIP の register、data = `brcm,msi-offset` + 番号、GIC SPI = msi-ranges の先頭 + data、host の edge と mask の初期化 |
| zedBSD-firmware `rpi5-boot/` の `bcm2712-rpi-5-b.dtb`・`bcm2712d0-rpi-5-b.dtb`（実行時に読む） | pcie1・mip1・`bcm_reset`・`pcie_rescal` の node |
| PCI Express Base 4.0 §6.6.1 | PERST# の後 100 ms |

## 2. dtb の事実（C1・D0 で同じ）

- `pcie@1000110000`: `compatible = "brcm,bcm2712-pcie"`、`reg = <0x10 0x110000 0x0 0x9310>`、`linux,pci-domain = 1`、`max-link-speed = 2`、`num-lanes = 1`、`status = "disabled"`（firmware が `dtparam=pciex1`（または `nvme`）で `okay` にする）。
- `ranges`: 32 bit の非 prefetch: PCI 0x0_8000_0000 → CPU 0x1b_8000_0000、2 GiB。64 bit の prefetch: PCI 0x4_0000_0000 → CPU 0x18_0000_0000、14 GiB。
- `dma-ranges`: (1) PCI 0x10_0000_0000 → CPU 0、64 GiB（system memory）。(2) PCI 0xff_ffff_f000 → CPU 0x10_0013_1000、4 KiB（MIP1 の register）。
- `interrupt-map`: INTA〜INTD → GIC SPI 219〜222（level high）。`interrupts` の "pcie" 223・"msi" 224 は使わない。
- `resets = <&pcie_rescal>, <&bcm_reset 43>`、`reset-names = "rescal", "bridge"`。`bcm_reset` は `brcm,brcmstb-reset`、CPU 0x10_0150_4318、0x30 byte。`pcie_rescal` は `brcm,bcm7216-pcie-sata-rescal`、CPU 0x10_0011_9500、0x10 byte（pcie0〜2 で共有）。
- `msi-parent = <&mip1>`。mip1: `reg` (1) CPU 0x10_0013_1000 の 0xc0 byte、(2) 0xff_ffff_f000（PCI 側の書き込み先、親の ranges の外なので訳さずに読む）。`msi-ranges = <&gicv2 GIC_SPI 247 EDGE 8>`、`brcm,msi-offset = 8`。
- `brcm,clkreq-mode = "safe"`、`brcm,fifo-qos-map = [3 3 3 3]`。`dma-coherent` は無い（非一貫）。clock・regulator の property は無い。
- pcie2（RP1）は別 node（子に RP1 の simple-bus の interrupt-controller）。pcie0 は disabled、`msi-parent` は自分。

## 3. BCM2711（Pi 4、今の pci-brcmstb.c）との差

| 項目 | BCM2711 | BCM2712 |
| --- | --- | --- |
| bridge の reset | 中の RGR1_SW_INIT_1（0x9210）bit 1 | 外の brcmstb-reset の 1 本（board が行う） |
| PERST# | 0x9210 bit 0（1 で assert） | MISC_PCIE_CTRL 0x4064 bit 2 PERSTB（0 で assert） |
| HARD_DEBUG / INTR2_CPU | 0x4204 / 0x4300 | 0x4304 / 0x4400 |
| rescal | 無し | 共有の rescal（board が行う、§5） |
| MISC_CTRL の burst | 0（128 byte）、SCB0 の大きさを書く | 2（512 byte）、RCB_MPS_MODE（0x400）・RCB_64B_MODE（0x80）、SCB の大きさは書かない |
| inbound | BAR2 1 つ（CPU 0 固定）、BAR1・BAR3 を閉じる | dma-ranges の 1 項目ごとに BAR1 から: CONFIG_LO/HI（0x402c + 8(n-1)）と UBUS remap LO/HI（0x40ac + 8(n-1)、下位 12 bit を落とし bit 0 = 有効） |
| 作った後 | — | post_setup（§4.2） |
| CLKREQ# | link の後に HARD_DEBUG bit 1 を立てる | link の前に CLKREQ 関係の 4 bit を落とし、link の後に "safe"（REFCLK_OVRD_OUT・REFCLK_OVRD_ENABLE、ROOT_CAP の L1SS mode を 2） |
| 世代 | 触らない | `max-link-speed` を PRIV1_LINK_CAPABILITY（0x4dc）と LNKCTL2（0xac + 0x30）の下位 4 bit に |
| DMA | 31 bit、2 GiB の上限 | PCI = 物理 + 0x10_0000_0000、64 GiB まで、address_bits 64 |
| MSI | 無し（INTx だけ） | MIP1（§6）、使えなければ INTx |
| segment | 0 | `linux,pci-domain`（1）。RP1 の USB が segment 0 bus 0 を使っているので 0 は拒む |
| register の span の確かめ | 0x9214 以上 | 0x9004 以上（EXT_CFG_INDEX まで） |

共通: EXT_CFG の index（0x9000、bus<<20 | dev<<15 | fn<<12）と data（0x8000）、MISC_PCIE_STATUS（0x4068、PHYLINKUP bit 4・DL_ACTIVE bit 5・RC mode bit 7）、REVISION（0x406c）、outbound の窓 0（0x400c/0x4010、0x4070、0x4080/0x4084）、ID_VAL3 の class（0x43c）、VENDOR_REG1 の endian（0x188）、RC の link status（0xac + 0x12）、BAR の割り当て、root port の窓。

## 4. pci-brcmstb.c の変え方

判断 D6（推し: A）: (A) 今の file に variant（`DRV_PCI_BRCMSTB_BCM2711`・`_BCM2712`）を足し、差の所だけを分ける。(B) 新しい file。A を推す: config space・BAR の割り当て・outbound・root port・map_bar・INTx は同じで、写しは保守の負担になる。Pi 4 の path は register の列を変えない（WS048 の host 試験をそのまま通す、§8）。

### 4.1 header（`include/drivers/pci/pci-brcmstb.h`）の追加

- `enum drv_pci_brcmstb_variant`（0 = BCM2711 なので、今の memset で 0 にする Pi 4 の呼び出しは不変）。
- `struct drv_pci_brcmstb_config` に: `variant`、`segment`、`max_link_speed`（0 = 触らない）、`tperst_ms`（`brcm,tperst-clk-ms`、0 = 無し）、`qos_valid` と `qos[4]`（`brcm,fifo-qos-map`）、`inbound_count` と `inbound[DRV_PCI_BRCMSTB_INBOUND_MAX = 3]`（cpu・pci・size。RC BAR1〜BAR3 だけを使う。BAR4 以後は 0x40d4・0x410c の別の列で、4 つ以上の dma-ranges は describe が ERANGE で拒む）、`coherent`、`board`（下の表への pointer、Pi 4 は NULL）。clkreq は常に "safe"（D3）なので field を持たない。
- `struct drv_pci_brcmstb_board`（board が持つ操作、全部 thread の文脈）:
  - `context`
  - `bridge_reset(context, asserted)`: 0 か errno。deassert の後の 100〜200 µs の待ちは board の側（reset controller の性質）。
  - `msi_establish(context, run, argument, &handle, &address, &data)`・`msi_disestablish(context, handle)`: NULL なら MSI を出さない。
- `drv_pci_brcmstb_describe()` は compatible で variant を決める（`brcm,bcm2712-pcie` → 2712、それ以外は今のまま 2711）。2712 の時だけ: dma-ranges の全部の項目、`max-link-speed`、`brcm,clkreq-mode`、`brcm,fifo-qos-map`、`dma-coherent`。segment と board は platform が describe の後に入れる。2711 の時の読み方は今と同じ。

### 4.2 start の手順（2712）

Procedure referenced: Raspberry Pi Linux rpi-6.12.y pcie-brcmstb.c（brcm_pcie_probe・brcm_pcie_setup・brcm_pcie_post_setup_bcm2712・brcm_pcie_start_link・brcm_config_clkreq）。

1. `board->bridge_reset(1)`、100〜200 µs、`board->bridge_reset(0)`（board が 100〜200 µs 待つ）。
2. PERST# を assert（PERSTB = 0）。Linux は 2712 では明に書かないが、bridge の reset の既定と同じ値で、firmware が残した状態に依らない。
3. SerDes の電源: HARD_DEBUG（0x4304）の IDDQ（bit 27）を落とし、100〜200 µs。
4. REVISION を log。
5. MISC_CTRL（0x4008）: SCB_ACCESS_EN（bit 12）・CFG_READ_UR_MODE（bit 13）・burst = 2（bit 20〜21）・RCB_MPS_MODE・RCB_64B_MODE。
6. inbound の窓を dma-ranges の順に BAR1 から（size の code は 4〜32 KiB が 0x1c〜0x1f、64 KiB〜64 GiB が log2 − 15）。使わない BAR は触らない。
7. VENDOR_REG1 の endian を little に、INTR2（0x4400）の mask_set（+0x10）と clear（+0x08）に全部の bit。
8. post_setup:
   - MDIO（ADDR 0x1100・WR_DATA 0x1104）: port 0 の reg 0x1f に 0x1600、続けて reg 0x16〜0x19・0x1b・0x1c・0x1e に 0x50b9・0xbda1・0x0094・0x97b4・0x5030・0x5030・0x0007（54 MHz の xosc を refclk に）、100〜200 µs。1 回の書き込み: ADDR に packet（port の bit 16〜19、port>>4 を bit 21、reg を bit 0〜15、command 0 = write を bit 20）、ADDR を読み戻し、WR_DATA に 0x8000_0000 | data、WR_DATA の bit 31 が落ちるまで 10 µs ごとに最大 10 回。落ちなければ ETIMEDOUT で start を止める。
   - PL_PHY_CTL_15（0x184c）の下位 8 bit を 0x12（PM clock 18.52 ns）。
   - UBUS_CTRL（0x40a4）の bit 13・bit 19（AXI の error 応答を止める）、AXI_READ_ERROR_DATA（0x4170）を 0xffff_ffff。読みの失敗が SError にならず全部 1 になる。
   - UBUS_TIMEOUT（0x40a8）= 0x0b2d_0000、RC_CONFIG_RETRY_TIMEOUT（0x405c）= 0x0aba_0000。
   - AXI_INTF_CTRL（0x416c）: bit 7 を落とし bit 11・12・13 を立てる。読み戻して bit 12 が 0（C1 か x1 の RC）なら下位 6 bit を 15。
   - MISC_CTRL_1（0x40a0）の bit 5（VDM の QoS）を落とす。
   - `brcm,fifo-qos-map` があれば、4 つの値（下位 4 bit）を 4 bit ずつ並べた語を TC_QUEUE_TO_QOS_MAP(i) = 0x4160 − 4i（i = 0〜7）に。`brcm,vdm-qos-map`（pcie2 の）は扱わない。
9. 世代の上限（`max-link-speed` が 1〜3 の時）。
10. link: HARD_DEBUG の CLKREQ_DEBUG_ENABLE（bit 1）・REFCLK_OVRD_ENABLE（bit 16）・REFCLK_OVRD_OUT（bit 20）・L1SS_ENABLE（bit 21）を落とし、PERSTB = 1、今と同じ待ち（100 ms の後 5 ms ごとに最大 100 ms）。上がらなければ ETIMEDOUT（`pcie: brcmstb link down`）。RC mode の確かめは今と同じ。`brcm,tperst-clk-ms`（dtparam `pciex1_tperst_clk_ms`、一部の NVMe の回避策）があれば、HARD_DEBUG の PERST_ASSERT（bit 3）を立てて PERSTB = 1、その ms 待って bit 3 を落とす（review L1、上限 1 秒）。`aspm-no-l0s` は ASPM を使わないので読まない。
10a. endpoint の待ち（review M1）: link の後、bus 1 の vendor が 0xffff か 0x0001（retry）の間、10 ms ごとに最大 1 秒読み直す（PCIe Base 4.0 §6.6.1、reset の後 1 秒まで CRS が許される。bridge の retry timeout は約 240 ms）。待ったら `pcie: brcmstb endpoint answered after N ms` を log。答えなければ割り当てが absent として飛ばす。
11. clkreq: 判断 D3（推し: 常に "safe"）。HARD_DEBUG の 4 bit を落として REFCLK_OVRD_OUT・REFCLK_OVRD_ENABLE を立て、PRIV1_ROOT_CAP（0x4f8）の bit 3〜7 を 2。zedBSD は ASPM・L1 substate を有効にしないので "safe" で失う物は電力だけ。dtb の `brcm,clkreq-mode` は読まない（pcie1 は "safe"）。rpi-6.12.y の brcm_config_clkreq は HARD_DEBUG を読まずに全体を書く（初期化されない値）が、ここは clock-request の bit だけを変え、SerDes・PERST の bit を保つ（review L4、mainline と同じ）。LNKCTL2 は 16 bit で書く（隣の LNKSTA2 の RW1C を触らない、review L3）。
12. outbound の窓・class・link の log・BAR の割り当ては今と同じ（2711 の「HARD_DEBUG bit 1 を立てる」は 2711 だけ）。
13. 失敗の時は PERST# を assert して（2712 は PERSTB = 0）、2712 は SerDes を IDDQ に戻し bridge を reset に戻してから（Linux の turn_off と同じ、review L8）map を外す。bridge の reset を解く前の失敗では register を触らない。

### 4.3 publish・割り込み

- DMA: dma-ranges のうち CPU 0 に着く項目を system memory の窓とし、bus_offset = その PCI base（0x10_0000_0000）、physical_limit = size − 1、address_bits 64、coherent = config の値（dtb には無いので 0）、max segment 16 MiB。2711 は今の 31 bit のまま。
- root bus は `drv_pci_bus_create_root(config.segment, 0, ...)`。config access の確かめを「segment が 0」から「segment が host の segment」に（Pi 4 は 0 のまま）。
- bus ops は 2 つの const の表: 今の表（Pi 4、MSI の hook 無し）と、`msi_establish`・`msi_disestablish` を足した表（board が msi の操作を持つ時）。
- `host_allocate_irqs`: board が msi の操作を持つ時だけ、MSI-X（capability 0x11）と MSI（0x05）を 1 本ずつ出す（PCI core は allocate の時に MSI-X → MSI → INTx の順に聞き、ENOTSUP・ENODEV の時だけ次へ進む。establish の失敗では INTx に戻らない）。capability が無ければ ENOTSUP。MSI は Message Control の bit 7（64 bit の address）が無ければ ENOTSUP（doorbell 0xff_ffff_f000 は 4 GiB の上、review M3）。INTx は今と同じ（dtb の interrupt-map、GIC SPI 219〜222）。
- config access: driver の中で組む address（`assign_resources`・`configure_root_port`・endpoint の待ち）にも host の segment を入れる（review H1。入れないと segment 1 で書き込みが全部捨てられる）。BCM2712 は bus 1 以下への access のたびに link を読み直す（Linux の map_bus と同じ。切れた link への request の abort を避ける、review L5）。BCM2711 は今のまま。
- hook は board の操作へそのまま渡す（handle・address・data は board が決める）。

## 5. Pi 5 の platform（新しい `src/drivers/platform/rpi5/rpi5-pcie.c`・`.h`）

- `drv_rpi5_pcie_init(fdt_phys)`: `kern_platform_init` の `start_rp1()` の後に呼ぶ。`drv_pci_init()`（EALREADY は可）、NVMe の driver の登録（`CONFIG_DRIVER_PCI_NVME`）、dtb を開き `brcm,bcm2712-pcie` を順に見る:
  - disabled は飛ばす（`pcie1: disabled in the device tree (config.txt needs dtparam=pciex1)` を log）。
  - 子に simple-bus の interrupt-controller（RP1 の bus）を持つ node（pcie2）は触らない。firmware の `pciex4_reset=0` の約束を守る。
  - segment は `linux,pci-domain`。0 か無い時は拒む（RP1 の USB の segment 0 と重なる）。
  - `resets`・`reset-names` から "bridge"（`brcm,brcmstb-reset` の phandle と id）と "rescal" を読む。
  - rescal: 判断 D2（推し: 済みか RP1 の link が生きていれば飛ばす）。STATUS（+0x8）の bit 0 が立っていれば飛ばす。立っていなくても RP1 が見つかっている（`drv_rp1_fdt()` が NULL でない、つまり pcie2 の link が上がっている = calibration は済んでいる）なら飛ばして log。どちらでもない時だけ START（+0x0）の bit 0 を立て、読み戻して立っていなければ EIO、STATUS の bit 0 を 100 µs ごとに最大 1 ms 待ち、START の bit 0 を落とす。理由: rescal は pcie0〜2 の共有で、RP1 の link が生きている間に calibration をやり直すと USB・Ethernet を乱すかもしれない（review M4）。
  - reset block の SET・CLEAR は write-only の register に bit だけを書く（読み・書き戻しをしない）。同じ bank の bit 12 は pcie2 の bridge（id 44）なので、read-modify-write は危ない（review L10）。line が block の大きさ（0x30 = 2 bank）を超えれば ERANGE。
  - bridge の reset の操作: id の bank = id / 32、bit = 1 << (id % 32)、bank の offset = bank × 0x18。assert は SET（+0x0）、deassert は CLEAR（+0x4）に書き 100〜200 µs。id 43 → offset 0x18、bit 11。
  - `msi-parent` が `brcm,bcm2712-mip` を指せば MIP を起こし（§6）、board の msi の操作にする。MIP が起きなければ MSI 無しで続ける（INTx）。
  - describe → segment・board・coherent を入れる → start → publish。失敗は log して boot を続ける。
- `drv_rpi5_pcie_refresh()`: `kern_platform_refresh_devices` から、`drv_pci_nvme_probe_namespaces()`。
- 数は最大 2 の枠だが、pcie0 は `linux,pci-domain = 0`（segment 0 は RP1 の USB と重なり拒む）なので、実際に起きるのは pcie1 だけ（review L2）。

## 6. MIP（新しい `src/drivers/platform/rpi5/rpi5-mip.c`・`.h`）

- RP1 の MIP0 は `rp1-irq.c` が RP1 の vector に使っている。MIP1 は別の block で、こちらは PCI の function の MSI・MSI-X を受ける。
- start: reg (1) を map（0xc0 byte）、reg (2) の address を親の cell 数で訳さずに読む（doorbell）、`msi-ranges`（phandle と GIC の 3 cell と数、kind は SPI であること）、`brcm,msi-offset`。offset + 数が MIP の 64 入力を超えれば ERANGE。手渡す数は最大 32。VPU の mask を全部 1、host の CFG を全部 1（edge）、host の mask を 0（rp1-irq.c と同じ）。
- 各 line を `kern_irq_set_mode(edge, high)` で確かめる（start の時、line は mask されている）。1 本でも失敗（GIC が line を持たない）すれば MIP は使わず INTx に。これで establish の時の失敗（PCI core は INTx に戻らない）を避ける。
- line の番号: GIC の INTID = 32 + msi-ranges の先頭 SPI + `brcm,msi-offset` + 番号（mip1: 287〜294）。device が書く data = `brcm,msi-offset` + 番号（8〜15）、address = doorbell（0xff_ffff_f000）。式は rpi-6.12.y と OpenBSD が同じ。実機で未確認（p003）。
- establish: 空いた番号を lock の下で取り、run・argument を入れ、lock の外で `kern_irq_register` → `kern_irq_unmask`（rp1-irq.c と同じ。`kern_irq_set_mode` は start の時に済ませる）。disestablish の後に同じ line を取り直すと、古い edge が 1 回だけ届きうる（handler は仕事なしで終わる）。
- 割り込み: run を lock の外で呼ぶ。判断 D5（推し: 入れる）: run が仕事なし（0）なら、MIP の host mask（0x40）を 16 回読む短い待ちを挟んで最大 8 回呼び直す（MSI が inbound の別の窓を通り、completion の書き込みを追い越しうる。PLDA で見た事象と同じ形の保険）。この読みは device の DMA を順序付けない。効くのは時間だけ（review L6）。最後に EOI。
- disestablish: mask → 表を空ける → in_flight が 0 になるまで 1 ms ごとに最大 1 秒 → unregister。間に合わなければ戻して EBUSY（PLDA と同じ契約）。

## 7. build と config

- `platform/arm64/vmunix.mk` の rpi5 の source に `rpi5-pcie.c`・`rpi5-mip.c`・`pci-brcmstb.c`、`CONFIG_DRIVER_PCI_NVME=y` の時 `pci-nvme.c`。
- `config/ci/config-rpi5.mk`: `CONFIG_DRIVER_PCI_NVME := y`。
- `platform/arm64/config-rpi5.txt`: 判断 D1（推し: `dtparam=pciex1` を足す）。HAT が無い板では link が上がらず log が 1 行と約 0.2 秒の待ちが増えるだけ。Gen 3 にはしない（`pciex1_gen` は書かない、`max-link-speed = 2`）。
- 判断 D4（推し: `max-link-speed` を守る）: Gen 2 が Raspberry Pi の保証。
- HAL（`include/hal/hal.h`）・toolchain は変えない。
- pci-nvme は `drv_dma_alloc_coherent` しか使わず、非一貫の device には uncached の写像（`kern_pmem_map_uncached`、arm64 は WS048 で承認済み）を渡すので、cache の clean・invalidate は要らない。data は bounce buffer の memcpy（速さは WS299 の範囲）。

## 8. 試験（p002）

- `plan/ws304/tests/` の host 試験（guest を起動しない）:
  - brcmstb の BCM2712: dtb（C1・D0、pcie1 を okay にした写し）からの describe の値、register の model での start の順と値（bridge の reset → PERST# → IDDQ → MISC_CTRL → inbound と remap → post_setup の MDIO と値 → 世代 → CLKREQ → PERSTB → 100 ms の後の config → clkreq safe）、MDIO の timeout、link down、DMA の窓（offset 0x10_0000_0000、limit 64 GiB − 1、bits 64、非一貫）、segment 1 の config access、MSI-X・MSI・INTx の割り当てと hook の転送。
  - MIP: dtb の mip1 からの値、初期化の書き込み、establish の data・address・INTID・edge・unmask、割り込みの再実行と EOI、disestablish、line の確かめの失敗で使わない事。
  - rpi5-pcie の bridge の reset と rescal の register の計算（できる範囲）。
- Pi 4 を壊さない確かめ（review M5・M7）: closed の WS048 の試験には頼らず、ws304 の `brcmstb-trace.c` で BCM2711 の start・publish・config・INTx・MSI の拒否・map の register の列（時刻つき、読みと書きの全部）を、WS の始まりの main（13c03a376）の pci-brcmstb.c と今の物で出し、`cmp` で同じことを確かめる（余分な書き込みも検出する）。WS048 の試験は 1 回だけ流した（p002 の記録）。Pi 4 の実機の起動は p003 の任意の項目（未実施）。
- build: `config-rpi5.mk` と `config-rpi4.mk` の `make -j16`（warning 0）。
- 実機（p003）の手順は `phase003/phase.md`。

## 9. 危険と未確認

- R1: MIP1 の SPI の式（247 + 8 + n）は出典 2 つが同じだが実機で未確認。違えば MSI が来ず NVMe の最初の admin command が時間切れになる。p003 で dmesg の `rpi5-mip` の行と、時間切れなら INTx での起動（MSI を止める手段: 下の R5）で切り分ける。
- R2: firmware が pcie1 の電源（M.2 HAT の 3.3 V）を入れるかは dtb からは分からない。link down なら HAT の LED と `dtparam=pciex1` の有無を見る。
- R3: rescal を飛ばす判断は STATUS の bit 0 が firmware の後も残る前提。残らなければ rescal を実行する（RP1 への影響は p003 で見る）。
- R4: 54 MHz の refclk の MDIO の列は D0・C1 の両方で Linux が書く値。
- R5: MSI を切る手段: 実機の切り分けのため、boot の option で MSI を止める物は今は作らない（§12 の規約: 試験用の switch を production に入れない）。必要なら p003 で uncleared にして設計を直す。
- R6: 64 bit の prefetch の窓（14 GiB）は使わない（BAR は 32 bit の 2 GiB の窓に置く）。大きな BAR の device は範囲の外。
- R7: NVMe の BAR は 64 bit の 16 KiB 前後。上位は 0 で 32 bit の窓に置く（今の 2711 の扱いと同じ）。
- R8（review M6）: root port の下の bridge（PCIe switch）は割り当てない（今の 2711 の扱い）。2 枚の NVMe の HAT（ASM1182e などの switch）では NVMe が見えず、`pcie: brcmstb bridge at 01:00.0 left unassigned` が log に出る。単一の NVMe の HAT（M.2 HAT+ など）が前提。買った HAT の型番はユーザーに確かめる（Q1 経由）。

## 10. design-reviewer の指摘と反映

2026-10-27、design-reviewer（1 回）。重大 1・中 7・軽 11。全部を反映した。

| 指摘 | 反映 |
| --- | --- |
| H1 driver の中の config access が segment 0 で組まれ、segment 1 で捨てられる | §4.3。内部の address に host の segment。host 試験で segment の行を消すと BAR が置かれず FAIL になることを確かめた |
| M1 CRS の endpoint が absent に見える | §4.2 の 10a。endpoint を 1 秒まで待つ。host 試験（retry 5 回） |
| M2 INBOUND_MAX = 4 と BAR4 の offset | §4.1。3 にし、4 つ以上は拒む |
| M3 32 bit の MSI は establish で失敗し INTx に戻らない | §4.3。allocate の時に bit 7 を見て ENOTSUP |
| M4 rescal が RP1 の生きた link を乱しうる | §5。RP1 が見つかっていれば実行しない |
| M5 Pi 4 の不変の確かめが弱い、Pi 4 の実機が無い | §8。register の列の全部の比較。Pi 4 の実機は p003 の任意の項目（未実施） |
| M6 switch つきの HAT | §9 R8。ユーザーに HAT を確かめる |
| M7 closed の WS の試験に依存 | §8。ws304 の試験だけで完結 |
| L1 tperst-clk-ms | §4.2 の 10 |
| L2 pcie0 は segment 0 で起きない | §5 |
| L3 LNKCTL2 の幅 | §4.2 の 11（16 bit） |
| L4 出典の clkreq の不具合 | §4.2 の 11 と code の comment |
| L5 link の毎回の確かめ | §4.3（2712 だけ） |
| L6 待ちの読みの意味 | §6 |
| L7 establish の lock の外 | §6 |
| L8 失敗の時の SerDes と bridge、id と offset の検査 | §4.2 の 13、§5、§6 |
| L9 SError の確かめ | rpi5.c の start_pcie・refresh の後に `drv_rpi5_serror_check()` |
| L10 reset block の書き方 | §5 |
| L11 review の前に実装を始めた | 事実。p002 の code は review の後に直した（phase002 の記録）。p001 の受け入れは版 2 |

## 11. 判断の項目（Q1 へ、推しつき）

| ID | 問い | 推し | 今の code |
| --- | --- | --- | --- |
| D1 | `config-rpi5.txt` に `dtparam=pciex1` を足すか | 足す。HAT が無い板は link down の log と約 0.2 秒が増えるだけ | 足した |
| D2 | 共有の rescal を実行するか | STATUS が済み、または RP1 の link が上がっていれば実行しない | そのとおり |
| D3 | clkreq の mode | 常に "safe"（ASPM・L1SS を使わないので電力だけ） | そのとおり |
| D4 | 世代 | `max-link-speed`（2）を守る。Gen 3 は `pciex1_gen=3` の時だけ | そのとおり |
| D5 | MSI の後に仕事が無い時の呼び直し | 入れる（最大 8 回） | 入れた |
| D6 | 置き場所 | pci-brcmstb.c に variant を足す（Pi 4 の register の列は不変を確かめた） | そのとおり |
| D7 | 使う HAT | 単一の NVMe の HAT が前提（R8）。型番をユーザーに確かめる | — |

出典の追加: PCI Local Bus Specification 3.0 §6.8.1.3（MSI の Message Control の bit 7）、PCIe Base 4.0 §6.6.1・§7.5.1.1（CRS と vendor 0x0001）、§7.5.3.19（Link Control 2）。
