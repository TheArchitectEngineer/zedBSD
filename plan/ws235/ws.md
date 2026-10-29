<!-- awesome-plan project=zedbsd record=ws235 -->

# WS235: Realtek RTL8127・8125・8126・8122・8121・8120 の共通の Ethernet driver（まず PCIe の RTL8127 10GbE）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-29、Q1 の判定）
Primary Milestone: MG006
Parent: [Master](../master.md)
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-14）

「このホストには、RTL8127という10GbE NICがついています。RTL8127/8125/8126/8122/8121/8120に共通のドライバを作りつつ、PCIeの8127が動くようにしたいです。できそうですか？WS作成と見積もりをお願いします。まだ実施はしません。」

## 目標

- `src/drivers/ethernet/` に、Realtek の 2.5G〜10G の PCIe の NIC の系列（RTL8125・8126・8127・8122・8121・8120）で共通の driver を作る。chip の差は版の表と chip ごとの hook に閉じ込める。
- 試験機（2026-10-29 から RDNA 機 10.0.10.3、Debian と zedBSD、相手は centris の X540 `enp94s0f1` に直結）の PCIe の RTL8127 で、link が上がり、IPv4 の通信と、PCIe の上限（8GT/s x1、約 7 Gbit/s。同じ機械の Linux の値が取れればそれ）に見合う転送ができる。
- 他の chip は版の判定と表まで（実機が無いので未確かめと明記）。
- server の build の config（`config/ci/config-amd64-server.mk`、ユーザーが作った物）で有効にする。

## 結果

- `src/drivers/ethernet/rtl8125.c`（`CONFIG_DRIVER_PCI_RTL8125`）: RTL8125 系の共通の driver（FreeBSD・OpenBSD の rge を参考、Linux r8169 の値は block ごとの出典）。版の判定（TXCFG）、MAC・PHY・CSI・EPHY の OCP の窓、firmware 無しの MAC・PHY の初期化と link、v1・v2 の descriptor の RX・TX の ring（1 queue・1 vector）、TX の割り込みを外して poll で回収。8125・8125B・8125D（2 版）・8126（2 版）・8127 の EPHY・PHY の表と PHY の patch mode。診断の profile（`CONFIG_DRIVER_PCI_RTL8125_PROFILE`、既定 n）。
- pcat の PCI の driver に多 vector の MSI-X の共通の口（`src/drivers/pci/pci-pcat.c`・`pci.c`・`include/drivers/pci/pci.h`、HAL は不変）。RTL8127 の driver は 1 vector で使う。
- 受け入れ（ユーザーの決定 ③「driver が律速でない事の profile と両向きの tcp-perf」）: 10.0.10.3 の RTL8127（PCIe Gen3 x1、10GbE の switch）で、TX の割り込みを外して driver の CPU を 68% → 15% にしても send は変わらず、recv は driver 30%・CPU の飽和無し・NIC の取りこぼし 0 → 律速は stack（WS221 p010 へ）。最後の確かめ（p008 の後の tree、2026-10-29 11:3x〜11:41）: 試験の config と server の config の両方で rge0 10000Mbps・rge0 だけで ping 200/200・ssh・100 MB 両向き・tcp-perf 30 s（試験 send 5642・recv 3311、server send 4743・recv 3364 Mbit/s）・errors 0。同じ機械の Linux（backports 7.2.11・r8169）は send 6808・recv 5458。
- server の config（`config/ci/config-amd64-server.mk`）で `CONFIG_DRIVER_PCI_RTL8125 := y`（2026-10-29 ユーザー「コンフィグはあなたが書き換えてOKです。」、Q1 が足した）。

## 制限・移管

- 約 7 Gbit/s への詰め（stack が 1 本の worker、MSI が全て BSP 宛て、RSS・複数 RX queue・zero copy は今の stack では効かない）は WS221 p010 へ。
- 8125・8125B・8125D・8126 は実機が無く未確かめ。RTL8122・8121・8120 は版の値（TXCFG）が Linux r8169 にも FreeBSD rge にも無く未実装。
- 未確かめ: jumbo frame、長時間の転送、switch 側の port、試験の config と server の config の send の差の原因。
- 試験の道具は `plan/tools/rtl8127/`（`rtl-link.sh`・`linux-base.sh`・`profile-summary.py`・試験の config、[plan/tools.md](../tools.md) に登録）。

