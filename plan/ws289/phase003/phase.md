<!-- awesome-plan project=zedbsd record=ws289-p003 -->

# ws289-p003 汎用の Cadence GEM の core の分離（rp1-ethernet.c を glue に）

- 親: [ws289](../ws.md)。設計: [design.md](../design.md) 版 2 の §2 D2・§4.1〜§4.3・§4.8・§8・§9 の p003 の行・§10 の R2/R3・§11 の U6。
- Queue: Q1 の割当（P1、2026-10-23、worktree `/home/awe/zedBSD-worktrees/p1-ws289-p003`、branch `agent/p1-ws289-p003`、基点 main ffde72051 = p002 の汎用 SDHCI の core を含む）。Fable は使わない（ユーザー）。
- 出典の決定: 2026-10-23 ユーザー（Q1 経由）「rpi4を参照するよりも、共通部分は分離しましょう。」。U6（ユーザー「はい」）: Pi 5 の Ethernet は T1 の実機の確認が p003 の clear の条件。
- Status: cleared（2026-10-23 Q1: T1 の実機の Pi 5（10.0.30.5、main d3c9f6505 の rpi5 kernel を入れ替え、vmunix.old を保持）で SD の root（mmcblk0 ready、block addressing）・gem: PHY 1 id 600d84a2・en0 1000 Mbps・DHCP 10.0.30.5・SSH・DMA error 0。gateway への ping は起動の直後の 1 回目が 100 中 25 の損失、その後の 4 回（T1 1・Q1 3）は 0、host からの 200 回も 0。register の手順は分離の前と byte 一致（host 試験）なので分離による変化ではないと判定し、起動直後の損失は観察として残す。merge ccf175eae）

## 範囲と到達点

- 範囲: `src/drivers/ethernet/cadence-gem.c`・`include/drivers/ethernet/cadence-gem.h`（新規）、`src/drivers/platform/rpi5/rp1-ethernet.c`（glue に）、`tests/cadence-gem-host-test.c`・`.h`・`tests/cadence-gem-rp1.trace`（新規）、`platform/arm64/vmunix.mk`（rpi5 の一覧に core と `mii-phy.c`、host 試験の規則）。Zynq の glue は p005（この Phase では書かない）。`rpi5.c` は触っていない（公開の口 `drv_rp1_ethernet_init/refresh` は不変）。
- 到達点（design §9）: rpi5・rpi4・amd64 の build warning 0、host 試験 PASS（64 bit の Pi 5 の列が分離の前と同じ）、`nm -u` の境界、**実機の Pi 5 で SD の root・DHCP・SSH（T1）**。

## 実装

- core（Zlib）: rp1-ethernet.c の GEM の部分の移動。`struct drv_cadence_gem`（前の `struct gem_device` から RP1 の field を外し config の写しと MDIO の `drv_mii_bus` を足した物）を `gem_units[DRV_CADENCE_GEM_UNIT_MAX = 2]` に。公開の口は `drv_cadence_gem_attach(config, &gem)`・`drv_cadence_gem_start_service(gem)`。
- descriptor の幅は config（32 = 2 word、64 = 4 word）。stride は `descriptor_words`。`ADDRESS_HIGH`（word 2）と reserved（word 3）は 64 bit の時だけ書く（`set_upper_words`、送信の時の上位 address も同じ）。`RXQBASEHI`/`TXQBASEHI` の書き込み（reset と start）と `DMACR` の `DMA64` も 64 bit の時だけ。
- flag: `MAC_FROM_FILTERS`（RP1。tree の address が無い時に filter から）、`RESTART_RX_ON_USED_BIT`（Zynq 用。RXUBR を受信の割り込みの組に入れ、mask・unmask を RXDONE と一緒に。割り込みで RXEN を落として立てる。Procedure referenced: Linux v6.12 `macb_main.c` `macb_interrupt()`）、`USRIO_RGMII`（Zynq 用。`start_controller` で `GEM_USRIO` (+0x0C) に bit 0。Values referenced: Linux v6.12 `macb.h` の `GEM_USRIO`・`GEM_RGMII_OFFSET`・`MACB_RXUBR_OFFSET` を取って確かめた。`MACB_USRIO` は 0xC0 だが GEM は 0x0C）。
- hook: `reset_phy`（RP1: GPIO の pulse）、`setup_phy(context, bus, phy, &identity)`（RP1: BCM54213PE の adapter で `drv_bcm54213pe_initialize`）、`read_link`（RP1: `drv_bcm54213pe_read_link` を full duplex で包む。NULL なら `drv_mii_phy_read_link`）、`set_tx_clock`（速さが変わって link が上がった時。RP1 は NULL）、`establish_irq`（RP1: `drv_rp1_irq_init/of/establish`）。`phy_scan`（Zynq/QEMU 用）: 指定の address が `ENXIO` なら 0〜31 を走査。`phy_scan = 0`（RP1）では core は MDIO を読まない（MDIO の列は前と同じ）。
- 順は前と同じ: version の読み → MAC（filter の読み）→ reset → `reset_phy` → `setup_phy` → `gem: PHY` の log → DMA の割り当て → 割り込み → interface。失敗の log の段の名前（`MAC address`・`PHY reset`・`PHY`・`DMA buffers`・`interrupt`・`network allocation`・`network registration`）も同じ。
- glue（`rp1-ethernet.c`）: device tree の探索・clock の enable・map・tree の MAC・`clock_rate`（0 なら 200 MHz、design 版 2 M3）・DMA の窓の device の作成・hook。`EALREADY`（2 回目の attach）は前と同じ判定（register の map の後）。

