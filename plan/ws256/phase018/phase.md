<!-- awesome-plan project=zedbsd record=ws256-p018 -->

# ws256-p018 VF2 の GbE（DW Ethernet QoS と PHY、gmac0）

Status: in-progress（2026-10-20 ユーザーの VF2 の実機: link up 1000 Mbps・DHCP で 10.0.30.2/16・「pingはOKです。」。残り: ssh（UAT の image に sshd が無い）。前: 板の前の code と host 試験 210 PASS（1881a2540））

- 親: [WS256](../ws.md)。Queue: q1292-i01（P2）。
- 範囲: [design-peripherals.md](../design-peripherals.md) §3.7（改訂 4）、§2.4 の `drv_fdt_dma_coherent` と板の既定、Minor 4（v1.3B は `starfive,tx-use-rgmii-clk` で TX の set_rate が要らない、YT8531 は推定なので MDIO で id を読む、AON SYSCON の gmac0 の interface の field）。板の前の code: DMA の ring、MAC・MTL・DMA の初期化、MDIO と PHY、link、送受信、割り込み、VF2 の glue、network の層への登録、visionfive2.c からの attach。hal.h は変えない。
- 完了の条件（板の前）: host 試験（fake の register の model で ring・MDIO・link）が合格、build が warning 0（VF2・virt）、実機で画面に出す行をこの文書に書く。実機の受け入れ（link・DHCP・ping・ssh）は板の接続と p022・p023 の後。

## 実装（2026-10-19、P2、commit 8963d12f8・57506f53f・dbe9c7e75）

| file | 中身 |
| --- | --- |
| `src/drivers/ethernet/dw-eqos.c`・`include/drivers/ethernet/dw-eqos.h` | DesignWare Ethernet QoS（4.x・5.x）の汎用の driver。attach で版と feature を読み、software reset、DMA の bus mode（fixed burst・AXI の OSR・burst 長・LPI、40/48 bit の時 EAME）、channel の PBL と受信 buffer の大きさ（RBSZ 2048）、MMC の割り込みの mask、MDIO の clock の範囲（CSR の rate から、不明なら最も遅い分周）、PHY の起動、ring の確保、PLIC の割り込みの登録、`net_device` の登録、service の thread。ring は送受信とも 128 個の 16 byte の descriptor を 1 つの 4 KiB の coherent の確保に置き（4 GiB の境界を跨がない）、descriptor ごとに固定の 2 KiB の buffer（coherent）を持つ。送信は buffer に複写（60 byte に満たない frame は 0 で詰める）、受信は buffer から packet に複写し CRC の 4 byte を外す。受信は常に 1 個を手元に残し tail をそこに置く（OpenBSD の手順）。open で ring を作り直し、station address、filter（全 multicast を通す）、MTL の queue（threshold mode か store-and-forward、FIFO の大きさ）、受信 queue 0 の有効化、DMA の開始、MAC の TE/RE。close で割り込み・RE・DMA を止め、送信 queue を flush、TE を止める。割り込みは channel の status を W1C で消し、送受信の割り込みを止めて worker を起こす（worker が ring を空にしてから戻す）。fatal bus error は 1 回だけ log。service の thread は 250 ms ごとに PHY の link を読み、変われば PHY と板の TX の clock、MAC の速度・duplex を合わせて carrier を出す。20 ms ごとに worker を起こす（割り込みの取りこぼしの保険） |
| `src/drivers/ethernet/mii-phy.c`・`include/drivers/ethernet/mii-phy.h` | clause 22 の標準の register（IEEE 802.3）: id、reset、advertise（PHY ができる 10/100 と、MAC と PHY の両方ができる時の 1000 全二重）と autonegotiation の開始、link の解決（両端が advertise する最速、全二重を先）。設計 §3.7 に無い file だが、YT8531 以外の PHY（「汎用」）を同じ道で動かすために分けた |
| `src/drivers/ethernet/yt8531.c`・`include/drivers/ethernet/yt8531.h` | YT8531（id 0x4f51e91b）の拡張 register（0x1e/0x1f、番号の register は元に戻す）: RGMII の受信の遅延（150 ps の段、固定の 1900 ps を足すのは段だけで届かない値の時）と送信の遅延、受信 pin の drive（1.8 V・3.3 V の表、2.5 V や表に無い値は既定 3）、link の速度ごとの TX の clock の反転（`motorcomm,tx-clk-adj-enabled` の時だけ） |
| `src/drivers/platform/visionfive2/vf2-ethernet.c`・`.h` | JH7110 の glue。`starfive,jh7110-dwmac` の 0x16030000（gmac0）だけを選ぶ（gmac1 は飛ばす）。pinctrl、`assigned-clock-parents`・`assigned-clock-rates`、clock 5 つ（stmmaceth・pclk・ptp_ref・tx・gtx）の enable、reset 2 つを assert → AON SYSCON の `starfive,syscon`（0xc、shift 18、3 bit）に RGMII = 1 → 5 ms → deassert → 5 ms。`phy-mode`（rgmii・rgmii-id・-rxid・-txid、他は ENOTSUP）、`phy-handle` の PHY の `reg`・遅延・`motorcomm,*`、`snps,*`・FIFO・`snps,axi-config`、`interrupt-names` の macirq（PLIC の 7）、`local-mac-address`、`drv_fdt_dma_coherent(fdt, node, 板の既定)`。`starfive,tx-use-rgmii-clk` が無い板（v1.2A）だけ速度の変わり目に SYS 108（GMAC0 GTX の源、PLL0 の 1〜15 分周）を 125/25/2.5 MHz にする（届くのは 125 MHz だけ。下の残り） |
| `src/drivers/platform/visionfive2/vf2-crg.c`・`.h` | `drv_vf2_clock_set_parent`（共有でない mux の selector を書く）と `drv_vf2_clock_apply_assigned_parents`（v1.3B の AON TX の親を RMII RTX に）。SYS 108 を set_rate 可に |
| `src/kern/platform/visionfive2.c` | `kern_platform_init` で SD の後に `attach_ethernet()`（`drv_vf2_ethernet_attach(&soc_tree, VF2_DMA_DEFAULT_COHERENT)`、失敗しても起動は続く）。P1 の p020 の行（vf2-pcie）とは別の場所 |
| `platform/riscv64/vmunix.mk` | VF2 の object に `vf2-ethernet.c`・`dw-eqos.c`・`mii-phy.c`・`yt8531.c`（`RISCV64_PLATFORM_SOURCES_visionfive2 +=` の別の段落） |

