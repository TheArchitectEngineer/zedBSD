# WS313: the S905X5M peripheral drivers (SD, Ethernet, USB): research notes

State on 2026-10-29 12:30: the drivers are written and build (warning 0), but
nothing is checked on hardware. The research below was read from the trees under
`build/ref/` in the p7-ws313 worktree (read with `git show HEAD:<path>` where the
checkout is sparse). What was built from it is in "Result" at the end and in
[phase004](phase004/phase.md) (SD), [phase005](phase005/phase.md) (USB) and
[phase006](phase006/phase.md) (Ethernet).

Sources:
- [HK] hk-common-drivers-s7d-6.12 @34daba9 (Hardkernel common_drivers, odroids7d-6.12.y)
- [UP] linux-upstream @c339f4010
- [UB] hk-bl33-uboot-c5-v2023.01 @3c17bc80
- the DTB `build/tmp/c5dtb/c5.dtb`, decompiled with `dtc -I dtb -O dts`

## Device tree facts (c5.dtb)

| Node | reg | IRQ (SPI, type) | Notes |
| --- | --- | --- | --- |
| `sd@fe08a000` "amlogic,meson-v8-mmc" | fe08a000/0x1000, fe00016c/4 (clock ctrl), fe004024/4 (pinmux C) | 0xb1 = 177, edge (1) | bus-width 4, cap-sd-highspeed, cd-gpios <&gpio 22 0> = GPIOC_6, vmmc = regulator-fixed on GPIO 42 = GPIOH_6 active-high (always-on, boot-on), vqmmc = regulator-gpio on GPIO 33 = GPIODV_4 (states: 1.8 V → 1, 3.3 V → 0) |
| `ethernet@fe368000` "amlogic,meson-axg-dwmac","snps,dwmac-4.00" | fe368000/0x12ec, fe364000/8 (PRG_ETH0/1) | 0x4a = 74, level-high (4) | phy-mode "rgmii", amlogic,tx-delay-ns 2, phy-handle → mdio-mux mdio@0 ethernet-phy@0 (reg 0, max-speed 1000, reset-gpios <&gpio 45 7> = GPIOH_9 active-low + open-drain, assert 10000 µs, deassert 80000 µs), rx-fifo 0x1000, tx-fifo 0x800 |
| `mdio-multiplexer@360000` "amlogic,g12a-mdio-mux" | fe360000/0xa4 | | mdio-parent-bus = the MAC's snps,dwmac-mdio child |
| `crg@fe340000` "amlogic, crg-host-drd" | fe340000/0x8000 | 0x82 = 130, edge (1) | phys = usbphy1 u2p@0 |
| usbphy1 `u2p@0` "amlogic,u2phy-s7d" | fe348000/0x10 (u2p regs r0..), fe002000/0x100 (reset), fe010330/4 (efuse trim), fe34c000/0x60 (phy cfg) | | reset-level 0x40, phy-reset-level-bits 9, usb-reset-bit 4, pll-settings <0x244a0030 0x3200022 0 0x7e18 9> |
| pinctrl `bank@4000` | mux fe004000/0x4c, gpio fe0040c0/0x2cc | | GPIO numbers in the tree follow [HK] include/dt-bindings/gpio/meson-s7d-gpio.h: E 0-1, B 2-15, C 16-23, D 24-28, DV 29-35, H 36-47, X 48-67, Z 68-80, TEST_N 81, CC 82-83 |

Note: the SD and USB IRQs are edge-triggered in this tree, and the Ethernet IRQ is level-high.

## Pin controller (verified)

[HK] drivers/gpio/pinctrl/pinctrl-meson-s7d.c (meson_s7d_periphs_banks, meson_s7d_periphs_pmx_banks) agrees with [UP] drivers/pinctrl/meson/pinctrl-amlogic-a4.c (aml_def_regoffs, aml_bit_strides) and [UP] amlogic-s7d.dtsi:
- Mux: register = 0xfe004000 + (pmx word + pin/8)*4, 4 bits per pin. Pmx words: B 0x0, DV 0x2, X 0x3, CC 0x5 (bit offset 24), Z 0x6, C 0x9, H 0xb, TEST_N 0xf, D 0x10, E 0x12.
- GPIO: base 0xfe0040c0. Each bank's words: in +0, out +1, dir +2 (1 = input), pull-en +3, pull +4 (1 = up), drive +7 (2 bits per pin). Bank starts in words: Z 0x00, X 0x10, H 0x20, D 0x30, E 0x40, C 0x50, B 0x60, DV 0x70, TEST_N 0x80.
- Functions: sdcard_d0..d3/clk/cmd on GPIOC_0..5 = func 1 (card_det GPIOC_6 can also be func 1, but the tree uses it as a GPIO). The tree's sd pins: pull-up, drive 3000 µA. eth_* RGMII on GPIOZ_0..12 = func 6, eth_mdc GPIOH_11 = func 4; the tree's eth pins: bias-disable, drive 4000 µA.
- Card detect polarity: [HK] meson-gx-mmc.c meson_mmc_get_cd returns `!gpio`, and the tree's flag is ACTIVE_HIGH, so **a card is present when GPIOC_6 reads low**.

