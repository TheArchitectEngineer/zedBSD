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

## Phase（案、p001 の設計で確定）

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 調べと設計（SDHCI と GEM の共通化の範囲、SD の配置と BOOT.BIN、boot mode の jumper と電源の扱い、QEMU zynq の SD・GEM の使い方） | WS286 p005 | planning |
| p002 | SD の driver（SDIO0、読み書き、partition）と root を SD に | WS286 p006（task・割り込み）、p001 | planning |
| p003 | LAN の driver（GEM0 + RTL8211E）、DHCP・SSH | WS286 p007（userland）、p001 | planning |
| p004 | SD からの起動（BOOT.BIN、kernel を SD から、SSH で更新して再起動） | p002、p003、WS286 p010 | planning |
| p005 | 規約の全文の見直し | p002〜p004 | planning |

- WS286 との順（Q1 の案）: WS286 p006 → p007 → WS289 p002（SD）→ WS286 p010（login。root は ramdisk でなく SD を主に）→ WS289 p003（LAN）→ p004（SD 起動）→ WS286 p008（PL）。

## 現在の状態・再開点

- 2026-10-23: 作成。p001 の設計から。
