<!-- awesome-plan project=zedbsd record=ws256-p024 -->

# ws256-p024 PCIe1（M.2）と NVMe の実機、NVMe の root

- 親: [WS256](../ws.md)。設計: [design-peripherals.md](../design-peripherals.md) §3.11（§3.5 の PLDA、§3.6 の NVMe の INTx）。
- 状態: in-progress（2026-10-26 05:59 Q1、q1587）
- 依存: p017（NVMe の INTx、cleared）・p020（PLDA の PCIe0 と VL805、cleared）・p015（SD の root）。

## 指示（2026-10-26 ユーザー）

「ssh kei@10.0.30.2 password "kei" これでVF2のzedBSDにログインできますので、サブエージェントを立てて、PCIe1を実装してください。現在はNVMeが接続されており、破壊的に読み書きしてOKです。USB serialも接続されています。既存WSで作業できならそうしてください。」

## 範囲

1. PCIe1（M.2、configuration の窓 0x9_c000_0000、STG の group 0x1f8）を実機で立ち上げる: `vf2-pcie.c`・`pci-plda.c` の PCIe1 の道（clock・reset・pin・PHY・PERST#・link）、bus の番号と BAR の割り当てが PCIe0（VL805）と衝突しない事。
2. M.2 の NVMe SSD: 識別（identify）、namespace、block の読み書き（破壊してよい、ユーザー許可）、読み書きの一致。INTx の道（MSI は使わない、§3.6）。U-Boot の `nvme scan` で動いた状態から PERST の後に attach できる事（`CC.EN`）。
3. 余裕があれば §3.11 の NVMe の root（pcat の形の `boot0=UUID=`、`probe_fixed_disks`、`wait_for_boot_disk`）。

## 確かめ方（実機）

- SSH（`sshpass -p kei ssh kei@10.0.30.2`、sudo は `echo kei | sudo -S`）。kernel は `/boot/boot0/vmunix` を差し替え（古い物は `/boot/boot0/vmunix.old`）、`sync` の後に SwitchBot の plug で電源を入れ直す（`python3 -I plan/tools/switchbot.py cycle AC276E42B252`、3 分 SSH が戻らなければもう一度）。
- USB serial（host の /dev/ttyUSB0）: ユーザーの「USB serialも接続されています」で、この Phase は生きた serial を対話で読んで判定・解析してよい（WS286・WS288 と同じ形）。

## 受け入れ

- PCIe1 の link が上がり、NVMe が列挙される。PCIe0 の VL805（USB keyboard）も今までどおり。
- NVMe の識別、書いて読んだ data の一致（大きさと回数を記録）。
- 証拠は実機（SSH・serial）だけ。QEMU の回帰は要らない（変えた所の host の試験と build の warning 0）。

## 記録

- 2026-10-26 05:59 Q1: ユーザーの指示（上）で開始。WS256 の p024（planning）を使う（既存の WS）。
- 2026-10-26 06:01 ユーザー「VF2はSwitchbotに接続されていて、再起動したいときにON OFFは制御を自動でお願いします」→ P2 は再起動が要る時に聞かずに `python3 -I plan/tools/switchbot.py cycle AC276E42B252` で電源を入れ直す（前に sync、3 分 SSH が戻らなければもう一度。2026-10-20 の許可と同じ）。
- 2026-10-26 06:05 P2 途中（申告、SSH で実機）: 板の今の kernel（5c83c16、2026-10-21）で PCIe1 と NVMe は既に動く（p020 の code が 2 つの host を立てる）。lspci 0001:00:00.0 1556:1111、0001:01:00.0 15b7:5041（WD の NVMe）。板の /var/log/kernel.log: 「pcie1: firmware left the link down」→「pcie1: link up」、BAR0 38000000（16 KiB）、「nvme: ... 0001:01:00.0 version=10400 queue=64 INTx」。namespace 1953525168 × 512 B（1 TB）。PCIe0 の VL805（1106:3483）は変わらず。破壊的な読み書き: 新 plan/ws256/tests/nvme-rw/rw.sh、3 回 × 7（512 B・4 KiB・64 KiB・100 KiB（max-transfer をまたぐ）・1 MiB・64 MiB・末尾の 1 MiB）の 21 件が cmp で一致、末尾の 1 block 先の書き込みは拒否。次: main の VF2 の kernel で再確認（電源の入れ直しの後の pattern も）、serial で U-Boot の pci enum; nvme scan の後の attach（CC.EN=1）、余裕があれば NVMe の root、WS288 の KC705 の注記。
