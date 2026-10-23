<!-- awesome-plan project=zedbsd record=ws289-p005 -->

# ws289-p005 ZYBO の LAN（GEM0 の glue、RTL8211E、SLCR の tx clock、`mac0=`）と DHCP・SSH

- 親: [ws289](../ws.md)。設計: [design.md](../design.md) 版 2 の §4.4（Zynq の glue と SLCR）・§4.5（RTL8211E）・§4.6（MAC）・§4.7（userland）・§6（`qemu-zynq.sh --net`）・§7・§8 の p005 の行・§9 の p005 の行・§10 の R3/R9/R10。
- Queue: Q1 の割当（P3、2026-10-23、worktree `/home/awe/zedBSD-worktrees/p3-ws289-p005`、branch `agent/p3-ws289-p005`、基点 main 4f2ab03bc）。Fable は使わない（ユーザー）。
- 判断と許可: U4（QEMU zynq の LAN は host の 127.0.0.1 の転送 port から SSH で判定）・U5（固定の MAC `02:5a:59:42:4f:30` を既定に、`mac0=` で変える）はユーザー「はい」（design §11）。WS286 の U3（ZYBO の QEMU と実機は生きた serial で判定）・U4（担当が板を JTAG で直に操作、板の lock）。ユーザーの決定「generic cores, not Pi references」（core は p003 の `cadence-gem.c`、glue は Zynq だけの事）。toolchain・HAL の API は変えていない。`src/kern/boot.c` の parameter の表に `mac0` を足した（design §11 の Q1 への依頼 (2)）。
- Status: cleared（2026-10-23 Q1: 実機の ZYBO（JTAG、ramdisk の root、LAN）で PHY 0 id 001cc915・1000 Mbps・DHCP・host から ssh の uname -m = armv7・8 MiB の scp の往復の cksum と cmp・ping 1000/100 回と flood 20000 回の損失 0・mac0=。T1 の QEMU zynq（SD の root、--net 2289、PHY 7 id 01410cc2、DHCP 10.0.2.15、ssh-check PASS）PASS、5 つの kernel の build warning 0、cadence-gem-host-test PASS（rp1 の trace 一致）。merge d5c244472。100・10 Mbps は未確認（switch が 1000 だけ））

## 受け入れの条件（design §8.2・§8.3・§9 の p005）

1. zybo・armv7-virt・rpi4・rpi5・amd64 の kernel の build が warning 0。`cadence-gem-host-test` PASS。
2. QEMU xilinx-zynq-a9（T1、serial ＋ SSH、`--net PORT`）: `gem: PHY 7 id 01410cc2`、en0 link up、DHCP の lease、host から `ssh -p PORT root@127.0.0.1 uname -m` = `armv7`、`scp` 8 MiB の往復で `cksum` 一致。
3. 実機の ZYBO（JTAG の kernel ＋ SSH の設定の ramdisk、LAN を繋いで）: `gem: PHY 0 id 001cc915`、link up、DHCP の address、host から ssh、`scp` の往復。

## 作った物