## Clocks (verified, [HK] drivers/clk/meson/s7d.c + s7d.h, base 0xfe000000)

- SYS_CLK_EN0_REG0 0x44: ethphy bit 4, sd_emmc_a/b/c bits 24/25/26. REG1 0x48: eth bit 3, usb bit 26.
- SD_EMMC_CLK_CTRL 0x16c, controller B: mux [27:25] with table {0 xtal, 1 fclk_div2, 2 fclk_div3, 3 hifi, 4 fclk_div2p5, 6 gp1, 7 gp0}, div [22:16] (7 bits, rate = parent/(div+1)), gate bit 23.
- ETH_CLK_CTRL 0x164: rmii div [6:0] from fclk_div2, rmii gate bit 8, 125 MHz gate bit 7 (fclk_div2/8).
- The vendor MMC driver's "src_clk_rate" defaults to 1 GHz (fclk_div2) for SD high speed. At 400 kHz it uses xtal (24 MHz) as the source with AUTO_CLK off.

## SD controller (V8), verified against [HK] meson-gx-mmc.c and aml_sd.h

- Registers: CLOCK 0x0 (div [5:0] one-based, src [7:6] not used on V8, core/tx/rx phase [9:8]/[11:10]/[13:12], always-on bit 28 on V8), DELAY1 0x4, DELAY2 0x8, ADJUST 0xc, START 0x40, CFG 0x44 (bus width [1:0]: 0=1-bit 1=4-bit; blk len log2 [7:4]; resp timeout log2 [11:8]; rc_cc [15:12]; always-on 18; stop clock 22; auto clk 23; err abort 27), STATUS 0x48 (write-1-to-clear; bit 31 busy, bit 30 desc busy, DAT lines [23:16]), IRQ_EN 0x4c, CMD_CFG 0x50, CMD_ARG 0x54 (**writing it starts the command**), CMD_DAT 0x58, CMD_RSP 0x5c, RSP1..3 0x60..0x68, ADDR64 0xa0 (V8: BUS64 bit 24, upper DMA address bits).
- Status bits: RX data CRC [7:0], TX CRC 8, desc err 9, resp CRC 10, resp timeout 11, desc timeout 12, end of chain 13, resp status 14, SDIO 15. Ack by writing 0x7fff.
- CMD_CFG: length [8:0] (block count in block mode, else bytes), block mode 9, R1b 10, end-of-chain 11, timeout log2 ms [15:12], no resp 16, no cmd 17, data io 18, data write 19, resp no-CRC 20, resp 128 21, resp num 22, data num 23, cmd index [29:24], owner 31.
- Long response: Linux resp[0]=RSP3, resp[1]=RSP2, resp[2]=RSP1, resp[3]=RSP. This is the same bit layout (CRC byte included) as zedBSD's dw-mmc.c response[] when word 0 = RSP.
- Init: CLOCK = always-on bit 28 | div 0x3f | core phase 180 (2<<8); CFG = resp timeout 8, rc_cc 4, blk len 9 (the vendor leaves err abort off, upstream sets it); write 0 to 0x150 (vendor V8); START = 0; ack the status; IRQ_EN = CRC | timeouts | end-of-chain.
- Direct mode (no descriptors): CMD_CFG (with end-of-chain), CMD_DAT = 32-bit DMA address of a bounce buffer (bit 0 = SRAM), CMD_RSP = 0, then CMD_ARG. On an error: wait until STATUS bits 31/30 clear (10 ms).
- The V8 internal SRAM sits at offset 0x400 (descriptors) and 0x800 (2 KiB data). The vendor uses it only with the `dram_access_quirk` property, and sd_emmc_b does not have that property.
- A clock change: set CFG stop-clock, clear auto-clk, change the clock source and divider, then clear stop-clock.
- U-Boot ([UB] configs/amlogic/s7d_odroidc5_defconfig) has no UHS support, so the card is at 3.3 V signalling when the kernel starts. CMD0 → CMD8 → ACMD41 → CMD2/3/9/7 works without a power cycle. Keep GPIODV_4 low (3.3 V) and GPIOH_6 high.
- RAM can reach 4 GiB ([UB] board_init_mem caps bootm at 0xe0000000), so the bounce buffers must be below 4 GiB (`kern_pmem_alloc_limited(..., 0xffffffff, ...)`) and kept in sync with `kern_dcache_clean_range` and `kern_dcache_invalidate_range`. The tree has no dma-coherent property.
- No shared SD card layer exists: dw-mmc.c and sdhci.c each carry their own card protocol. Plan: a new src/drivers/mmc/meson-mmc.c that follows dw-mmc.c's card layer (identify_card, configure_bus, transfer runs with recovery, publish_disk as mmcblk0, DISK_REMOVABLE, 512 B blocks, DISK_STATS_KIND_SDMMC). Note: P7's header says the s905x5m functions return a negative errno, while dw-mmc returns a positive one.
- Planned clocks: identification at 400 kHz = xtal 24 MHz / 60; default speed = 24 MHz / 1 (≤ 25 MHz); high speed = fclk_div2 1 GHz / 20 = 50 MHz (CLKC mux 1, div 0, gate on).

