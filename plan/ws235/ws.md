<!-- awesome-plan project=zedbsd record=ws235 -->

# WS235: Realtek RTL8127・8125・8126・8122・8121・8120 の共通の Ethernet driver（まず PCIe の RTL8127 10GbE）

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-29 開き直し、P1 が RDNA 機の PCIe の RTL8127 から。下の 2026-10-29 の決定。p001〜p008 cleared（11:1x）。WS の受け入れ（server の config で有効にする事はユーザーの判断）は Q1）
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

- 2026-10-29 07:0x P1（q1669-i02、worktree p1-ws235b）: p001 cleared（判断の 5 件の回答は下の「p001 の判断」と phase001 の末尾）。p002・p003 は code と build まで。
  2026-10-29 07:20〜07:30 の RDNA 機の枠で 3 回: attach・link（2.5G の switch）・DHCP・LAN 越しの ping 200/200 は成立、ssh は未成立（下の p004）。
  次は RTL8127 の 10G の switch への移設の後の枠で `LANTEST=2 rtl-link.sh`（ue0 を落として rge0 だけで ssh・転送）。
- 2026-10-29 08:3x P1（q1669-i03、worktree p1-ws235c）: ユーザー「移設完了、起動済みです。」（RTL8127 は 10GbE の switch）。枠は P2 の次（Q1 の知らせ待ち）。
  用意: kernel `build/ws235/amd64-c/vmunix`（main 7b297b46a、cksum 4251467738）、`rtl-link.sh` の LANTEST=2 は ue0 の connected route も消す
  （閉じた device の route が残り、返事が ue0 に向かうため）、`PERF=` で tcp-perf の両向き、`POWERCYCLE=1`。Linux の基準は `linux-base.sh survey|install|measure`。
  枠の中の順: `POWERCYCLE=1 LANTEST=2 PERF=build/ws235/amd64-c/bin/tcp-perf sh plan/ws235/tests/rtl-link.sh build/ws235/amd64-c/vmunix build/tmp/ws235-link-4`
  → 通れば `linux-base.sh survey`（install・measure は時間が残れば）。
- 2026-10-29 09:1x P1（q1669-i03）: 08:57〜09:10 の枠で、rge0 が 10GbE の switch と 10000Mbps、rge0 だけで ping・ssh・100 MB 両向き・reboot、
  tcp-perf 20 s send 3539・recv 3373 Mbit/s（errors 0）。Linux の基準（backports 7.2.11・r8169、PCIe 8GT/s x1）send 6808・recv 5458 Mbit/s。
  p002〜p005 を cleared と記録（[p005](phase005/phase.md)）。機械は Debian 6.12.111（既定）、NVMe の kernel は P2 の物に戻した。
  次は p006（profile で driver と stack の律速を分け、多 vector の MSI-X・RSS・割り込みの抑え・zero copy）。server の config で有効にする事は p005 の後にユーザーに頼む（Q1）。
- 2026-10-29 11:0x P1（q1669-i04、worktree p1-ws235d）: p006・p007 cleared。profile（`CONFIG_DRIVER_PCI_RTL8125_PROFILE`）で、TX の割り込みを外して send の driver の CPU を
  68% → 15% にしても send は変わらず、recv は driver 30%・CPU の飽和無し・NIC の取りこぼし 0 → 律速は stack（WS221 p010）。既定の config で send 4935・recv 3360 Mbit/s。
  多 vector の MSI-X は pcat の共通の口まで（driver は 1 vector、RSS・zero copy は入れない、理由は [p006](phase006/phase.md)）。p007 で 8125・8125B・8125D・8126 の EPHY・PHY の表。
  次は p008（規約の全文の見直し）。機械は Debian 6.12.111、NVMe の kernel は P2 の vmunix-va1。
- 2026-10-29 11:1x P1（q1669-i04）: p008 cleared（[phase008](phase008/phase.md)）。全 Phase cleared。残り: WS の受け入れの確かめ（Q1）、server の config で有効にする事（ユーザー）、
  見直しの後の実機の非退行（未実施）。