## Phase（全て cleared、記録は git の履歴に）

p001 調べと設計 → p002 共通の骨格 → p003 MAC・PHY の初期化と link → p004 RX・TX の ring → p005 実機の共通の段と Linux の基準 → p006 RTL8127 の拡張（profile・MSI-X の共通の口） → p007 他の chip の表 → p008 規約の全文の見直し。

## ユーザーの決定（2026-10-29）

- 原文: 「WS221には、10GbEでのTCPスタックの改善を次のフェーズとして作成してお願いしたいです。それに向けて、P1は、RTL8159というPCIeの10GbEアダプタのドライバを書いてほしいです。RDNA機に刺さっています。それが完成したら、WS221に戻って、10GbEの限界を、RDNA機で試す流れにしたいです。」
- 原文（Q1 の問いへの答え）: 「PCIeのRTL8159です。半年くらい前に発売されたばかりで、あなたの学習データにはないでしょうが、Webを検索すれば出ますし、RDNAマシンを見ればわかります。」、WS309 は「止める」。
- 原文（web）: 「LinuxはやっとRTL8159がメインラインに入ったところだと聞きました。もしかしたらまだstagingかもしれないですが、あるそうです。Webから取得してOKです。サブエージェントにWebの取得(GET)を許可して下さい。GETはパラメータなしなら安全と見なしていいです。」
- 原文: 「これからPCIeボードを挿します。」（2026-10-29 05:3x 頃）
- Q1 の解釈: この WS を開き直し、最初の chip を RTL8127 から RTL8127（RDNA 機 10.0.10.3 の PCIe）に替える。p001 の調べで RTL8127 の系列（RTL8127 などとの差）を確かめ、Phase の表を直す。Linux の driver は GPL: guardrail のとおり値・表は block ごとの出典で使えるが、code・comment・構造は写さない（操作の手順の引用は WS039 にだけ明示の許可があるので、要れば Q1 経由でユーザーに聞く）。RDNA 機は P2（WS291）と時間の枠で分ける（plan/tools.md の「RDNA 機の時間の枠」）。
- 訂正（原文、2026-10-29）: 「すみません、チップ名を間違いました。RTL8127です。」→ 上の原文の「RTL8159」は RTL8127 の誤り。この WS の元の目標（まず PCIe の RTL8127）のまま、試験機は RDNA 機（10.0.10.3）。
- ユーザー（原文、2026-10-29）: 「いずれにせよ、製品化されてすぐなので、学習データにないと思います。」→ Q1: P1 は記憶に頼らず、RDNA 機の観測（lspci・Debian の driver の振る舞い）と web の GET で取った資料（Linux の driver の値、BSD の rge）から設計する。記憶からの推測は「未確認」と書く。
- Q1 の確かめ（2026-10-29 05:5x、10.0.10.3 の Debian 6.12.111、ssh で読みだけ）: `03:00.0 Ethernet controller [0200]: Realtek Semiconductor Co., Ltd. RTL8127 10GbE Controller [10ec:8127] (rev 05)`。Debian の kernel（6.12）にはこの device に bind する driver が無い（network の interface が出ない）。他に `04:00.0` RTL8111/8168（r8169、down）、USB の RTL8156（r8152、2.5G、up）、GPU は `01:00.0` TU106 RTX 2070 `[10de:1f02]`。
- ユーザー（原文、2026-10-29）: 「なお、RTL8127は、RTL8125などのドライバでも、マルチビットのNICとしてそのまま使えるらしいです。」→ Q1: p001 で、RTL8125 系の driver（BSD の rge など）の作りが RTL8127 にそのまま当たるかを最初に確かめる（当たるなら共通の driver の骨格を RTL8125 系の形にして、RTL8127 の差を hook に）。ユーザーの伝聞なので、観測で確かめるまで「未確認」。
- ユーザーの決定（原文、2026-10-29）: 「はい、なので、まずRTL81xxのドライバを、他のOSを参考にして作成して動くようにしてから、RTL8127の拡張部分を実装するのがいいと思います。付け加えて、このマシンはPCIeの帯域の制約があり、7Gbit/sくらいしか出ないです。」
  - Q1 の解釈: 順は (1) 他の OS（FreeBSD・OpenBSD の rge など）を参考に RTL81xx（RTL8125 系）の共通の driver を作り、RTL8127 を RTL8125 系の multi-gig の NIC として動かす → (2) RTL8127 の拡張の部分（10GbE の速さ、chip の固有の機能）を足す。Phase の表は p001 でこの順に直す。
  - 10.0.10.3 の PCIe の帯域の制約で、10GbE は約 7 Gbit/s が上限（ユーザーの情報）。WS235 p005 と WS221 p010 の受け入れの目標は、この上限（と同じ機械の Linux の値）に合わせる。
