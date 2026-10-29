<!-- awesome-plan project=zedbsd record=ws235-p006 -->

# ws235-p006 RTL8127 の拡張: profile で driver と stack の律速を分け、多 vector の MSI-X・RSS・割り込みの抑え・RX の zero copy

- 親: [WS235](../ws.md)。
- 状態: in-progress（2026-10-29 P1、q1669-i04）。
- 依存: [p005](../phase005/phase.md)。checksum・TSO の offload は WS221 p010 の口ができてから（この Phase の外）。

## 範囲（Q1 の依頼 q1669-i04）

- まず profile で driver と stack の律速を分ける。目標は Linux の値 send 6808・recv 5458 Mbit/s と PCIe Gen3 x1 の約 7 Gbit/s。律速が stack なら記録して Q1 へ
  （WS221 p010 は P3 の物、触らない）。
- 多 vector の MSI-X（pcat の PCI driver の側、他の driver も使える共通の口、HAL は変えない）・RSS と複数の RX queue・割り込みの抑え・RX の zero copy。
- 受け入れ（ユーザーの決定 ③、p001 の判断 3）: 「driver が律速でないことを profile で示し、tcp-perf の両向き」。7 Gbit/s への残りは WS221 p010。

## 読みの結果（2026-10-29 09:2x P1、main c7d4e42da の code）

- stack は 1 本の thread: `src/kern/net/core.c` の `network_worker` が driver の poll（`poll_devices`、device ごとに 16、全体で 32）・`ethernet_input`（IP・TCP）・
  TCP の timer と ACK の flush を同じ thread で回す。`net_device_receive` は input の queue に積むだけ。
- amd64 の MSI・MSI-X は全て BSP（CPU 0）宛て（`src/hal/amd64/irq.c` の `hal_irq_register_msi`、`hal_irq_set_affinity` は MSI に `HAL_ERR_UNSUPPORTED`）。
  → RSS・複数の RX queue・多 vector は、今の stack と HAL の実装では別の CPU に処理を分けない（並列化は stack の側と HAL の実装の補完が要る。Q1: 実装の補完は承認不要、
  hal.h の契約を変える時だけ理由を添えて Q1 へ。profile が先）。
- Linux の r8169（git.kernel.org の torvalds/linux.git の `drivers/net/ethernet/realtek/r8169_main.c`、2026-10-29 に GET で取得、GPL、値と振る舞いの観察だけに使う）:
  RTL8125 系の IntrMitigate（0xe2）の coalesce の表は 1000M 以下の速さだけ（10G は表に無い）で、10G では NAPI（drain まで割り込みを閉じる）に頼る。
  RX は `napi_alloc_skb` に copy（zero copy ではない）。TSO・SG・checksum の offload を使う。

## profile の道具

- `CONFIG_DRIVER_PCI_RTL8125_PROFILE`（Makefile、既定 n、診断）: driver の割り込み・poll・transmit の回数と時間（`kern_rtc_read_counter` の差）を数え、
  service の thread が frame の動く間 1 秒ごとに `rtl8125: rge0 profile ms … irq N us poll N full frames us tx N copies refusals us`（累計）を log に出す。
  poll の時間は stack の input queue への受け渡しを含み、stack の IP・TCP の処理は含まない。
- `plan/ws235/tests/config-amd64-rtl8127-profile.mk`、`rtl-link.sh` の `PROFILE=1`（各向きの前後に ssh で `sysctl hw.cputimes` と `dmesg` の最後の profile の行）、
  `plan/ws235/tests/profile-summary.py`（driver の CPU の割合・frame あたりの時間・CPU ごとの負荷）。判定は ssh の読みで、console・serial の log は読まない。

## 実装（2026-10-29 P1）

| commit | 内容 |
| --- | --- |
| a580715fe | profile の計上（config で有効）、試験の config・`PROFILE=1`・`profile-summary.py` |
| 3e2087929 | pcat の `allocate_irqs` が MSI-X で min〜max 本（table の大きさまで、vector i に entry i）、`pci.c` が device ごとに MSI-X の vector を数え、最後の 1 本の disestablish で capability の control を戻す。`pci.h` の説明。既存の呼び手は全て 1〜1 本 |
| fe85b1da9 | poll が budget を使い切った時は IMR を開けない（受けの ring が空になるまで割り込みを閉じたまま） |

## 記録

## 判定
