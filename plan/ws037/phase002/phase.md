<!-- awesome-plan project=zedbsd record=ws037-p002 -->

# ws037-p002: 試験機（RTX 2070）の調査と試験の道

Status: planned（host の情報待ち）
Disposition: normal
Parent: [WS037](../ws.md)
Queue: none（host がわかったら Q1 が作る）
依存: [host.md](../host.md) の「ユーザーに聞く」が埋まっていること。p001 と並走できる（p001 の試験の道の節を使う）。
実行者: phase-runner（host の操作は Q1 の許可の範囲で。権限で止まったら止まって Q1 に返す）

## 範囲

0. **読むだけの調査**（変更しない）: host.md の「調べて埋める」の表を全部埋める。`lspci -nn -d 10de:`・`lspci -k`・IOMMU の group・`dmesg | grep -i -e iommu -e nouveau -e nvidia`・`cat /proc/cmdline`・VBIOS の rom の hash。結果を host.md に書き、Q1 に返す（**方式 A・B の決めと、host を変える操作はユーザーの許可の後**）。
1. **方式 A（USB から素で起動）の道**: CI の amd64 の image に nvrtx の段の印の kernel を入れて USB に書く手順（`plan/tools/nvrtx/README.md`）。GOP の framebuffer に段の印が出るのを、ユーザーが目で（または capture で）確かめる。
2. **方式 B（VFIO）の道**（ユーザーが B を選んだとき）: `plan/tools/nvrtx/` に、
   - `gpu-mode.sh host|vfio`（RTX 2070 の全 function を vfio-pci に付け替える／戻す。5330 の `igpu-mode.sh` を手本）、
   - `nvrtx-hw.sh start|stop`（lock を取り、OVMF・GPU の rom で QEMU を起動。i915 の `hdmi-h4-hw.sh` を手本。guest の画面は RTX 2070 の出力の monitor）、
   - 撮った画面（QMP の screendump は GPU の画面を撮れないので、capture の器具かユーザー）。
3. **最初の起動**: 今の zedBSD（nvrtx 無し）が、A なら GOP の画面で、B なら passthrough の guest で起動することを確かめる（boot-test の login か、段の印の前の画面）。

## 受け入れ

- host.md の表が全部埋まる。
- 選んだ方式（A・B）で、今の zedBSD が RTX 2070 の GOP の画面に出る（証拠: 写真か capture）。
- 手順が `plan/tools/nvrtx/README.md` にあり、能力の低いセッションでも流せる。

## 注意

- host の Linux の設定（kernel の command line、driver の blacklist）を変えるのは、ユーザーの許可の後。
- QEMU の console・serial の log で判定しない（AGENTS.md）。