## Ethernet, from [HK] and [UP]

- dw-eqos.c already runs a non-YT8531 PHY on the standard registers. It picks the MAC address from the board, then from the controller's registers, then a random one. set_speed may be NULL. VF2 memsets its config, so a new optional field in struct drv_dw_eqos_config is safe for VF2.
- The PHY is probably an RTL8211F (ODROID-C4 in [UP] meson-sm1-odroid.dtsi: RTL8211F 0x001cc916 with the same rgmii + tx-delay-ns 2 settings). For phy-mode "rgmii", Linux [UP] drivers/net/phy/realtek/realtek_main.c (rtl8211f_config_rgmii_delay) **clears** both PHY delays: page select reg 0x1f; page 0xd08 reg 0x11 bit 8 = TX delay, reg 0x15 bit 3 = RX delay. The MAC glue adds the 2 ns TX delay.
- Proposed dw-eqos change (additive, VF2 unaffected): `int (*configure_phy)(void *context, const struct drv_mii_bus *bus, unsigned phy, uint32_t identity); void *phy_context;` in struct drv_dw_eqos_config, called in bring_up_phy() after drv_mii_phy_reset() for a non-YT8531 PHY.
- Still to read: [HK] drivers/net/ethernet/stmicro/stmmac/dwmac-meson8b.c (S7D PRG_ETH0/1 RGMII bits, cali_val written to PRG_ETH1) and [HK] drivers/net/mdio/mdio-mux-meson-g12a.c (ETH_PHY_CNTL2 at 0x88: the external bus select). U-Boot's DT says internal PHY (mc_val 0x1804), so the glue must be reprogrammed for RGMII.
- U-Boot reads the MAC address from efuse user data through an SMC (efuse_read_usr) and the kernel cannot do this (see "SMC" below). Expect the dw-eqos random-address fallback unless U-Boot left an address in the MAC registers.
- The sources needed in ARM64_S905X5M_SOURCES: src/drivers/ethernet/dw-eqos.c, mii-phy.c, yt8531.c (dw-eqos includes yt8531.h), so far built only for riscv64.

## USB, from [HK] and [UB]

- Host role: [UB] drivers/amlogic/usb/host/xhci-crg.c crg_set_mode clears bit 0 of base + 0x20FC. ([HK] crg.c leaves set_mode as a TODO.) The quirks are XHCI_NO_64BIT_SUPPORT and XHCI_RESET_ON_RESUME.
- Hand-off model: src/drivers/platform/rpi5/rp1-usb.c shows the controller to the PCI core as a function of a virtual root bus (config space in memory, BAR0 = registers, INTx served from the SoC interrupt and a polling thread). Give `drv_dma_device_create` address_bits 32. Because the DMA device then stays below 4 GiB, **no pci-xhci.c change looks necessary**.
- PHY init ([HK] phy-meson-aml-usb2-com.c meson_u2phy_aml_init + socs/phy-meson-s7d-usb.c):
  1. enable the SYS_USB gate (REG1 bit 26);
  2. set level reset bit 9 at 0xfe002040, i.e. take the PHY out of reset (hold_reset(true) ORs the bit in);
  3. pulse USB reset: write 1<<4 to 0xfe002000 (done once);
  4. wait 50 µs;
  5. host mode: r0 (0xfe348000) host_device = 1 (bit 0), POR = 0 (bit 3) ([HK] include/linux/amlogic/usb-v2.h union u2p_r0_v2);
  6. reset the PHY config: clear level bit 9, wait 50-100 µs, set it again;
  7. trim: efuse 0xfe010330 bit 12 = trim valid, value = ((reg>>8)&0xf)+2 capped at 12, else pll-settings[4] = 9. Write a thermometer mask ((1<<n)-1) into the low 12 bits of cfg+0x10 (cfg = 0xfe34c000), which differs from the briefing;
  8. cfg+0xc = 0x39, clear cfg+0x38 bit 27, cfg+0x50 = pll-settings[3] = 0x7e18;
  9. poll r1 (0xfe348004) phy_rdy (bit 0, [HK] usb-v2.h union u2p_r1_v2) up to about 1 ms;
  10. PLL at cfg+0x40: set bit 27; write 0x549540|bit27; set 26, wait 20 µs; set 25, wait 20 µs; set 24; poll bit 31 up to 5×20 µs; retry up to 5 times.