- `src/drivers/platform/zybo/zynq-ethernet.c`・`.h`（新規、Zlib）: GEM0 の glue。SLCR の `APER_CLK_CTRL` bit 6・`GEM0_RCLK_CTRL`・`GEM0_CLK_CTRL` の gate を見て落ちていれば立てる、`kern_device_map(0xE000B000)`、DMA は `drv_dma_device_create`（32 bit、`coherent = 0` = uncached の slot）、`pclk_hz` = cpu_1x（取れなければ ZYBO の 108.33 MHz と警告）、`descriptor_width = 32`、`phy_address = 0`・`phy_scan = 1`（QEMU の 7 を走査で）、`flags = RESTART_RX_ON_USED_BIT | USRIO_RGMII`、`setup_phy` = `drv_mii_phy_reset` → identity が RTL8211E（revision を mask）なら `drv_rtl8211e_set_rgmii_delays(1, 1)` → `drv_mii_phy_start_autonegotiation(gigabit)`、`read_link` は generic、`set_tx_clock` = 速さの 125/25/2.5 MHz に最も近い DIV0/DIV1（外側の DIV1 を小さい方から、最初の一致で止める。IO PLL 1000 MHz では 8/1・40/1・50/8。EMIO の clock は触らない）、`establish_irq` = `kern_irq_register(54)` ＋ `kern_irq_unmask` の adapter（handler の後に `kern_irq_send_eoi`）。MAC は (1) `mac0=`（loader の行を `kern_boot_parameters_parse` で読む: platform の init は kernel の parse より前）、(2) DTB の `cdns,gem` で reg が `0xe000b000` の node の `local-mac-address`、(3) 固定の `02:5a:59:42:4f:30` と log の 1 行。不正な `mac0=` は log して次へ（起動は止めない）。
- `src/drivers/ethernet/rtl8211e.c`・`include/drivers/ethernet/rtl8211e.h`（新規）: `drv_rtl8211e_set_rgmii_delays`（page 7 → extension page 0xa4 → register 0x1c の bit 13|12|11 を modify → page 0 に戻す。失敗しても page 0 には戻す。Procedure referenced: Linux v6.12 `realtek.c` `rtl8211e_config_init`）。`DRV_RTL8211E_IDENTITY` = `0x001cc915`。
- `src/drivers/platform/zybo/zynq-slcr.c`・`.h`: offset（`ARM_CLK_CTRL`・`GEM0_RCLK_CTRL`・`GEM0_CLK_CTRL`・`CLK_621_TRUE`）と `drv_zynq_slcr_cpu_1x_hz()`（CPU の PLL / `ARM_CLK_CTRL` の divisor / 6 か 4。Values referenced: Linux `clkc.c` の `cpu_1x_div` = 4 + 2 × 621）。
- `src/kern/platform/zybo.c`: `kern_platform_init` で `drv_zynq_ethernet_init`（失敗は `zybo: Ethernet unavailable` で起動は続く）、`kern_platform_refresh_devices` で `drv_zynq_ethernet_refresh`（main の WS286 p008 の `/dev/fpga0` と merge 済み）。`platform/armv7/zybo.mk` に `zynq-ethernet.c`・`cadence-gem.c`・`mii-phy.c`・`rtl8211e.c`。
- `include/kern/boot.h`・`src/kern/boot.c`: `KERN_BOOT_PARAMETER_MAC0`（表に名前だけ。値の検査は driver。不明の parameter の警告が出ないため）。`docs/reference/kernel-boot-parameters.md` §7e。
- `src/drivers/ethernet/cadence-gem.c`: Q1 の指摘（WS286 p009 の review）の書き方だけの直し 3 か所（閉じ括弧の後の空行、`gem_poll` の 2 つの `if` の段落の comment）。動作は変えていない（host 試験の trace が byte 一致）。
- userland（armv7 の root）: `platform/armv7/userland.mk` に network の program（`dhcpc hostname ifconfig net networkd ping route` のうち config が名指す物）、network の program は `netutil.o`・`dhcp.o` と link（arm64 と同じ形）、`net/main.o` に libedit の include。`platform/armv7/rootfs.mk`: `networkd` を名指す root に `networkd` の service と `/etc/net.conf`（base の物。名の無い有線の interface は networkd が DHCP にする、design L3）。試験の config [tests/config-zybo-ssh.mk](../tests/config-zybo-ssh.mk) に network の program を足した（既定の `config-zybo.mk` は変えていない。root は 16 MiB のまま、image 17 MiB）。
- `plan/tools/zybo/qemu-zynq.sh --net PORT`（`-nic user,model=cadence_gem,hostfwd=tcp:127.0.0.1:PORT-:22`、1 つ目の NIC が GEM0）。`plan/tools/zybo/uart-expect.py` の `--then run:COMMAND`（host の command を走らせ、0 以外で失敗）と expect の名前付きの group（`(?P<addr>…)` を後の step の `{addr}` に）。`jtag-boot.sh` と `qemu-zynq.sh` の両方で使える。
- [tests/ssh-check.sh](../tests/ssh-check.sh)（新規）: `HOST PORT OUTDIR`。`sshpass -p root ssh` で `uname -m` = armv7（sshd が起動するまで最大 5 分待つ）、8 MiB の乱数の file を scp で送り guest の `cksum` と比べ、戻して `cmp`。消さない（OUTDIR は呼ぶ側の物）。

### design からの差（Q1 の確認用）