- 出典（block ごとに `Value(s) referenced:`・`Procedure referenced:`、file の先頭には書かない）: DW QoS の register の offset・bit と descriptor の bit は OpenBSD `sys/dev/ic/dwqereg.h`（ISC、master 859883f17cb8e20088fece5398547e4264ad30b3）、受信 buffer の大きさの field（RBSZ、bit 14:1）だけは Linux v6.12 `dwmac4_dma.h`（GPL の値の引用、Guardrail の 2026-10-15「GPL にも広げる」）。手順は OpenBSD `dwqe.c`・`if_dwqe_fdt.c`（ISC）。YT8531 の register・表・手順は Linux v6.12 `motorcomm.c`（設計 §0.2 の決定「手順の参考を許す」）と OpenBSD `ytphy.c`（ISC。master の版は YT8531 の init を持つ。設計 §0.2 の表の「YT8521 だけ」は古い）。clause 22 の register は IEEE 802.3。code・comment・構造・symbol の名前は写していない。
- 取得した物（P2 の worktree の `build/tmp/ws256-p018-docs/`、git に入れない、`SHA256SUMS` あり）: `obsd-dwqereg.h`（f19a9d28…）、`obsd-dwqevar.h`（18f074e7…）、`linux-v6.12-motorcomm.c`（9a316090…）、`linux-v6.12-dwmac4_dma.h`（23daf4bd…）。他は P1 の `build/tmp/ws256-docs/`（`obsd-dwqe.c`・`obsd-if_dwqe_fdt.c`・`obsd-ytphy.c`・DTS）。
- 決めた事（設計に無い細部、推定を含む）:
  - flow control（pause）は advertise も MAC の設定もしない（OpenBSD は常に有効にする）。
  - open で software reset をしない（OpenBSD と同じく、DMA を止めてから ring の base を書き直す）。base の書き直しで DMA の現在の descriptor が先頭に戻る、という前提は推定（実機の close→open で確かめる、下の 7）。
  - 受信は frame 1 つが buffer 1 つに入るものだけ（FD と LD が両方）。jumbo は使わない。VLAN の tag の付いた 1518 byte までを受ける。
  - MAC の address は DT（`mac-address`・`local-mac-address`）→ reset の前の MAC の register → 乱数（locally administered）の順。