### design からの差（小さい物、Q1 の確認用）

1. hook を `phy_setup` 1 つでなく `reset_phy` と `setup_phy` の 2 つにした（失敗の log の段 `PHY reset` と `PHY` を前と同じに保つため）。`setup_phy` の identity は in/out（core が読んだ値、読まない時は 0。glue が自分で読んだ値で置き換える）。
2. MAC は config の `mac` ＋ flag `MAC_FROM_FILTERS`（filter の読みを前と同じ位置 = version の読みの後・reset の前に保つため。design は glue の仕事としていた）。
3. RP1 の DMA の device の作成（`drv_rp1_dma_window`・`drv_dma_device_create_window`）は glue で attach の前に（design のとおり）。前は PHY の後だったので、窓の取得が失敗した時だけ log の位置が変わる（register の列は変わらない）。
4. `net_device_create` が失敗した時、前は poll の thread を作った（登録されていない interface で）。今は attach が gem を返さないので refresh は何もしない（失敗の道だけの差）。
5. spinlock の名前は `"RP1 GEM"` → `"Cadence GEM"`。
6. `pclk_hz` が 0 なら core は `EINVAL`（design 版 2 M3 のとおり。RP1 の glue は 200 MHz の fallback を自分で入れる）。

## 確認（host、2026-10-23）

- **同じ列の証拠**: 偽の controller・PHY（BCM54213PE と generic）・device tree・DMA・GPIO・割り込み・net の層を新しく書き（`tests/cadence-gem-host-test.h`・`.c`）、Pi 5 の glue を attach → 2 回目の attach → refresh 2 回 → open → 送信 4 つ（42・1514・1515・10 byte）と NULL → controller の完了と受信 2 つ（完全・壊れた）→ 割り込み（RXDONE|TXDONE|RXOVR）→ poll → link 100 → 間隔内の poll → link down → HRESP → close → 再 open → close と動かし、register の全 access・clock の register・GPIO・delay・割り込みの経路・DMA の割り当て・lock・barrier・log・descriptor・interface を 1 行ずつ印字（`-t`）。tree の MAC・filter の MAC・MAC 無し（`MAC address` で失敗）の 3 case を fork した子で。**分離の前の rp1-ethernet.c（main ffde72051 の物）と新しい core ＋ glue の trace は 812 行が byte 一致**（`cmp`、md5 `5e230c06b8a2a91820ff2290a7818717`）。前の trace を `tests/cadence-gem-rp1.trace` に固定し、make の規則が毎回 `cmp` する（error の番号は Linux の host の物）。
- 32 bit の形の検査（`-t` 無し）: version 2・PHY は address 7 だけ・flag `RESTART|USRIO` で、走査（address 0 の MDIO の frame `0x600a0000`、7 の `0x638a0000`、8 以降を読まない）、`setup_phy` に 7 と `01410cc2`、`MDIO divider 48`（108.33 MHz）、ring の大きさ `2×257×8`、受信・送信の全 descriptor と spare の値、**ring の後ろの guard の 64 byte に書かない**、送信で次の descriptor の word（sentinel）に触れない、`DMACR` に `DMA64` 無し、`USRIO` = 1、`IER` = `0xc86`（RXUBR 入り）、generic の link の読みで 1000 full と `set_tx_clock(1000)`、RXUBR の割り込みで `IDR` = RXDONE|RXUBR と NETCTL の RXEN の落とし・立て、poll で `IER` = RXDONE|RXUBR、`RXQBASEHI`/`TXQBASEHI`/`CFG6` の access 0。64 bit の形: 走査無しで MDIO の frame 0、hook の identity が log に、ring `2×257×16`、`RXQBASEHI`・`DMA64` を書く、flag 無しで `USRIO` に触れない。拒む config（48 bit・register 無し・pclk 0）は unit を取らず register に触れない、3 つ目の attach は `EBUSY`。
- 試験が効く事の確かめ（scratchpad の写しに変異を入れて）: 32 bit で word 2・3 を書く → guard の検査で FAIL、送信で上位を書く → sentinel で FAIL、RXUBR の mask 無し・restart 無し・`DMA64` を常に・HI の register を常に・走査無し → それぞれ FAIL、FDEN の値を変える → Pi 5 の trace が不一致。
- `make -o build/rpi5/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi5.mk cadence-gem-host-test` → `cmp` 一致、`cadence-gem-host-test: PASS`。
- `make -j16 -o build/rpi5/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi5.mk build/rpi5/vmunix` → rc 0、`warning:` 0、`arm64 vmunix check: PASS`（`cadence-gem.c`・`mii-phy.c`・`rp1-ethernet.c` の compile が log に有る）。vmunix の sha256 `d055d483e4b98558cec610a44335d718daa49371aa1d8a01d00372b152f6a7e8`（7544832 byte、tree c4f6e6be9）。
- `make -j12 -o build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix` → rc 0、`warning:` 0、`arm64 vmunix check: PASS`（rpi4 は core を link しない）。
- `make -j12 -o build/amd64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/vmunix` → rc 0、`warning:` 0、`amd64 vmunix check: PASS`。
- worktree の `build/llvm`・`build/NoctLang` は main の checkout の共有の物への symlink（読むだけ。toolchain は変えていない）。
- `llvm-nm -u` の境界: core の未定義は kernel の口（`kern_*`・`net_device_*`・`packet_buf_*`・`spin_*`・`kthread_create`・`thread_start`・`sched_*`・`clock_ticks`・`drv_dma_alloc_coherent`）と `drv_mii_phy_read_identity`・`drv_mii_phy_read_link` だけ（`drv_rp1_*`・`drv_rpi4_*`・`drv_zynq_*` 無し）。glue の未定義に `drv_cadence_gem_attach`・`drv_cadence_gem_start_service`。
- `git diff --check` → clean。

