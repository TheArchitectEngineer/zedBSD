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