## 試験（2026-10-19、P2、host と build だけ。QEMU・実機は無し）

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| host 試験（新規） | `plan/ws256/tests/dw-eqos/run.sh build/p018-host/dw-eqos` | exit 0、PASS 210・FAIL 0（通常と ASan/UBSan の 2 本 × 105、log build/tmp/p018-eqos.log） |
| 既存の VF2 の SoC の host 試験（vf2-crg を変えた） | `plan/ws256/tests/vf2-soc/run.sh build/p018-host/vf2-soc` | exit 0、PASS 94・FAIL 0 |
| 既存の DW MMC の host 試験（vf2-crg を使う） | `plan/ws256/tests/dw-mmc/run.sh build/p018-host/dw-mmc` | exit 0、PASS 60・FAIL 0 |
| VF2 の vmunix | `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p018-vf2 -j16 vmunix` | exit 0、warning・error 0（main の 0398b03d4 の p020 を merge した上、log build/tmp/p018-vf2.log）。vmunix に dw-eqos・yt8531・vf2-ethernet の文字列 25 個 |
| virt の vmunix（vmunix.mk を変えた） | `make ZEDBSD_CONFIG=config/ci/config-riscv64-virt.mk BUILD=build/p018-virt -j16 vmunix` | exit 0、warning 0 |

arm64・amd64 は変えた file を build しないので流していない。

host 試験の中身（`plan/ws256/tests/dw-eqos/`: `dw-eqos-host-test.c` は fake の controller と fake の PHY の上で実物の 8 file を動かす。`vf2-eqos-soc.dtsi` は SoC の部分、`vf2-eqos-v13b.dts`・`vf2-eqos-v12a.dts` は板の版ごとの gmac0、`include/kern/thread.h` は host 用の shim）:
- driver: software reset 1 回、bus mode（LPI・OSR 15/15・40 bit・burst 256〜32・fixed burst）、DMA の device（40 bit・coherent）、PBL 16・x8 無し・RBSZ 2048、MMC の mask、割り込み 7、thread。PHY の reset、MDIO の CR（198 MHz → 150〜250）、YT8531 の 1500 ps → 段 10・固定無し・100 Mbps の遅延は保つ、1.8 V の drive 3970 → 6・2910 → 3（他の bit は保つ）、advertise 0x01e1・GTCR 0x0200・BMCR 0x1200、番号の register を戻す。
- open: ring 128、受信の tail 127・送信の tail 0、127 個の受信 descriptor（OWN・IOC・BUF1V）、受信 queue 0、MTL（2 KiB、threshold 128/512、有効）、filter、station address、1 us の tick、DMA の開始、割り込み、TE/RE。
- link: 無 link で ENETDOWN、1000 全二重（MAC の速度 bit・反転・板への通知・carrier）、同じ link で変化無し、100 全二重、10 半二重、汎用の解決（1000 半二重は両端が言う時だけ、共通の mode 無し、autonegotiation 未完、全 1 の id）。
- 送信: 42 byte を 60 に詰める（IOC・FS・LS・OWN、中身と 0 の詰め）、1519 は EMSGSIZE、1518 は送る、wire を止めて 127 個で ENOBUFS、MAC の error を 1 回数える、回収して再び送る。
- 受信: 割り込み（EOI、worker、status の消去、送受信の割り込みを止める）、worker（CRC を外した 2 frame、error の frame は rx_errors・rx_dropped、割り込みを戻す、tail の移動）、400 frame を ring を何周もして順に、ring が満ちて RBU → worker が補充して次が入る。
- close（割り込み・MAC・DMA が止まり carrier 0、ENETDOWN）と再 open（ring を作り直し、送れる）。
- 失敗: PHY が無い（ENODEV、何も公開しない）、reset が終わらない（ETIMEDOUT）、address が無い（乱数、local bit）、同じ register の 2 回目（EALREADY）、汎用の PHY（YT8531 の register に触れない、100 で up）、controller に残った address。
- glue（v1.3B と v1.2A の木）: gmac1 を飛ばす、AON SYSCON 0xc の bit 20:18 = 1（他の bit は保つ）、clock の gate、reset の解除、macirq = 7、198 MHz の AXI の clock からの CR、板の既定の coherent、`snps,axi-config`。v1.3B: TX の親が RMII RTX、`local-mac-address`、PHY の遅延と drive、1000 で反転・GTX の源は触らない。v1.2A: TX は GTX の源のまま、firmware の MAC、受信 1900 ps は固定の部分・送信 1350 ps は段 9・drive は既定、1000 で GTX の源を PLL0 1.5 GHz / 12、100 は分周の届かない ERANGE（log に出て divider は保つ）。