- 2026-10-29 11:4x P1（q1669-i05、worktree p1-ws235e）: WS の受け入れの最後の確かめ（[phase008](phase008/phase.md) の「WS の受け入れの最後の確かめ」）。見直しの後の tree（main 054afe9a9）で
  試験の config と server の config（`CONFIG_DRIVER_PCI_RTL8125 := y`）の kernel を build（warning 0）、10.0.10.3 で `LANTEST=2` の rtl-link.sh: 両方で rge0 10000Mbps・ping 200/200・ssh・
  tcp-perf 30 s（試験の config send 5642・recv 3311、server の config send 4743・recv 3364 Mbit/s）・errors 0。機械は Debian 6.12.111、NVMe の kernel は P2 の vmunix-va1。
  残り: WS を完了の形に書き直す事（Q1）。

## Phase（2026-10-29 P1 が p001 でユーザーの順に直した。旧の表と見積もり（2026-10-14 Q1）は git の履歴に）

順はユーザーの「まずRTL81xxのドライバを、他のOSを参考にして作成して動くようにしてから、RTL8127の拡張部分を実装する」: p002〜p005 が RTL81xx の共通の driver で RTL8127 を
動かす段、p006 が RTL8127 の拡張。見積もりは 1 LW ≈ 15〜25 分。

| ID | 内容 | 見積もり | 状態 | 依存 |
| --- | --- | --- | --- | --- |
| [ws235-p001](phase001/phase.md) | 調べと設計: 10.0.10.3 の観測、rge（OpenBSD・FreeBSD）の作りが RTL8127 に当たるか、firmware と license、zedBSD の口、Phase の表 | 4 LW | cleared（2026-10-29 07:0x P1、q1669-i02。判断の 5 件の回答を記録） | — |
| [ws235-p002](phase002/phase.md) | 共通の骨格: PCI の ID、BAR2、bus master、版の判定（TXCFG）、MAC・PHY・CSI・EPHY の OCP の窓、reset、MAC address、1 vector の割り込み、net device の登録、`CONFIG_DRIVER_PCI_RTL8125`、試験の config `plan/ws235/tests/` | 6 LW | cleared（2026-10-29 09:1x P1、q1669-i03。版の値は ssh で読めず未確かめ） | p001 |
| [ws235-p003](phase003/phase.md) | MAC・PHY の初期化と link（共通の手順＋RTL8127 に要る MCU・EPHY・PHY の表）: firmware 無しから試し、要れば zedBSD-firmware の手順で（Q1 に先に）。auto-negotiation、link の報告（両側） | 10 LW | cleared（2026-10-29 09:1x P1、q1669-i03。firmware 無しで 2.5G・10G の link） | p002 |
| [ws235-p004](phase004/phase.md) | RX・TX の ring（v1・v2 の descriptor）、1 queue・1 vector、net の worker への接続（offload 無し） | 8 LW | cleared（2026-10-29 09:1x P1、q1669-i03。ssh・TCP の両向き、errors 0） | p002 |
| [ws235-p005](phase005/phase.md) | 実機（共通の段）: link・IPv4（static か DHCP）・ping・ssh・TCP の転送の計測と直し、同じ機械の Linux の基準 | 6 LW | cleared（2026-10-29 09:1x P1、q1669-i03。zedBSD send 3539・recv 3373、Linux 6808・5458 Mbit/s） | p003・p004 |
| [ws235-p006](phase006/phase.md) | RTL8127 の拡張: 多 vector の MSI-X、RSS と複数の RX queue、割り込みの抑え、RX の zero copy、約 7 Gbit/s（Linux の値）に近づける。律速が stack なら記録して WS221 と組で Q1 へ。checksum・TSO の offload は、stack の口（WS221 p010、P3 が設計）ができたら driver の側を足す | 10 LW | cleared（2026-10-29 11:0x P1、q1669-i04。driver は律速でない、send 4935・recv 3360。driver は 1 vector、RSS・zero copy は入れない） | p005。offload は WS221 p010 の口 |
| [ws235-p007](phase007/phase.md) | 他の chip の版の表と差の hook（8125・8125B・8125D・8126。実機は未確かめ。8122・8121・8120 は版の値が未確認） | 4 LW | cleared（2026-10-29 11:0x P1、q1669-i04。EPHY・PHY の表、他の chip は未確かめ、8122・8121・8120 は版の値が無く入れない） | p003 |
| [ws235-p008](phase008/phase.md) | 規約の全文の見直し | 2 LW | cleared（2026-10-29 11:1x P1、q1669-i04。書き方だけ、build warning 0、実機の非退行は未実施） | 全て |

