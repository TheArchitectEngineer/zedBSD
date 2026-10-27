<!-- awesome-plan project=zedbsd record=ws304-p003 -->

# ws304-p003 実機（Pi 5 ＋ M.2 の NVMe）での確かめ

- 親: [WS304](../ws.md)。依存: p002、実機の用意（ユーザー: 新しい SwitchBot、2026-10-27 の夜の予定）。
- Status: uncleared（q1620-i02、2026-10-27 P3。pcie1 の link が上がらない。rescal の実験の前に権限の判定で停止、再開の条件は下の「q1620-i02 の結果」）
- Disposition: normal

## 目的・受け入れ

Pi 5 に M.2 HAT と NVMe を付けた実機で、pcie1 の link が上がり、NVMe が `/dev/nvme0n1` として見え、読めること。RP1 の USB・Ethernet を壊さないこと。

## 手順

1. image: `config/ci/config-rpi5.mk` で作る（`CONFIG_DRIVER_PCI_NVME=y`、`config.txt` に `dtparam=pciex1`）。p002 の merge の後の main の SHA で。
2. HAT の型番を記録する（単一の NVMe であること。switch つきなら R8）。HAT の電源の LED を見る（ユーザー）。
3. 起動して `dmesg` で次の行を見る（値は版の違いで少し変わってよい）:
   - `rpi5-pcie: PHY calibration done already` または `rpi5-pcie: PHY calibration left as it is: RP1's link is up`
   - `rpi5-mip: messages to PCI fffffff000, data 8-15, GIC lines 287-294`
   - `pcie: brcmstb resetting at 1000110000`、`pcie: brcmstb at 1000110000 revision <n> (BCM2712)`
   - `pcie: brcmstb inbound 1 PCI 1000000000 CPU 0 size 1000000000`、`pcie: brcmstb inbound 2 PCI fffffff000 CPU 1000131000 size 1000`
   - `pcie: brcmstb link up, generation 2, x1`（Gen 1 の device なら 1）
   - `pcie: brcmstb 01:00.0 BAR0 at 80000000 (16384 bytes)`（大きさは device による）
   - `rpi5-pcie: segment 1 started, message interrupts through the MIP`
   - `nvme: PCI controller 0001:01:00.0 version=... MSI-X`
   - `nvme: /dev/nvme0n1 namespace=1 blocks=... block-size=512 ...`
   - RP1 の USB・Ethernet の行がこれまでと同じであること、`rpi5-serror` の行が出ないこと。
4. 読みの試験（書かない）: `dd if=/dev/nvme0n1 of=/dev/null bs=1048576 count=256` が終わり、時間を記録する。`dd if=/dev/nvme0n1 bs=1048576 count=64 | cksum` を 2 回流し、同じ値であること。`blkid` か `diskpart` で区画が読めること（区画があれば）。
5. 失敗の切り分け:
   - `link down`: HAT の LED、FFC の向き、`dtparam=pciex1` の有無。`pciex1_tperst_clk_ms`（例 10）を足して再試験。
   - `PHY register ... write did not finish`: MDIO の列を読む（R4）。
   - link は上がり `nvme: attach failed at ...` か時間切れ: MSI が来ていない疑い（R1）。gdbstub は無いので、log の GIC line と `hw` の割り込みの数で見る。MSI を切る switch は作らない（R5）。uncleared にして設計を直す。
   - SError: `rpi5-serror` の行の段の名前で場所を見る。
6. 任意: Pi 4（Pi 4 の VL805 の USB が今までどおり動くこと。pci-brcmstb.c を共有するため）。T1 か Q1 の判断で。

## 記録

- 実機の証拠と host 試験の証拠を分けて書く。やっていない確かめは未実施と書く。

## q1620-i02 の結果（2026-10-27 11:40〜12:20、P3、実機 Pi 5 10.0.30.6 ＋ Geekworm X1001 ＋ NVMe）