## 実機で画面に出る行（p018 の実機の確かめ、ユーザーの目視と SSH。板の接続・p022・p023 の後）

boot の画面（kernel の log）:

1. `vf2-ethernet: bus clock … Hz (0)` — GMAC0 の AXI の clock（SYS 8 の STG AXI/AHB から）。p022 の `axi_cfg0` の値の半分の見込み（198 MHz 前後）。括弧が 0 でなければ rate が計算できず、MDIO は最も遅い分周で動く。
2. `dw-eqos: en0 version … irq 7` — 版の下 8 bit が 0x52（5.20）の見込み。
3. `dw-eqos: en0 PHY 0 id 4f51e91b (YT8531)` — **PHY の型番の確かめ（設計の未確認）**。違う id なら `(generic)` と出る。その id を記録する。
4. `yt8531: rx delay step 10, tx delay step 10, drive clock 6 data 3` — v1.3B の DT の値。v1.2A なら `rx delay step 0 + 1900 ps, tx delay step 9, drive clock 3 data 3`。`unsupported` の行が出たら PHY の LDO が 1.8 V でない。
5. `dw-eqos: en0 ready, MAC xx:xx:xx:xx:xx:xx, DMA 40 bit, coherent` — MAC が U-Boot の `local-mac-address`（板の EEPROM の値）と同じ事。`using a random one` の行が出たら U-Boot が渡していない。
6. `visionfive2: Ethernet is en0`。
7. cable を挿すと `dw-eqos: en0 link up 1000 Mbps full duplex`、networkd が開くと `dw-eqos: en0 RX/TX enabled`。
8. 出てはいけない行: `dw-eqos: en0 failed at reset (110)`（PHY から RGMII の受信の clock が来ていない、または reset の順の誤り）、`no PHY at 0`、`failed at DMA rings (95)`（板の DMA が非 coherent と扱われ uncached が無い: p023 の予備 (i) が要る）、`DMA fatal bus error`、`phy-mode … unsupported`、`visionfive2: no Ethernet (…)`、`PHY status failed`、`board clock for … failed`（v1.3B では出ないはず）。

shell と host から（#15 の方法による）:

9. DHCP の lease（networkd の log か `ifconfig en0` の address）。
10. host から ping（loss 0）、host から ssh で login（§5 の (C)）。
11. 大きい frame: host から 1472 byte の ping（`ping -s 1472`、1514 byte の frame）が通る。
12. 1000/100 の切り替え（switch の port か cable で 100 にして link の行と ping）。
13. `ifconfig en0 down` → `up` の後も ping が通る（open で software reset をしない前提の確かめ）。
14. §2.4 の短い coherency の確かめ（GMAC の MAC の内部 loopback）: p023 の `/dev/vf2-dmatest` の道具が要るので、この Phase の code には入れていない（下の残り）。

## 見つけた事・残り

- **v1.2A の 100/10 Mbps**: TX の clock の源（SYS 108）は PLL0 を 1〜15 で分けるので 25 MHz・2.5 MHz に届かない。v1.2A の板では gigabit だけが動く見込み（log に `board clock for 100 Mbps failed (34)`）。板の版は「つないだ時に記録」（決定 2026-10-18 17:46）。v1.2A なら別の Phase で TX の mux を RMII RTX 側に回す道などを調べる。
- **GMAC の短い coherency の確かめ**（設計 §2.4、§3.7 の試験の最初）: p023 の cdev の道具に依るので未実装。p023 の後に GMAC の内部 loopback で 1 回流す（p023 が道具を作る時に GMAC の版を足すか、p018 の実機の段で足す）。
- p013 の見つけた事の `net: UP: No such file or directory`（riscv64 で network の core か lo が起動していない疑い、未調査）が実機の DHCP を止める可能性がある。virt の走りで切り分けるのが早い（Q1 へ）。
- 設計 §0.2 の表の「OpenBSD ytphy.c は YT8521 だけ」は、master の版では YT8531 の init（遅延・drive・clk-out）を持つ。設計の直しは Q1 へ（この Phase は両方を出典に書いた）。
- `starfive,tx-use-rgmii-clk` の板で、AON の TX の mux を RMII RTX に回した後の実際の TX の rate は gmac0_rmii_refin（DT では 50 MHz の固定の clock）に依る。Linux も OpenBSD も rate を設定しないので同じにした。実機で gigabit の送信が通るかで確かめる（上の 10・11）。
- Q1 の判定、merge。実機の確かめは板の接続（#10）と p022・p023 の後に T1/T2 経由で。
