<!-- awesome-plan project=zedbsd record=ws313-p006 -->

# ws313-p006: Ethernet（EQoS・glue・RGMII の PHY）

Status: in-progress（実装済み・実機未試験）
Queue: q1675-i02（P7 の配下 A）
WS: [WS313](../ws.md)、設計 [design.md](../design.md) §3、調べ [peripherals.md](../peripherals.md)

## 目的

DesignWare EQoS（0xfe368000、SPI 74）と外付けの RGMII の 1G の PHY で en0 を出す（`drv_s905x5m_ethernet_init`・`drv_s905x5m_ethernet_refresh`）。

## 実装（2026-10-29、commit 369bebd4a・470b4720d）

- `include/drivers/ethernet/dw-eqos.h`・`src/drivers/ethernet/dw-eqos.c`: `struct drv_dw_eqos_config` の末尾に `configure_phy(context, bus, phy, identity)` と `phy_context` を足し、`bring_up_phy()` で YT8531 以外の PHY の reset の後に呼ぶ（NULL なら何もしない。VF2 は config を memset するので影響しない）。
- `src/drivers/platform/s905x5m/s905x5m-ethernet.c`:
  - gate（REG0 bit 4 の sys_ethphy、REG1 bit 3 の sys_eth）、GPIOZ_0..12 を function 6、GPIOH_11 を function 4（MDC）、pull 無し・4 mA。
  - glue（PRG_ETH0 0xfe364000）: mode [2:0] = 1（RGMII）、m250_sel（bit 4）= 0（fclk_div2 1 GHz）、m250_div [9:7] = 4、TX clock enable（bit 10）→ 1 GHz ÷4 ÷2 = 125 MHz、TX delay [6:5] = tx-delay-ns 2 ÷ 2 = 1、RX の adjust（bits 24:13）を 0、inverted RMII（bit 11）を 0、TX と PHY の ref clock（bit 12）を 1。PRG_ETH1 = 0（internal_phy 2 で cali_val が無い時の HK の値）。前後の値を log。
  - MDIO の mux（0xfe360000 + 0x88）= 0 で外の bus。前後の値を log。
  - PHY の reset: DT の phy-handle の reset-gpios（GPIOH_9、active low・open drain）を 10 ms low、pull-up の入力に戻して 80 ms 待つ。PHY の address は DT の reg（0）。
  - `configure_phy`: identity が 0x001cc916（RTL8211F）なら page 0xd08 の reg 0x11 bit 8（TX delay）と reg 0x15 bit 3（RX delay）を落とし、元の page に戻す（UP realtek_main.c の rgmii の扱い）。前後の値を log。他の PHY は identity を log して何もしない。
  - dw-eqos の config: irq は DT の SPI 74 + 32 = 106（dw-eqos が level-high で登録）、FIFO は DT の tx 0x800・rx 0x1000、csr_clock_hz 0（MDC は最も遅い divider）、MAC は dw-eqos の選び（register に残った物、無ければ乱数）。
- refresh は dw-eqos が attach で link の service を始めるので、port が無い時の log だけ。

## 確かめ

- build: C5 の vmunix は p004 と同じく warning 0。VF2: `timeout 1800 make -j8 ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/pa-vf2 build/pa-vf2/vmunix`: exit 0、`warning:` 0。
- host 試験: 無し。
- 実機: 未実施。QEMU: 対象外。

## 夜の serial で見る行

- `s905x5m-ethernet: glue <PRG_ETH0> <PRG_ETH1> -> <新> <新> (tx delay 2 ns)`
- `s905x5m-ethernet: MDIO multiplexer ... -> 00000000 (external bus)`
- `s905x5m-ethernet: PHY reset on pin 45 (flags 7), held 10000 us, waited 80000 us`
- `dw-eqos: en0 version ...`（MAC の version register）、`dw-eqos: en0 PHY 0 id ........ (generic)`（PHY の ID、regs 2/3）
- `s905x5m-ethernet: RTL8211F tx delay register .... -> ....`・`rx delay register ...`、または `PHY ........ is not an RTL8211F`
- `dw-eqos: en0 ready, MAC ..., DMA .. bit, uncached`、その後の link の速さ。`dw-eqos: en0 failed at reset (ETIMEDOUT)` なら PHY の RX clock が来ていない（glue・pin・PHY の reset を疑う）

## 未確認・危険

- PHY の chip 名は資料に無い（RTL8211F は C4 からの推定）。RX の遅延を PHY でも MAC でも入れない（"rgmii" の Linux の扱い）ので、実機で受信が壊れたら RX delay を PHY 側に戻す案を試す。
- MAC の address は efuse（SMC）から読めないので、U-Boot が register に残していなければ乱数。
- 再開点: 夜の試験の結果を見て直す。