実機の証拠だけ（QEMU は無し）。host 試験は `make -f plan/ws304/tests/host-test.mk run` で 3 本 PASS（brcmstb2712 1144、rpi5-mip 878、BCM2711 の列 117 行不変）。

- 入り方: `sshpass -p kei ssh kei@10.0.30.6`、root は `echo kei | sudo -S`（ユーザーの決定、authorized_keys には書かない）。zedBSD の sudo に `-p` は無い。
- /boot/boot0（FAT 130 MB）が満杯だったので Q1 が vmunix.gputest と途中の写しを消した。元の kernel・config.txt・cmdline.txt・rc.conf の控えは host の `build/tmp/pi5-backup/`（worktree の中、cksum 照合済み、vmunix は cksum 562076002 7544832）。`p003-pi5.sh` は控えを host に置き、Pi の上に写しを作らない形にした（`BACKUP=` と `SSHPASS=`）。
- config.txt に `dtparam=pciex1` が無かったので足した（今の Pi の config.txt は元＋`[all]`・`dtparam=pciex1`・`dtparam=pciex1_tperst_clk_ms=10`・`dtparam=pciex1_gen=1`）。
- main 2d2464505 の kernel（af90a8fb）で起動・SSH・RP1 の USB・Ethernet は従来どおり。pcie1 は reset・`revision 500 (BCM2712)`・inbound の窓まで進み `pcie: brcmstb link down`。
- 診断（a4b79420a、link down の時に STATUS・PCIE_CTRL・HARD_DEBUG・LTSSM の履歴 0x1cec〜0x1cf8 を log）:
  - 最初の向きと 1 回目の付け直し: `LTSSM 81808180 ×4`（Detect.Quiet/Active だけ＝相手の終端が見えない）。
  - ユーザーが FFC を逆さに付け直した後: `LTSSM 09808109 80810980 81098081 09808109`、tperst 10 ms では `0a808109`（Polling.Active 0x09、Polling.Configuration 0x0a が出る）。相手は見え、TS の交換が通らず Detect に戻る。
  - tperst_clk_ms=10、gen=1、PERST# の再試行 2 回（各 1 秒、1 回は REFCLK_OVRD で refclk を常時出す）: どれも link down。履歴は解除の約 30 ms 後に止まる。実験の code は `plan/ws304/temp/p003-experiments.patch`（sha256 43e537bc…、merge しない、git の外）。
- 手順は Raspberry Pi Linux rpi-6.12.y の pcie-brcmstb.c（probe・setup・post_setup・start_link）と照合し一致。残る差は rescal（PHY の抵抗の較正、pcie0〜2 共有）: Linux は pcie1 の probe で毎回行い、zedBSD は RP1 の link が生きているので飛ばす（設計 D2・R3）。STATUS の done は 0。
- `rpi5-pcie: 1000100000 disabled ... (config.txt: dtparam=pciex1)` は pcie0 の行で、注記が誤り。47abe7538 で pciex1（1000110000）の時だけ注記を出すようにした。
- 12:00 前後の不通は、ユーザーの FFC の付け直しの電源断。hang ではない。
- 停止: rescal の実験の安全網（RP1 が死んでも次の起動で元の kernel に戻す一回きりの service を Pi の /etc に置き、rc.conf に足す）が権限の判定（Unauthorized Persistence）で止められ、実行されなかった（Pi の /etc は変わっていない）。回避せず停止。
- 再開の条件: ユーザーが (a) 安全網の service を許すか、(b) 安全網なしで rescal の実験をする（RP1 が死ぬと SSH が切れ、SD を外して vmunix を戻す必要）か、(c) tryboot（firmware の一回きりの起動、zedBSD には reboot の flag を渡す code が無いので mailbox の SET_REBOOT_FLAGS の実装が要る）を選ぶ。rescal でも上がらなければ、FFC・HAT・NVMe の組の確かめ（Raspberry Pi OS の lspci）。
- 未実施: NVMe の読みの試験、NVMe への install、NVMe からの起動、Pi 4 の確かめ。