## 未実施

- 実機の Pi 5（T1 に依頼、下）。QEMU に RP1 の machine は無いので QEMU の確認は無い。
- Zynq の 32 bit の道の実機・QEMU の確認は p005（ここでは host 試験だけ）。

## T1 への依頼（実機の Pi 5、U6）

- kernel: branch `agent/p1-ws289-p003` の c4f6e6be9 以降（Q1 の merge の後は main の merge の commit）から `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi5.mk build/rpi5/vmunix`。p002（汎用 SDHCI）を含む。c4f6e6be9 の物は担当が build 済み: `/home/awe/zedBSD-worktrees/p1-ws289-p003/build/rpi5/vmunix`（sha256 `d055d483…f6a7e8`）。
- 入れ方: [WS278 の手順](../../ws278/tests/rpi5-hw-test.md) の「kernel だけの差し替え」（`scp vmunix kei@<Pi 5>:` → board で root になり `cp /boot/boot0/vmunix /boot/boot0/vmunix.old && cp ~kei/vmunix /boot/boot0/vmunix && sync && cmp ~kei/vmunix /boot/boot0/vmunix && reboot`）。address は前回 `10.0.30.5`（T1 が板の lock と address を確かめる）。戻すときは `vmunix.old`。
- 合格（design §8.3 の p003 の行）: 起動して SSH で入れる。`dmesg` に `sdhci: mmcblk0 ready`（root が SD = Pi 5 の SD の回帰、p002）、`gem: revision …, MDIO divider 96`、`gem: PHY 1 id 600d84a…`、`gem: en0 ready MAC …`（基板の MAC）、`gem: en0 link up 1000 Mbps full duplex`、`gem: en0 RX/TX enabled`。`ifconfig en0` に DHCP の address、host から SSH の login（kernel を入れ替えた後の新しい接続）、`ping -c 100 <router>` の loss 0。`gem: initialization failed`・`gem: DMA error`・stall watch の画面が出ない事。`dmesg` の `gem:` と `sdhci:` の行と `uname -a` を返す。

## 残り・引き継ぎ（Q1 へ）

- Master の Tools に `tests/cadence-gem-host-test.c`（`make … cadence-gem-host-test`、`cmp` と `cadence-gem-host-test: PASS`）を載せる事（担当は master.md を書かない）。
- p005 で core に足す物は無い見込み（32 bit・RXUBR・USRIO・走査は入れた）。Zynq の glue・RTL8211E・SLCR の tx clock は p005。

- 2026-10-23 Q1: T1 の実機の Pi 5 の試験は未実施。10.0.30.5 に届かない（ping「Destination Host Unreachable」、ssh「No route to host」）。板に何も書いていない。再開の条件: Pi 5 の電源と address（ユーザーの試験の一覧 U40）。