1. MAC の (2) は `/aliases/ethernet0` でなく「`cdns,gem` で reg が GEM0 の node の `local-mac-address`」（tree の FDT の口に path の解決が無い。U-Boot の fixup は同じ node に書く）。JTAG と QEMU の DTB には無いので未確認。
2. tx clock は design の固定の表（8/1・40/1・20/20）でなく PLL の rate からの計算（1000 MHz で 8/1・40/1・50/8。10 Mbps の組が違うが同じ 2.5 MHz）。QEMU の IO PLL（1300 MHz、reset 値）でも何か選ぶ。
3. `net.conf` の `en0` は書いていない（design §4.7 のとおり任意。実機で lease は数秒で取れた）。
4. 既定の `config-zybo.mk` には network の program と package を入れていない（JTAG の ramdisk は p010 の最小の root のまま、design §4.7 の「ramdisk には入れない」）。SD の root（p006）に入れる時に config を決める。

## 確認

### host（2026-10-23 P3）

| command | 結果 |
| --- | --- |
| `make -j12 ZEDBSD_CONFIG=config/ci/config-{zybo,armv7-virt}.mk BUILD=build/p005-<c> vmunix` | 両方 rc 0、warning 0（merge の前と main 4bffa4976 の merge の後の両方） |
| `make -j12 -o build/p005-<c>/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-{rpi4,rpi5,amd64}.mk BUILD=build/p005-<c> vmunix` | 3 つとも rc 0、warning 0、`arm64 vmunix check: PASS`・`amd64 vmunix check: PASS`（merge の前と後） |
| `make -o build/p005-rpi5/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi5.mk BUILD=build/p005-rpi5 cadence-gem-host-test` | `cmp` 一致、`cadence-gem-host-test: PASS`（書き方の直しの後と merge の後） |
| `make ZEDBSD_CONFIG=plan/ws289/tests/config-zybo-ssh.mk BUILD=build/p005-zybo-ssh -j16 vmunix build/p005-zybo-ssh/root.img build/p005-zybo-ssh/zybo-sd.img` | rc 0。tree の code の warning 0（warning は OpenSSH・OpenSSL の deprecated と perl の locale だけ）。root.img 17 MiB、root.ufs 256 MiB、zybo-sd.img 512 MiB。root に `/sbin/{networkd,net,dhcpc,ifconfig,route}`・`/etc/service.d/{networkd,networking,sshd}`・`/etc/net.conf` |
| `sh -n`・`python3 -m py_compile`（qemu-zynq.sh・uart-expect.py・ssh-check.sh） | OK |
| `git diff --check` | clean |

### 実機 ZYBO（JTAG、LAN の cable あり、2026-10-23 14:5x〜15:3x P3、board の lock、実機の証拠）

kernel `build/p005-zybo-ssh/vmunix.bin`（tree c2afdb894）、`--initrd build/p005-zybo-ssh/root.img`（rootpart 無し = ramdisk の root、実機の card は p004 のとおり初期化できない）、SPL は p2-zybo-p008 の `spl-build.sh` の物の写し（sha256 `ed936c13…`、p004 と同じ）。

- 1 回目（mac0 無し）: `zynq-ethernet: no address given; taking the fixed one`、`zynq-ethernet: bus clock 108333333 Hz`、`gem: revision 00020118, queues 0001, MDIO divider 48`、**`gem: PHY 0 id 001cc915`**、`gem: en0 ready MAC 02:5a:59:42:4f:30`、`gem: en0 RX/TX enabled`、**`gem: en0 link up 1000 Mbps full duplex`**、`zynq-ethernet: transmit clock 125000000 Hz for 1000 Mbps (pll 1000000000 Hz / 8 / 1)`。HRESP（`gem: DMA error`）は出ない（R3）。`ifconfig en0`: `inet 10.0.30.2 netmask 255.255.0.0`、IPv6 の SLAAC の address 2 つ、`media: 1000Mbps`。
- host（10.0.10.2）から: `ping -c 3` loss 0、`sshpass -p root ssh root@10.0.30.2 uname -a` = `zedBSD kei 1.0.0-beta2+gc2afdb8 … armv7`、`uname -m` = `armv7`。8 MiB の乱数の file を `scp` で送り（5.5 s）board の `cksum` = `275954623 8388608`（host と一致）、戻して（8.7 s）`cksum` 一致・`cmp` 一致。`sudo ping -c 1000 -i 0.005` loss 0（rtt 平均 0.084 ms）、board から host へ `ping -c 100` loss 0。
- （無効な試し: その後の flood ping は、間に P2 が板を自分の kernel で起動し直していたので結果に使わない。）
- 2 回目（15:2x〜15:3x、`--append mac0=02:5a:59:42:4f:31`、`jtag-boot.sh` の一つの lock の中で起動から終わりまで、`--then run:` の steps。exit 0）: `zynq-ethernet: address from mac0=`、`gem: PHY 0 id 001cc915`、`gem: en0 ready MAC 02:5a:59:42:4f:31`、`gem: en0 link up 1000 Mbps full duplex`、tx clock 125 MHz（/8/1）、`dhcpc: en0: bound 10.0.30.3/16`（MAC が変わったので別の lease）、`ifconfig en0` の `inet 10.0.30.3`。`plan/ws289/tests/ssh-check.sh 10.0.30.3 22 build/tmp/p005` → `uname -m = armv7`、cksum `4063767974 8388608` が host と guest で一致、scp 上り 5 s・下り 10 s、`ssh-check: PASS`。`sudo ping -f -c 20000 -s 1400` → **20000/20000 受信、loss 0**（受信の ring の負荷、RXUBR の道）、その後の `ping -c 1000 -i 0.005` loss 0。`dmesg` に `gem: DMA error` 無し。