- ユーザーの回答（Q1 経由、2026-10-29 クリック）: RTL8127 の port の相手は「centris の 10GbE に直結」。Q1 の訂正: 直結の相手は centris の `enp94s0f1`
  （Intel X540-AT2、ixgbe。`enp94s0f0` は LAN の switch 側の 10G）。centris の側の設定は変えない（IP を付けるなどは先に Q1 に案を送る）、ethtool・ip の読みは自由。
  - 観測（2026-10-29 06:1x P1）: RTL8127 の PHYSTAT（MMIO 0x6c）は driver 無しで 100M full の link を示すが、centris の `enp94s0f1` は `Link detected: no`。
    食い違いは未確認として p001（git の履歴） に。X540 の広告は 100baseT・1000baseT・10000baseT（2.5G・5G は無い）。
  - **訂正**（2026-10-29 07:3x）: ユーザーの回答（Q1 経由、クリック）「2.5G の switch」→ RTL8127 の cable は 2.5G の switch に繋がっていた（centris の enp94s0f1 との直結ではない。
    上の「centris の 10GbE に直結」は誤り）。P1 の観測（p004 の kernel）でも rge0 が LAN の DHCP で 10.0.30.6/16 を取り、media 2500Mbps、enp94s0f1 は link 無し。
  - ユーザー（原文、2026-10-29、Q1 経由）: 「もしよかったら、10GbEスイッチに移設します。電源を切ってもいいですか？」→ P1 は 07:30 に機械を Debian にして返した。
    移設の後の経路（10G の switch）での試験の枠は Q1 が知らせる。10GbE の計測（p005 の受け入れ・WS221 p010）にはこの 10G の経路が要る。

## p001 の判断（2026-10-29、Q1 経由のユーザーのクリック。原文と解釈を分ける。詳しくは p001（git の履歴） の「判断の回答」）

1. ユーザー（原文）: 「別の mirror も可」→ Q1 の解釈: FreeBSD の `if_rge_microcode.h`・`if_rge_hw.h` は FreeBSD の公式の mirror（git の clone を含む）から取ってよい。GET・clone で、拒否・制限に当たったら止める。
2. ユーザー（原文）: 「WS235 に入れる (推し)」→ Q1 の解釈: 複数の MSI-X の vector は p006 で、pcat の PCI の driver の変更、他の driver も使える共通の口、HAL は変えない（HAL を変える必要があれば理由を具体的に Q1 へ）。
3. ユーザー（原文）: 「それでよい (推し)」→ Q1 の解釈: WS235 の受け入れは driver が律速でない事の profile と両向きの tcp-perf。7 Gbit/s までの残りは WS221 p010。
4. Q1 の判断（ユーザーの回答ではない）: RTL8122・8121・8120 を目標に残す（p007 で版の表と差の hook まで、実機は未確かめ）。
5. ユーザー（原文）: 「backports の kernel (推し)」→ Q1 の解釈: Linux の基準は 10.0.10.3 の Debian に backports の新しい kernel と firmware-realtek を入れ、起動の既定は今の kernel のまま、計測の時だけ backports で起動。導入は sudo で、時間の枠の中で。
- ユーザーの回答（クリック、2026-10-29）: RTL8127 の cable は「2.5G の switch」。→ 前の「centris の 10GbE に直結」は誤り（訂正）。今の link は LAN の 2.5G の switch と 2500 Mbps。10GbE の測り（WS235 の受け入れ・WS221 p010）には 10G の経路（centris の enp94s0f1 との直結など）が要る、つなぎ直しはユーザーの判断。
