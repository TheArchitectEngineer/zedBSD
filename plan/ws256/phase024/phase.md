<!-- awesome-plan project=zedbsd record=ws256-p024 -->

# ws256-p024 PCIe1（M.2）と NVMe の実機、NVMe の root

- 親: [WS256](../ws.md)。設計: [design-peripherals.md](../design-peripherals.md) §3.11（§3.5 の PLDA、§3.6 の NVMe の INTx）。
- 状態: cleared（2026-10-26 Q1、q1587。記録の末尾）
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

## 結果（2026-10-26 06:30、P2、worktree ~/zedBSD-worktrees/p2-ws256p024、branch agent/p2-ws256p024）

### 分かった事: PCIe1 は p020 の code ですでに動いていた

- 板の kernel（`/boot/boot0/vmunix`、2026-10-21 に置いた物）は、p020 の `pci-plda.c`・`vf2-pcie.c` で 2 つの host を両方立ち上げていた。PCIe1 に足す code は無かった（clock・reset・pin・PHY・PERST#・link は p020 のまま）。
- lspci（SSH）: `0000:00:00.0 1556:1111`・`0000:01:00.0 1106:3483`（VL805）・`0001:00:00.0 1556:1111`・`0001:01:00.0 15b7:5041`（WD の NVMe、class 0108 prog-if 02）。bus の番号は host ごとの segment 0・1（`linux,pci-domain`）で別、BAR は PCIe0 が 0x3000_0000（4 KiB）、PCIe1 が 0x3800_0000（16 KiB）で、窓が違うので衝突しない。
- NVMe: `nvme: PCI controller 0001:01:00.0 version=10400 queue=64 timeout=5000ms INTx`、`/dev/nvme0n1 namespace=1 blocks=0x74706db0（1953525168）block-size=512 writable max-transfer=128`（1 TB）。INTx の道（§3.6）。

### 実機の確かめ（SSH と生きた serial。保存した log では判定していない）

| 確かめ | 結果 |
| --- | --- |
| 読み書きの一致（`plan/ws256/tests/nvme-rw/rw.sh write /dev/nvme0n1 3`、7 件 × 3 回） | 21/21 ok（cmp）。大きさ 512 B・4 KiB・64 KiB・100 KiB（max-transfer の 128 block を越え driver が分ける）・1 MiB・64 MiB・末尾の 1 MiB。namespace の 1 block 先への書き込みは拒まれた（ok）。板の古い kernel で 1 回、この branch の kernel で 1 回 |
| device に届いた事（電源を入れ直した後の `rw.sh verify`） | 7/7 の cksum が直前の書き込みの 3 回目と一致。古い kernel が書き、この branch の kernel が読んだ回と、この branch の kernel が書いて読んだ回の 2 回。注意: 書いた直後の読み返しは kernel の block の cache から返る（hw.diskstats の read_ops が増えない）ので、device の確かめは電源の入れ直しの後の verify だけ |
| U-Boot の `nvme scan` の後の attach（`CC.EN`） | serial で U-Boot を止め `pci enum; nvme scan; nvme info; boot`。kernel の serial に `pcie1: firmware left 15b7:5041 class 010802, command 0006`・`pcie1: NVMe CC.EN 1, CSTS.RDY 1` → PERST の後 `pcie1: link up`・`nvme: … INTx`・`/dev/nvme0n1 …` が出て attach した。PCIe0 は `firmware left 1106:3483`・`HCHalted 1`。通常の起動（U-Boot が PCIe に触らない）では両方とも `firmware left the link down` |
| PCIe0（VL805） | この branch の kernel で `pcie0: link up`、BAR0 0x3000_0000、`usb0: xHCI, 5 root ports`、hub 2109:3431 の 4 port。**USB keyboard は板に挿さっていなかった**（lsusb は hub だけ、前の kernel の log にも HID が無い）ので、keyboard の入力は未確認 |
| build | `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p024-vf2 -j16 vmunix` exit 0・warning 0。`config-amd64.mk` の vmunix も exit 0・warning 0（devfs.c が共有のため）。`vf2-pcie.c` を `-DCONFIG_DRIVER_PCI_GHCI=1` で compile だけ（link は P3 の riscv64 の build の file の後） |
| host 試験 | `plan/ws256/tests/pci-plda/run.sh build/tmp/p024-host` → `137 PASS, 0 FAIL` と `oversize: 35 PASS, 0 FAIL`、通常と ASan/UBSan。変異（HEAD の pci-plda.c）で oversize は 4 FAIL |

### 直した事

1. **devfs の `BLKGETIDENTITY` が user の address に kernel から直接書いていた**（`src/kern/devfs.c` の `block_ioctl`）。riscv64 は SUM が立たないので、板で `blkid /dev/mmcblk0p3` が `riscv64 trap scause=f stval=3fffffec60`・`fatal: src/hal/riscv64/int.c:330: unhandled riscv64 kernel exception` で止まった（生きた serial で見た）。kernel の stack に取って `copyout` するように直した。直した kernel で mmcblk0p3・p4・nvme0n1p1・p4 の blkid が答えを返す（nvme0n1p3 は `Invalid argument`、乱数で上書きした所の file system の判定の答えで、trap ではない）。amd64 も同じ形（SMAP が無いので表に出ていなかっただけ）。
2. **（Q1 の依頼 A、P3 の WS288 p043 のため）** `vf2-pcie.c` の `register_drivers` で `CONFIG_DRIVER_PCI_GHCI` の時に `drv_pci_ghci_driver_register()` を呼ぶ。
3. **（Q1 の依頼 B）** `pci-plda.c` の `assign_bar`: 窓に入らない BAR を ENOSPC で全体を止めず、BAR（64 bit なら上半も）に 0 を書いて log し、残りの BAR と root port の窓を続ける。host 試験に NVMe の BAR が 256 MiB の場合（oversize）を足し、fake の endpoint の BAR2〜5 を 0 に読むよう直した（前は all-ones が残り 16 byte の BAR が 4 つ置かれていた）。

