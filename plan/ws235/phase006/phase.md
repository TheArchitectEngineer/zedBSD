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
| 58044230e | pcat の変更の書き方（関数の呼び出しを条件の外へ） |
| 8018d289b | IMR から TX_OK と TX_UNAVAILABLE を外す（TX の回収は transmit の半分の時と poll。poll は RX の割り込みと service の thread の 20 ms）。`rtl-link.sh` の `PROFILE=1` に top と centris の `/proc/net/snmp` |
| f9c8d0edd | profile の行に controller の tally counter の missed（descriptor が無くて落とした frame、16 bit）。`profile-summary.py` が読む |

## 記録

### 2026-10-29 09:54〜10:26 の RDNA 機の枠（lock「P1 q1669-i04 start 09:54 end 10:39」、10:26 に返した）

命令（worktree p1-ws235d、出力は `build/tmp/ws235-prof-N/`）:
`KEY=$PWD/plan/tmp/guest/id_ed25519 POWERCYCLE=1 LANTEST=2 PROFILE=1 SECONDS_PERF=20 PERF=build/ws235/amd64-p/bin/tcp-perf sh plan/ws235/tests/rtl-link.sh build/ws235/kN/vmunix build/tmp/ws235-prof-N`
（各回 4〜5 分。tcp-perf は k1 の build の物）。machine: 16 CPU、counter 2496000000 Hz、counter の 1 回の読み 40 ns（dmesg の profile on の行）。

| 回 | kernel（cksum） | send Mbit/s | send の driver（1 CPU 比） | recv Mbit/s | recv の driver（1 CPU 比） |
| --- | --- | --- | --- | --- | --- |
| 1 | k1: a580715fe＋3e2087929（4079549303） | 3568 | 71.7%（irq 31.4・poll 11.4・tx 28.9）、irq 99987/s、tx 0.94 us/frame | 3368 | 35.7%（irq 10.1・poll 18.7・tx 6.9）、irq 51527/s、poll 0.64 us/frame |
| 2 | k2: fe85b1da9（2854028322） | 3763 | 68.5%、irq 96861/s | 3233 | 31.5%、irq 44324/s |
| 3 | k3: k2＋TX_OK を外す（3875452462） | 2989 | 46.0%（irq 31.6%、TX_UNAVAILABLE が 94579/s 鳴った）、tx 0.26 us/frame | 3291 | 31.3% |
| 4 | k4: 8018d289b（1480656801） | 3547 | **10.5%**（irq 2.4・poll 3.1・tx 5.0）、irq 8996/s | 3553 | 30.3%（irq 5.5・poll 19.8・tx 5.1） |
| 5 | k4（top を足した） | **5505** | 17.7%、irq 10493/s | 3358 | 29.8% |
| 6 | k4 | **5405** | 15.2% | 3201 | 28.0% |
| 7 | k4（centris の snmp を足した） | 4519 | 16.1% | **4252** | 28.8% |

- CPU ごとの負荷（hw.cputimes の差 ÷ 20 s）: send の回 4 は cpu13 86%（他は 26% 以下）で 3547、回 5・6 は cpu0 84〜98%・cpu1 78〜95% の 2 つに集まって 5405〜5505。
  同じ kernel で 3547〜5505 と振れ、送りの thread と worker の置き場所（CPU）で変わる見込み（未確認、scheduler・stack の側）。
- recv はどの回もどの CPU も 100% に届かない（最大 cpu0 78%・cpu11 59%・cpu1 61% など）。driver は 1 CPU の 28〜36%（poll 0.59〜0.66 us/frame）。
- 回 7 の centris の `/proc/net/snmp`: recv の 20 s で OutSegs 7366641・**RetransSegs 866**、send は RetransSegs 0。zedBSD の rge0 の errors・dropped は 0
  （driver の `rx_dropped` は packet の不足と誤りの frame、stack の input queue の溢れは device の `rx_dropped` に数える）。取りこぼしの場所（NIC の ring の溢れか
  stack か、Linux の TLP の見かけの再送か）は未確認 → tally counter の missed（f9c8d0edd）と RX の ring 1024（k5b）を次の枠で。
- top（`top -b -n 2 -d 10`）は全ての process の %CPU が 0.0 で使えない（zedBSD の top の不具合か、未確認。この Phase では直さない）。
- 最後の状態（各回の script の照合と 10:25 の ssh）: Debian 6.12.111（saved_entry 6.12.111、next_entry 空）、RTX 01:00.0 は vfio-pci、NVMe の kernel は P2 の物（sha256 6a06e796a7b9b632…、一致）。電源の入れ直しは無し。
- 判定は ssh（ifconfig・sysctl hw.cputimes・dmesg の profile の行）と centris の perf-peer.py・/proc/net/snmp で。console・serial の log は読んでいない。

### 読み（2026-10-29 10:3x P1）

- send: TX の割り込みを外して driver の CPU を 68% → 10% に下げても、回 1（3568）→ 回 4（3547）で send は変わらず、driver の CPU が同じ 15〜18% の回 5〜7 で
  4519〜5505 と振れる。send の値は driver の CPU でなく、driver の外（送りの thread の CPU＝TCP の分割・checksum・user からの copy、と thread の置き場所）で決まる
  （置き場所との関係は CPU ごとの負荷からの見込みで未確認）。
- recv: CPU が飽和せず、相手が再送している。driver の CPU は 28〜30% で、全てを 0 にしても飽和していない律速は解けない。取りこぼしが NIC の ring なら driver の側（ring の大きさ・補充の速さ）、
  stack なら WS221 p010。次の枠の k5a（missed）・k5b（ring 1024）で分ける。

## 判定
