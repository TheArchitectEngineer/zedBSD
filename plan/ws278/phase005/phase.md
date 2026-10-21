<!-- awesome-plan project=zedbsd record=ws278-p005 -->

# ws278-p005 RP1 の Ethernet（Cadence GEM）

- 親: [ws278](../ws.md)、Queue: q1543 / q1543-i02（P3、2026-10-21）
- Status: in-progress（実装と build まで。実機の確認は WS278 の最後の一括の試験）

## 目的と受け入れの条件

Pi 5 の有線 Ethernet（RP1 の Cadence GEM + BCM54213PE の PHY）を `en0` として使う。

- build（warning 0）。
- 実機（一括の試験）: `gem: revision …`、`gem: PHY 1 id 600d84a…`、`gem: en0 ready MAC …`（board の MAC）、cable を挿して `gem: en0 link up 1000 Mbps full duplex`、DHCP で address、`ping` が通る。

## 設計

- `src/drivers/platform/rpi5/rp1-ethernet.c`（rpi4-ethernet.c の GENET を写して GEM に書き直した。共通化はしない）。
- 起動: rp1 の子の `cdns,macb` を探し、`phy-mode`（`rgmii-id` → PHY が rx・tx の delay を入れる）と `phy-handle` の PHY の address（1）を読む。RP1 の clock の Ethernet と TSU の enable bit が落ちていれば立てる（source と divider は触らない）。register を写像し、version（MID）・CFG6（priority queue・DMA64）・CFG8（screener）を読む。MAC は tree の `local-mac-address`（firmware が書く）、無ければ GEM の filter に残った物、無ければ失敗（作らない）。controller の reset（queue base・screener を空に、MDIO の divider = RP1 の clk_sys 200 MHz → 96 分周で 2.08 MHz、MDEN）。PHY の reset（`phy-reset-gpios` = RP1 GPIO 32 の active low、`phy-reset-duration` 5 ms + 1 ms、離して 1 ms）、既存の `bcm54213pe.c` で PHY を始める。
- DMA: RP1 の窓（offset 0x10_0000_0000）、64 bit の descriptor（16 byte）。ring（受信・送信 各 256 + 未使用の queue を閉じる予備 1）と slot（各 256 × 2 KiB）は kernel の続く限り持つ（close でも返さない）。
- open: reset → ring（受信は全部 controller の物で slot を指す、送信は全部 driver の物）→ queue 0 の base、未使用の queue は予備の descriptor に disable で → LADDR0 に MAC → NETCFG（FCS を除く、1536、全 frame を受ける COPYALL、broadcast、RX offset 0、checksum offload 無し）→ DMACR（DMA64、CPU の byte order、2 KiB の受信、16 beat）→ 統計を消し RXEN・TXEN → ISR を消し IER（RXDONE・TXDONE・RXOVR・HRESP）。
- 送信: slot に写し、短い frame は 60 byte まで 0 で埋め、descriptor の address → barrier → status（長さ・LAST・WRAP、USED を落とす）→ STARTTX。回収は USED が立った slot。1 つは常に driver の物（満杯の判定）。
- 受信: word0 の USED が立った slot の SOF・EOF・長さを確かめて packet に写し、status = 0、word0 に address（USED を落とす）で返す。
- 割り込み: RP1 の vector 6（level、p004b の経路、dispatch が IACK）。handler は ISR を読んで書き戻し、RXDONE は poll が一束を処理するまで IDR、TXDONE は worker を起こす、HRESP は止める。20 ms ごとの thread が link（250 ms ごと）と取り逃しを拾う（GENET と同じ）。
- link: `drv_bcm54213pe_read_link` の速度で NETCFG（1000 は 100 と 1000 の両方、100 は 100、全二重）。RP1 は送信の clock を自分で合わせる（OpenBSD の rp1-gem の扱い）。
- 出典: OpenBSD の if_cad.c（register・bit・descriptor・reset・up・送受信・MDIO・link の手順）、Raspberry Pi の Linux の clk-rp1.c・dt-bindings/clock/rp1.h（clock の register と番号、GPL の値の引用）。各 block の「Value(s) referenced / Procedure referenced」。

## 変えた所

- 新: `src/drivers/platform/rpi5/rp1-ethernet.c/h`。
- `src/kern/platform/rpi5.c`（Ethernet の init と refresh）、`platform/arm64/vmunix.mk`（rpi5 の source に rp1-ethernet.c と bcm54213pe.c）。

## 確認（2026-10-21 P3、host）

- rpi5 kernel build: rc=0、warning 0、vmunix check PASS。
- 未実施: host の単体の試験（net の層に繋がるので作らなかった。register の値と手順は OpenBSD と照らして読んだ）、実機。

## 危険

- R12: firmware が RP1 の Ethernet の clock の source・divider を設定していなければ、enable だけでは 125 MHz にならず link しても frame が通らない。log の `gem: Ethernet clock was off (…)` の値を見る（その時は clk-rp1 の divider の設定を足す）。
- R13: `local-mac-address` が 0 のままなら `gem: no Ethernet address from the firmware` で en0 無し。
- R14: GEM が DMA64 を持たなければ（`gem: no 64-bit DMA reported`）、RP1 の DMA の番地（0x10_… 以上）が descriptor に入らない。その時は rp1 の dma-ranges の 3 つ目（offset 0）の窓に替える。
