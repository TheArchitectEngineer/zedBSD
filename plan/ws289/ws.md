<!-- awesome-plan project=zedbsd record=ws289 -->

# WS289 ZYBO の単独起動: SD・LAN・SD からの起動

- Master: [plan/master.md](../master.md)（2026-10-23 作成）。Primary Milestone: MG008（HAL の移植と異種機）。Related: [WS286](../ws286/ws.md)（ARMv7 の HAL・ZYBO の BSP、この WS の前提）。
- Status: planning
- 出典: 2026-10-23 ユーザー（WS286 p005 の cleared の報告の後）「メモリがあまり多くないのでSDドライバがあるとうれしいです。LANのドライバもついでに動かしてしまえませんか？単独ブートできるといいと思います。」。WS286 を作った時のユーザーの希望「まずはSDブートで、SSHでカーネルを更新して、Switchbotで再起動、というのがいいですね。」も同じ方向。WS286 の design の U8（SMP・L2・SD・LAN・SD 起動は後の WS）のうち SD・LAN・SD 起動をこの WS にした。

## 目的と完了の条件

- SD: Zynq の SDIO0（Arasan SDHCI、base clock は SLCR の 50 MHz）の driver。root を SD の partition に置き、ramdisk の分の RAM（512 MiB）を使わない。Pi 4 の `src/drivers/platform/rpi4/rpi4-sdhci.c` の共通に出来る所を使う。
- LAN: GEM0（Cadence GEM、RGMII）と PHY RTL8211E（MDIO address 0、reset は gpio0 46）。Pi 5 の `src/drivers/platform/rpi5/rp1-ethernet.c`（同じ Cadence GEM）の共通に出来る所を使う。DHCP と SSH で入れる事。
- SD からの起動: SD の BOOT.BIN（U-Boot SPL、ps7_init）から zedBSD の kernel を読んで起動する。JTAG も UART の操作も無しで `login:` と SSH まで。kernel の更新は SSH で SD に書き、再起動。
- 完了: 実機の ZYBO を SD の boot mode にして電源を入れると、JTAG 無しで起動し、root は SD、LAN で SSH に入れ、SSH で kernel を差し替えて再起動できる。
- この WS の外: SMP の CPU1・L2（別の WS）、USB（WS287）、PL（WS286 p008）。

- 2026-10-23 ユーザー「rpi4を参照するよりも、共通部分は分離しましょう。」→ SDHCI と Cadence GEM の共通の部分を汎用の driver に分け、Pi 4・Pi 5・ZYBO は薄い glue にする（分離は ZYBO の glue の前の Phase、Pi 側の回帰の確認付き）。

## Phase（[design.md](design.md) 版 2 の §9。2026-10-23 ユーザー「rpi4を参照するよりも、共通部分は分離しましょう。」で p002・p003 を共通化の Phase にした）

| Phase | 内容 | LW | 依存 | 状態 |
| --- | --- | --- | --- | --- |
| [p001](phase001/phase.md) | 調べと設計（[design.md](design.md)） | 10 | WS286 p005 | in-progress（版 2 = review の全指摘に対応、Q1 の判定待ち） |
| p002 | 汎用の SDHCI の core（`src/drivers/mmc/sdhci.c`）を rpi4-sdhci.c から分離、rpi4 は glue、host 試験、T1 の raspi4b で Pi 4 の SD の回帰 | 10 | p001 | planning |
| p003 | 汎用の Cadence GEM の core（`src/drivers/ethernet/cadence-gem.c`、32/64 bit の descriptor）を rp1-ethernet.c から分離、rp1 は glue、host 試験、実機の Pi 5 で SD と Ethernet の回帰（T1、kernel は p002 を含む） | 14 | p001、p002（実機の kernel に含める） | planning |
| p004 | ZYBO の SD（SDIO0 の glue、SLCR、CSD）と root を SD の partition に、SD の image の tool、QEMU の `--sd` | 12 | p002、WS286 p006、WS286 p010 | planning |
| p005 | ZYBO の LAN（GEM0 の glue、RTL8211E、SLCR の tx clock、`mac0=`）、DHCP・SSH（net.conf の en0、openssh） | 14 | p003、p004、WS286 p007、Q1: armv7 の package | planning |
| p006 | SD からの起動（SPL の Falcon の BOOT.BIN、FIT の `vmunix.itb` ＋ `system.dtb`、`make sd-image`、`deploy.sh`、JP5 はユーザー） | 12 | p004、p005、U1・U2 | planning |
| p007 | 規約の全文の見直し | 6 | p002〜p006 | planning |

- 合計 78 LW（版 2）。WS286 との順（design §9）: WS286 p006 → p007 → **WS289 p002・p003（WS286 と独立、並行可）** → WS286 p010 → WS289 p004（SD）→ p005（LAN）→ p006（SD 起動）→ WS286 p008（PL）。
- 訂正（design §1.2）: 目的の節の「reset は gpio0 46」は誤りで、`gpio0 46` は USB の PHY の reset。GEM の RTL8211E に reset の GPIO は無い（MDIO の soft reset で足りる）。

## 現在の状態・再開点

- 2026-10-23: 作成。p001 の設計から。
- 2026-10-23 P1（worktree p1-ws289-p001）: design.md 版 1（§0〜§14）と phase001。ユーザーの決定（共通部分の分離、Q1 経由）を D1・D2・p002・p003 に反映。design-reviewer（Opus）の Critical 1・High 1・Medium 7・Low 15 の全部に対応して版 2（§15 の表。主な変更: SD の kernel は legacy の uImage でなく FIT、SD と ramdisk の両方が有る時は initrd の有無で boot device を決める）。再開点: Q1 の判定、U1〜U7、Q1 への依頼（design §11）。
