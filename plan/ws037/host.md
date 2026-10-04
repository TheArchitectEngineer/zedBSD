<!-- awesome-plan project=zedbsd record=ws037-host -->
# nvrtx の試験機（RTX 2070）の情報

2026-10-04 ユーザー「RTX 2070の実機があり、そのホストをあとで伝えるので、それさえわかれば作業を開始できるように」。**ユーザーから host を聞いたら、Q1 が下の「ユーザーに聞く」を埋め、p002 の 0 段（読むだけの調査）で「調べて埋める」を埋める。** 埋まるまで p002 以降を始めない（p001 は host 無しで進められる）。

## ユーザーに聞く（Q1 が記入）

| 項目 | 値 | 備考 |
| --- | --- | --- |
| host の名前・IP | （未記入） | centris から ssh で届くか |
| ssh の利用者・鍵 | （未記入） | centris の `~/.ssh/config` に alias（例 `nvrtx-host`）を Q1 が作る |
| OS | （未記入） | Linux（版）か、それ以外 |
| sudo | （未記入） | password 無しか |
| 試験の方式（下の A・B） | （未記入） | A（USB から zedBSD を素で起動）・B（host の Linux の上の QEMU に VFIO で渡す）・両方 |
| RTX 2070 の出力につながる monitor | （未記入） | DP・HDMI のどれ。段の印を見るのに要る |
| 画面の撮り方 | （未記入） | HDMI の capture の器具、ユーザーの写真、無し |
| 強制の電源の切り方 | （未記入） | ユーザーの手、smart plug、IPMI |
| host 自身の画面 | （未記入） | 別の GPU（iGPU）か。B ではRTX 2070 を host から外すので要る |
| 使ってよい時間・lock | （未記入） | 例 centris の `/tmp/nvrtx-hw.lock`（i915 の `/tmp/i915-hw.lock` と同じ考え） |

## 調べて埋める（p002 の 0 段、読むだけ）

| 項目 | 値 | 調べ方 |
| --- | --- | --- |
| GPU の PCI の address と ID | （未記入） | `lspci -nn -d 10de:`。RTX 2070 は TU106（`10de:1f02`・`1f07`）。RTX 2070 SUPER は TU104（`1e84`・`1ec2`・`1ec7`）で、同じ手順の見込み（[design](nvrtx-design.md) 1.1） |
| 同じ GPU の他の function | （未記入） | TU106 の board は audio（.1）・USB の xHCI（.2）・UCSI（.3）を持つことがある。VFIO では全部を一緒に渡す |
| IOMMU の group | （未記入） | `/sys/kernel/iommu_groups/*/devices/` |
| IOMMU の有効 | （未記入） | kernel の command line の `intel_iommu=on`・`amd_iommu=on`、`dmesg` |
| 今の driver | （未記入） | `lspci -k`（nouveau・nvidia・vfio-pci） |
| VBIOS の版 | （未記入） | `/sys/bus/pci/devices/*/rom`（読めれば hash を記録、BLOB は repository に入れない） |
| UEFI・CSM | （未記入） | 素の起動（A）で GOP が出るか |
| QEMU・OVMF の版 | （未記入） | B のとき |
| reset の手段 | （未記入） | Turing は FLR が無いことが多い（**p001 で確かめる**）。bus reset・`reset_method` |
