<!-- awesome-plan project=zedbsd record=ws235 -->

# WS235: Realtek RTL8127・8125・8126・8122・8121・8120 の共通の Ethernet driver（まず PCIe の RTL8127 10GbE）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-29 開き直し、P1 が RDNA 機の PCIe の RTL8127 から。下の 2026-10-29 の決定。p001 in-progress）
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

## Resume point

- 2026-10-29 P1（q1669-i01）: p001 の調べと設計を [phase001/phase.md](phase001/phase.md) に。design-reviewer の指摘の反映と Q1 の判定の待ち。次は p002（共通の骨格）。

## Phase（2026-10-29 P1 が p001 でユーザーの順に直した。旧の表と見積もり（2026-10-14 Q1）は git の履歴に）

順はユーザーの「まずRTL81xxのドライバを、他のOSを参考にして作成して動くようにしてから、RTL8127の拡張部分を実装する」: p002〜p005 が RTL81xx の共通の driver で RTL8127 を
動かす段、p006 が RTL8127 の拡張。見積もりは 1 LW ≈ 15〜25 分。

| ID | 内容 | 見積もり | 状態 | 依存 |
| --- | --- | --- | --- | --- |
| [ws235-p001](phase001/phase.md) | 調べと設計: 10.0.10.3 の観測、rge（OpenBSD・FreeBSD）の作りが RTL8127 に当たるか、firmware と license、zedBSD の口、Phase の表 | 4 LW | in-progress（2026-10-29 P1） | — |
| ws235-p002 | 共通の骨格: PCI の ID、BAR2、bus master、版の判定（TXCFG）、MAC・PHY・CSI・EPHY の OCP の窓、reset、MAC address、1 vector の割り込み、net device の登録、`CONFIG_DRIVER_PCI_RTL8125`、試験の config `plan/ws235/tests/` | 6 LW | planned | p001 |
| ws235-p003 | MAC・PHY の初期化と link（共通の手順＋RTL8127 に要る MCU・EPHY・PHY の表）: firmware 無しから試し、要れば zedBSD-firmware の手順で（Q1 に先に）。auto-negotiation、link の報告（両側） | 10 LW | planned | p002 |
| ws235-p004 | RX・TX の ring（v1・v2 の descriptor）、1 queue・1 vector、net の worker への接続（offload 無し） | 8 LW | planned | p002 |
| ws235-p005 | 実機（共通の段）: link・IPv4（static か DHCP）・ping・ssh・TCP の転送の計測と直し | 6 LW | planned | p003・p004 |
| ws235-p006 | RTL8127 の拡張: 多 vector の MSI-X、RSS と複数の RX queue、割り込みの抑え、RX の zero copy、約 7 Gbit/s（Linux の値）に近づける。律速が stack なら記録して WS221 と組で Q1 へ。checksum・TSO の offload は、stack の口（WS221 p010、P3 が設計）ができたら driver の側を足す | 10 LW | planned | p005。offload は WS221 p010 の口 |
| ws235-p007 | 他の chip の版の表と差の hook（8125・8125B・8125D・8126。実機は未確かめ。8122・8121・8120 は版の値が未確認） | 4 LW | planned | p003 |
| ws235-p008 | 規約の全文の見直し | 2 LW | planned | 全て |

- server の build の config（`config/ci/config-amd64-server.mk`、ユーザーの物）で有効にするのは、p005 の後にユーザーに頼む（担当は変えない）。
- 要るユーザーの判断（Q1 へ、p001 の「同じ機械の Linux の値」）: 10.0.10.3 の Debian で RTL8127 を動かす方法（新しい kernel＋linux-firmware の rtl8127a-1.fw か、Realtek の driver）。

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
    食い違いは未確認として [p001](phase001/phase.md) に。X540 の広告は 100baseT・1000baseT・10000baseT（2.5G・5G は無い）。
