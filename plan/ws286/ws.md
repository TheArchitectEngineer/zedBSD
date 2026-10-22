<!-- awesome-plan project=zedbsd record=ws286 -->

# WS286 Digilent ZYBO（初代、Zynq Z-7010）への移植: ARMv7-A の HAL・BSP・driver

- Master: [plan/master.md](../master.md)（2026-10-23 作成）。Primary Milestone: MG008（HAL の移植と異種機）。
- Status: planning
- 出典: 2026-10-23 ユーザー「次の計画です。DIGILENT ZYBOボードにzedBSDを移植します。Zynq Z-7010のモデルです。」「初代 ZYBO（Z-7010）です。画面はカーネルの移植後に、RTLを実装します。」「FPGAのツールチェインは私の方でホストにインストールします。ひとまずarmv7のHALと、このボードのBSP、ドライバを実装しましょう。JTAGからのブートはできたらうれしいものの、まずはSDブートで、SSHでカーネルを更新して、Switchbotで再起動、というのがいいですね。PLのプログラミングは、SSH経由でできるようにできそうですか？」

## 目的と完了の条件（案）

- 新しい arch: ARMv7-A（Cortex-A9 MPCore、VFPv3・NEON）の HAL と toolchain・sysroot。
- ZYBO の BSP と PS の driver: UART（Cadence）、SD（SDHCI）、LAN（Cadence GEM ＋ RTL8211E）、USB（ChipIdea ＋ ULPI）、SLCR（clock・reset）、PL の書き込み（devcfg の PCAP、SSH から bitstream を書く道具）。
- 起動: SD から（FSBL か U-Boot の SPL、license を確かめる）。JTAG の起動はできれば後で。
- 完了: SD から起動して login、LAN で SSH、kernel を SSH で替えて SwitchBot で再起動できる。画面（PL の RTL）は移植の後の別の段。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 調べと設計（[design.md](design.md) 版 1。§12 の JTAG 先行の決定は未反映、別の担当に引き継ぎ） | — | in-progress |

## 現在の状態・再開点

- 2026-10-23: 作成。p001 の設計から。
- 2026-10-23: 実機の接続。ZYBO の USB（FT2232H、Digilent Adept、serial 210279785182）を host に接続: /dev/ttyUSB0 = if00（JTAG）、/dev/ttyUSB1 = if01（UART、115200、console）。電源は SwitchBot のプラグ AC276E42B252（VF2 から付け替え、ユーザー「AC276E42B252をプラグします。認識の通りです。」）。boot mode の jumper は今は JTAG。host の user awe を dialout に追加（次の login から sudo 無しで読める）。
- 2026-10-23: OSS だけで進める（ユーザー）。openocd・openFPGALoader を host に導入、`sudo openFPGALoader -c digilent --detect` で Cortex-A9 の DAP と xc7z010 を確認。ZYBO は USB 給電、SwitchBot は未接続（JTAG で reset する）。
- 2026-10-23 P1: design.md 版 1（§0〜§11）。ユーザーの決定（JTAG の起動を SD より先、OSS だけ、SwitchBot は未接続）は design §12 に原文で置き、反映は引き継ぎの担当（§4・§8・§10 の書き直し）。