- RTL8122・8121・8120 は未実装: 版の値（TXCFG）が Linux の r8169（2026-10-29 の torvalds/linux.git）にも FreeBSD rge にも無く、版の表に入れられない（[p007](phase007/phase.md)）。
- server の build の config（`config/ci/config-amd64-server.mk`、ユーザーの物）: 2026-10-29 ユーザー「コンフィグはあなたが書き換えてOKです。」→ Q1 が `CONFIG_DRIVER_PCI_RTL8125 := y` を足した（vmunix の build は rc 0・warning 0）。担当は変えない。
- 同じ機械の Linux の値は下の「p001 の判断」の 5（backports の kernel）。

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
  - **訂正**（2026-10-29 07:3x）: ユーザーの回答（Q1 経由、クリック）「2.5G の switch」→ RTL8127 の cable は 2.5G の switch に繋がっていた（centris の enp94s0f1 との直結ではない。
    上の「centris の 10GbE に直結」は誤り）。P1 の観測（p004 の kernel）でも rge0 が LAN の DHCP で 10.0.30.6/16 を取り、media 2500Mbps、enp94s0f1 は link 無し。
  - ユーザー（原文、2026-10-29、Q1 経由）: 「もしよかったら、10GbEスイッチに移設します。電源を切ってもいいですか？」→ P1 は 07:30 に機械を Debian にして返した。
    移設の後の経路（10G の switch）での試験の枠は Q1 が知らせる。10GbE の計測（p005 の受け入れ・WS221 p010）にはこの 10G の経路が要る。

## p001 の判断（2026-10-29、Q1 経由のユーザーのクリック。原文と解釈を分ける。詳しくは [p001](phase001/phase.md) の「判断の回答」）

1. ユーザー（原文）: 「別の mirror も可」→ Q1 の解釈: FreeBSD の `if_rge_microcode.h`・`if_rge_hw.h` は FreeBSD の公式の mirror（git の clone を含む）から取ってよい。GET・clone で、拒否・制限に当たったら止める。
2. ユーザー（原文）: 「WS235 に入れる (推し)」→ Q1 の解釈: 複数の MSI-X の vector は p006 で、pcat の PCI の driver の変更、他の driver も使える共通の口、HAL は変えない（HAL を変える必要があれば理由を具体的に Q1 へ）。
3. ユーザー（原文）: 「それでよい (推し)」→ Q1 の解釈: WS235 の受け入れは driver が律速でない事の profile と両向きの tcp-perf。7 Gbit/s までの残りは WS221 p010。
4. Q1 の判断（ユーザーの回答ではない）: RTL8122・8121・8120 を目標に残す（p007 で版の表と差の hook まで、実機は未確かめ）。
5. ユーザー（原文）: 「backports の kernel (推し)」→ Q1 の解釈: Linux の基準は 10.0.10.3 の Debian に backports の新しい kernel と firmware-realtek を入れ、起動の既定は今の kernel のまま、計測の時だけ backports で起動。導入は sudo で、時間の枠の中で。
- ユーザーの回答（クリック、2026-10-29）: RTL8127 の cable は「2.5G の switch」。→ 前の「centris の 10GbE に直結」は誤り（訂正）。今の link は LAN の 2.5G の switch と 2500 Mbps。10GbE の測り（WS235 の受け入れ・WS221 p010）には 10G の経路（centris の enp94s0f1 との直結など）が要る、つなぎ直しはユーザーの判断。