### 範囲外の観察（直していない）

- SD（dw-mmc）の読み取りの失敗: `dw-mmc: read of … failed (5, status 408c|a4)`。1 回の起動では `/sbin/init` の load が EIO で止まった（電源の入れ直しで戻った）。memory にある 2026-10-21 からの SD の不調と同じと見られる。
- `rw.sh` の 64 MiB の読み返しが 2・3 回目に 15〜28 秒かかった（1 回目は 0 秒）。hw.diskstats の read_ns は小さいままで、device ではなく kernel の側（cache か tmpfs の上書き）の時間。/dev/null への 64 MiB の読みは 1〜2 秒。

### 残り

- NVMe の root（§3.11、任意）: 未実施。今の command line に `boot0=UUID=5A45-4253 rootpart=PARTLABEL=zedBSD-root` があり、VF2 の root は SD の mmcblk0p4。pcat の形（`probe_fixed_disks`・`wait_for_boot_disk`）を visionfive2.c に足すのは p026 の後に回す。
- USB keyboard の入力の確かめ（板に keyboard が無かった）。

## WS288 のための記録: KC705（GHCI、BAR0 小・BAR2 1 GiB 32 bit、MSI 1 本）を M.2 に挿す時に要る事（実装はしていない）

- **PCIe1 の窓（板が受け取る DT、`/boot/boot0/dtb/starfive/jh7110-starfive-visionfive-2-v1.3b.dtb` を dtc で読んだ）**: `ranges` は 2 つだけ。32 bit の非 prefetch の窓 0x3800_0000 から 128 MiB（PCI の address = CPU の address）と、64 bit の prefetch の窓 0x9_8000_0000 から 1 GiB。**32 bit の prefetch の窓は無い**。`dma-ranges`・`dma-coherent`・`dma-noncoherent` は無く、`msi-controller` がある。
- **BAR2 1 GiB 32 bit は入らない**: 32 bit の窓は 128 MiB で、BAR0 と合わせても 1 GiB に届かない。64 bit の窓は 1 GiB ちょうどだが、32 bit の BAR は 4 GiB より上の PCI の address を持てない。入れる道は (a) bitstream の BAR2 を 64 bit・prefetch にし、64 bit の窓へ置く（BAR0 は 32 bit の窓、`assign_resources` が今は 32 bit の窓だけを使うので、64 bit の窓に置く分を足す。root port の prefetch の窓も今は閉じている）、(b) ATR の translation で CPU 0x9_8000_0000 → PCI の 32 bit の address（例 0xC000_0000）に写し、その範囲を DMA から外す（DDR の 0x4000_0000〜0xFFFF_FFFF と PCI の address が重なり、root port の窓に入る address への device の DMA は上へ行かない）、(c) BAR2 を 128 MiB 以下にする。今の KC705 の bitstream は BAR0 128 KiB・BAR2 64 MiB で 32 bit の窓にちょうど入る（Q1 2026-10-26）。
- **MSI**: PLDA の MSI の受け口と demux が要る（p026、OpenBSD stfpcie.c の `#ifdef notyet` と Linux pcie-plda-host.c の値だけ、2026-10-26 ユーザー）。PLIC の 1 本の線（pcie1 は 57）の ISTATUS の MSI の bit から MSI の状態を読み、vector ごとに振り分ける。MSI の address は PLDA の受け口の address を device に書かせ、PCI core の MSI の割り当てを host の hook に通す（HAL の global の MSI ではない）。
- **DMA**: root の DMA の device は 32 bit（`PLDA_DMA_ADDRESS_BITS`、DDR の 0x4000_0000〜0xFFFF_FFFF の 3 GiB）。inbound の ATR は書いていない（p020 の判断 1）。(b) を採るなら、BAR を写した PCI の範囲を DMA の上限から外す。
- **coherency**: `VF2_DMA_DEFAULT_COHERENT` は 1 で、PLDA の DMA の device は coherent と答える。p023（DMAC での確かめ）は板での確かめが未実施のまま（in-progress）で、根拠にならない。p024 の観察は PCIe の DMA が両向きとも coherent である事と矛盾しない: NVMe の SQ・CQ が cache の操作無しで動き、512 B・4 KiB の書き込み（CPU→device）と電源の入れ直しの後の読み（device→CPU）が一致した。ただし陽性の対照の無い観察で、p023 の手順の確かめではない。
- 2026-10-26 06:31 Q1: agent/p2-ws256p024 484d6374d を main に fast-forward（main 07011c914 を P2 が merge 済み）。src の差分（devfs.c の BLKGETIDENTITY を copyout に、vf2-pcie.c の GHCI の登録、pci-plda.c の入らない BAR を 0 にして続ける）を Q1 が読んだ。受け入れ（PCIe1 の link・NVMe の列挙・読み書きの一致（電源の入れ直しの後の cksum 7/7 × 2）・PCIe0 の VL805）を満たす → **cleared**。残り: NVMe の root（任意だった、未実施）、USB keyboard の入力（板に keyboard が無く未確認）、dw-mmc の SD の読みの誤り（1 回 /sbin/init が EIO、範囲外。再発したら bug に）。DMA の一貫性は p023 の実機の確かめが未実施（p024 では矛盾なし、陽性の対照なし）。