### QEMU（T1 に依頼、未実施）

下の「T1 への依頼」。

## T1 への依頼（2026-10-23、Q1 経由）

tree: branch `agent/p3-ws289-p005` の最新（Q1 が merge した後なら main）。build（worktree に main の `build/llvm`・`llvm-source`・`llvm-build`・`NoctLang` への symlink、`build/distfiles` に `openssh-10.5p1.tar.gz`・`openssl-3.5.8.tar.gz`・`zlib-1.3.2.tar.xz`・`cacert-2026-08-13.pem`。host に `sshpass`）:
`make ZEDBSD_CONFIG=plan/ws289/tests/config-zybo-ssh.mk BUILD=build/p005-zybo-ssh -j16 vmunix build/p005-zybo-ssh/zybo-sd.img`
（QEMU が SD に書くので、A の前に `cp build/p005-zybo-ssh/zybo-sd.img build/tmp/p005-sd.img` の写しを使う）

- A（zynq、SD の root、LAN、serial U3 ＋ SSH U4。port は空いている物、例 2289）:
  `plan/tools/zybo/qemu-zynq.sh build/p005-zybo-ssh/vmunix --sd build/tmp/p005-sd.img --net 2289 --append 'rootpart=/dev/mmcblk0p2' --timeout 1200 --expect 'gem: PHY 7 id 01410cc2' --expect 'gem: en0 ready MAC 02:5a:59:42:4f:30' --expect 'gem: en0 link up' --expect 'init: system running' --then 'run:sleep 60' --then 'type:' --then 'expect:login: *$' --then type:root --then expect:Password: --then type:root --then 'expect:root@kei' --then 'type:ifconfig en0' --then 'expect:inet 10\.0\.2\.15 netmask' --then 'expect:root@kei' --then 'run:plan/ws289/tests/ssh-check.sh 127.0.0.1 2289 build/tmp/p005-qemu'`
  → exit 0 で PASS（`ssh-check: uname -m = armv7`・`ssh-check: PASS` が `host|` の行に出る）。`gem:`・`zynq-ethernet:`・`dhcpc:` の行と `ssh-check:` の行を写して返す。sshd の host key の生成（RSA・ECDSA・ED25519）が QEMU では数分かかる事がある（ssh-check は最大 5 分待つ）。
- 失敗の時: 最後に見えた serial の行と `host|` の行を返す（解析は担当）。`build/tmp/p005-sd.img`・`build/tmp/p005-qemu/` は Q1 が `q1-clean.sh` で消す。

## 残り・引き継ぎ（Q1 へ）

- T1 の A の結果と Q1 の判定・merge。
- Master の Tools に `qemu-zynq.sh --net`・`uart-expect.py` の `run:` と名前付きの group・`plan/ws289/tests/ssh-check.sh` を載せる事（担当は master.md を書かない。ssh-check.sh は p006 の SD 起動の試験でも使う見込み）。
- 削除の依頼（担当は rm しない）: `/home/awe/zedBSD-worktrees/p3-ws289-p005/plan/tools/zybo/__pycache__/`（py_compile が作った、git の ignore の対象）、`build/tmp/p005/`。
- 実機の 100 Mbps・10 Mbps の tx clock は未確認（switch が 1000）。RXUBR の立て直しが実際に起きたかは log が無いので分からない（core は log しない）。
- 既定の ZYBO の root（`config-zybo.mk`）に network と SSH を入れるかは p006（SD の root）で決める。