- U-Boot powers the hub only when `early_usb_start=true` (cmd/showlogo.c: "usb hub-on 1"). The power domain U2H (5) may therefore be off if U-Boot never started USB. Reading the controller's registers in an off domain may hang.

## SMC / power domains

The kernel has no SMC call open to drivers (nothing in include/hal/hal.h or include/kern/). The secure power controller ([UP] drivers/pmdomain/amlogic/meson-secure-pwrc.c: SM_A1_PWRC_SET/GET) and the efuse read both need one. The plan is to assume ETH (always on) and EMMC_B (U-Boot booted from SD) are on, and to report that USB may need U2H. This is not a HAL request yet: first test whether the xHCI CAPLENGTH/HCIVERSION reads sane after U-Boot.

## Next steps (in order)

1. src/drivers/platform/s905x5m/s905x5m-soc.{c,h}: clock gate, reset (pulse 0x0+(n/32)*4, level 0x40+(n/32)*4), pinmux and GPIO helpers with the tables above, and FDT lookups (reg, interrupts, cd-gpios). Host test against c5.dtb.
2. meson-mmc.c + s905x5m-mmc.c (SD, root fs). Host test with fake registers.
3. s905x5m-ethernet.c (glue, MDIO mux, PHY reset GPIOH_9, RTL8211F hook) + the dw-eqos hook; check that VF2 and RPi5 still build with zero warnings.
4. s905x5m-usb.c (PHY + virtual PCI function, 32-bit DMA, IRQ SPI 130 + polling).
5. Replace s905x5m-stubs.c in platform/arm64/s905x5m.mk; the file was removed with git rm on 2026-10-29.

## Checks for the serial session (once code exists)

SD: log the CLKC 0x16c and controller CLOCK/CFG before init, the card-detect level, CMD8 echo, OCR, CID/CSD, bus 4-bit/clock, then a read of MBR sector 0 (0x55aa).
Ethernet: PRG_ETH0/1 before and after, the MAC version register, the PHY ID (regs 2/3), the PHY delay register values, then link speed.
USB: the 0x20FC value, PHY r1 ready, PLL cfg+0x40 lock bit 31, xHCI CAPLENGTH/HCIVERSION/HCSPARAMS1, port status of the hub port.

## Result (2026-10-29, implemented, not tested on hardware)

| File | What |
| --- | --- |
| `src/drivers/platform/s905x5m/s905x5m-soc.{c,h}` | clock gates and the SD clock selector, reset pulse/level, pinmux, GPIO, FDT lookups (node by compatible and address, GIC interrupt, GPIO specifier, raw reg for the USB PHY's bus without ranges), spin delay |
| `src/drivers/mmc/meson-mmc.{c,h}` | the Amlogic SD/eMMC core: direct mode, polled, 64 KiB bounce buffer below 4 GiB, card layer after dw-mmc.c, mmcblk<unit> |
| `src/drivers/platform/s905x5m/s905x5m-mmc.c` | sd_emmc_b glue: gate, pins, supplies from the DT regulators, CLKC selector (xtal for 400 kHz and 24 MHz, fclk_div2 for 50 MHz), sector 0 check |
| `src/drivers/platform/s905x5m/s905x5m-ethernet.c` | PRG_ETH0/1 for RGMII (125 MHz from fclk_div2 /4 /2, 2 ns TX delay), MDIO mux external, PHY reset GPIOH_9, RTL8211F delays off through the new dw-eqos `configure_phy` hook |
| `src/drivers/platform/s905x5m/s905x5m-usb.c` | USB 2 PHY1 bring-up, CRG host role, virtual PCI root bus for pci-xhci.c, 32-bit DMA, SPI 130 edge + polling |
| `include/drivers/ethernet/dw-eqos.h`, `src/drivers/ethernet/dw-eqos.c` | `configure_phy` and `phy_context` (additive; VF2 builds with 0 warnings) |
| `platform/arm64/s905x5m.mk` | the stubs replaced by the files above, dw-eqos.c, mii-phy.c, yt8531.c and pci.c |
| `tests/meson-mmc-host-test.c` | host test with a fake controller and card (driver 39, version1 5, glue on the real DTB 12 checks; all PASS) |

Choices made while writing: the card detect is logged only (the card U-Boot read is
tried even if the pin says empty); the default speed runs from the crystal (24 MHz),
as the vendor driver runs its legacy timing; IRQ_EN stays 0 and the status is polled.
The serial lines to read are listed in each phase.md.
