<!-- awesome-plan project=zedbsd record=ws256-p015 -->

# ws256-p015 rootfs からの起動（SD の root、板の前の code）

Status: in-progress（2026-10-20: ユーザーの VF2 の実機（2026-10-20 06:11「画面がつきました！」「HDMIで画面を見ながら、USBキーボードでログインしてコマンド操作できました。」、image main d1ef60e39 の native-dtb.img、U-Boot の prompt から mainline の v1.3b の DTB で bootefi） で SD の root から initrd 無しで起動し画面で login できた（SD は 24.75 MHz の fallback、BUG-340）。残り: design §3.4 の 10 回の起動と p015.txt の reboot の後の読み、U-Boot の自動起動（今は prompt で mmc rescan・load・bootefi、BUG-339））

- 親: [WS256](../ws.md)。Queue: q1295-i01（P2）。
- 範囲: [design-peripherals.md](../design-peripherals.md) 改訂 4 §3.4 と [design.md](../design.md) §3.2 の SD の image。SD の image の ESP（`EFI/BOOT/BOOTRISCV64.EFI`・`zedbsd.cfg`・`vmunix`）と root の partition、loader の `boot0=UUID=` と `rootpart=` で SD の root を見つけて mount し initrd 無しで起動する道（p014 の pcat_auto と mmcblk0）、image を作る build の規則（`disk-image.mk`）。HAL（hal.h）は変えない。
- 同じ Queue の (a): riscv64 の `net: UP: No such file or directory (6)` と `init: oneshot networking failed`（T1-729 の走り C の画面 `build/review/t1-729/vf2-gop-login.png`）の原因と直し（下の「(a)」）。
- 完了の条件: build が warning 0（VF2・virt）、host 試験が合格、T1 の QEMU の確かめ（virt の NVMe で native の image の root）を Q1 が合格とする。実機の 10 回の起動と login（design §3.4）は板の接続の後（#10・#15、#27）。

## (a) riscv64 の `net: UP: No such file or directory`（2026-10-19、P2、commit 46518392b）

- 原因: `/sbin/ifconfig` が image に無い。init の `networking` の oneshot は `/sbin/net startup` を走らせ、`net` は `/etc/net.conf` の `lo0`（loopback、127.0.0.1/8）を networkd に `UP lo0` で頼む（`userland/base/net/main.c` の `configure_loopback`）。networkd は `/sbin/ifconfig lo0 up` を exec する（`userland/base/networkd/main.c` の `NETWORKD_OP_UP`）が、exec が ENOENT で失敗し、その errno が `net: UP: No such file or directory` になる。kernel の `lo0` は `net_init` → `loopback_init`（`src/kern/net/core.c`）で作られており、riscv64 の network の stack の初期化と device には問題が無い（boot は `network subsystem initialization failed` で止まらない）。
- なぜ無いか: top の `Makefile` の「Essential administrative tools」の一覧が `net`・`networkd`・`init`（`networking`・`homes` の service の file を入れる）を全部の image に必ず入れるが、それらが exec する `ifconfig`・`route`・`dhcpc`（networkd）と `mkdir`・`chown`（`/usr/libexec/make-homes`）は入れない。amd64・arm64 の config は自分の program の一覧にこれらを持つので出ない。program の一覧の短い config（`config/ci/config-visionfive2.mk`（T1-729 の時は `cat echo login ls sh uname`、今は mkdir・chown を含む）・`config/ci/config-riscv64-virt.mk`・`plan/ws256/tests/config-riscv64-virt*.mk`・ppc32・ppc64・m68k・riscv32 の CI の config）で出る。T1-729 の root の `/sbin` に `ifconfig` が無く、`/bin` に `mkdir`・`chown` が無い事を `/home/awe/zedBSD-worktrees/t1/build/visionfive2/rootfs` で確かめた。**virt の riscv64 の image（plan/ws256/tests/config-riscv64-virt*.mk）でも同じく出る**（一覧に ifconfig・mkdir が無い）。
- 直し: `Makefile` の essential の一覧に `chown dhcpc ifconfig mkdir route` を足した（注記に理由）。全部の platform に効く。userland の network の program の build の規則を持たない platform（ppc32・ppc64・m68k・riscv32）は、前から networkd を一覧に持っていて、この変更で build の対象は増えない（規則が無いので作られない）。
- 確かめ: VF2 の build（`make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p015-vf2 -j16 …/vmunix …/root.img …/uefi/BOOTRISCV64.EFI …/disk.img`）exit 0、warning 0（build/tmp/p015-vf2-1.log）。root の `/sbin` に `dhcpc ifconfig route`、`/bin` に `chown mkdir`。virt（`plan/ws256/tests/config-riscv64-virt-pci.mk`、`BUILD=build/p015-virt`）も同じく `/sbin/ifconfig` あり（build/tmp/p015-virt-1.log、warning 0）。画面で `net: UP` が消える事は T1 の走り A・C で見る（未実施）。
- 未確認: riscv64 の `ifconfig` の ioctl（`SIOCGIFINDEX` など）が kernel で通るか。LP64 の struct ifreq は arm64 と同じ形なので通る見込み。走り A の `ifconfig lo0` で見る。

## (b) 実装（2026-10-19、P2、commit 093814166）

| file | 中身 |
| --- | --- |
| `platform/riscv64/tools/make-riscv64-disk-image.py` | `--root-ufs FILE`（UFS の root を ESP の次の partition に置く。GPT の type は FreeBSD UFS 516E7CB6-…、label `zedBSD-root`、amd64 の native の image と同じ）、`--scratch-mib N`（最後に未使用の partition、label `zedBSD-scratch`、zedBSD 独自の type。design §3.3 の p014 の raw の読み書きの試験用）、`--spl FILE --uboot-itb FILE`（VF2 の firmware: p1 SPL（type 2E54B353-…、LBA 4096〜8191）、p2 U-Boot の itb（type BC13C2FF-…、LBA 8192〜16383、SPL が partition 2 を読む）。その時 ESP は LBA 16384 から p3、root は p4、scratch は p5）。`--root-ufs` と `--root-image` は同時に断る、firmware は 2 つ揃いで、partition に入らない大きさは断る。ESP の FAT の serial は今まで通り 5A45-4253。出典は docstring（UEFI 2.10、U-Boot v2025.01 の `doc/board/starfive/visionfive2.rst`・defconfig） |
| `platform/riscv64/tools/make-riscv64-root-image.py` | `--raw`: MBR と boot の FAT を付けず UFS の root だけを書く（marker の検査は同じ） |
| `platform/riscv64/zedbsd-native.cfg`（新規） | `kernel=vmunix`、`rootpart=PARTLABEL=zedBSD-root`。loader は `boot0=UUID=5A45-4253` を前に足す（`zedbsd-config.c`）。ESP に `root.img` を置かないので loader は ramdisk を読まない（`bootriscv64.c` の `load_initrd`） |
| `platform/riscv64/rootfs.mk` | `$(BUILD)/root.ufs`（`--raw`、`RISCV64_NATIVE_ROOT_MIB` 既定 256 MiB） |
| `platform/riscv64/disk-image.mk` | `$(BUILD)/native.img` と phony `native-image`: ESP（loader・native.cfg・vmunix）＋ root（root.ufs）＋ scratch（`RISCV64_NATIVE_SCRATCH_MIB` 既定 64）。`RISCV64_SPL`・`RISCV64_UBOOT_ITB` を両方渡すと firmware の 2 partition を前に置く（既定は空で firmware 無し: 板の QSPI の U-Boot から起動する形）。今までの `disk.img`（ramdisk の道）は不変 |
| `src/kern/platform/visionfive2.c` | ramdisk が無い時、SD card（mmcblk0）を boot device（bios 0x80、`KERN_DEV_BOOT_ORIGIN`）として出し、`kern_platform_block_device` が SD を返す。log `visionfive2: no ramdisk; the boot device is mmcblk0`。ramdisk がある時は今まで通り（ramdisk が boot device） |
| `src/kern/platform/riscv-virt.c`・`platform/riscv64/vmunix.mk`（virt の行） | virt の partition の scheme を MBR から `pcat_auto` に（ramdisk の root.img は MBR のまま読む、NVMe の native の image の GPT も読める）、virt の object に `gpt.c`・`pcat.c`。QEMU で同じ道（initrd 無しで `rootpart=` の root を disk から mount）を確かめるため（下の走り D） |

- root の見つけ方: `rootpart=` があるので kernel の `vfs.c` は legacy の autoroot（riscv64 で `VFS_LEGACY_NULL_AUTOROOT`）を使わず、amd64 の native と同じ道を通る: `kern_boot_source_context_mount` が `boot0=UUID=5A45-4253` を block identity で SD の ESP（mmcblk0p3、firmware 無しなら p1）に解決して私的に mount → `vfs_resolve_native_root` が `PARTLABEL=zedBSD-root` を GPT の label で mmcblk0p4（firmware 無しなら p2）に解決 → `vfs_mount_native_root` で UFS を read-write で mount。kernel の `vfs.c`・`boot.c` は変えていない。riscv64 でこの非 legacy の道を通るのは初めて（今までは全部 ramdisk の legacy の autoroot）。
- `rootpart=` が無い時（ESP に普通の `zedbsd.cfg` を置いた SD）の予備: legacy の autoroot が boot device（SD）の partition 1 を boot partition とし、兄弟の UFS で marker のある物を root にする。firmware 付きの layout では partition 1 は SPL だが、root の探し方は p1 以外の兄弟を見るので root（p4）は見つかる見込み（試していない）。
- 書いていない物: SD の root の上の swap（`swap0=`）、fstab、eMMC、`/boot/efi` の扱いの変更（amd64 と同じ共通の code に任せた）。

## 試験（2026-10-19、P2、host と build。QEMU・実機は無し）

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| native の image の host 試験（新規） | `plan/ws256/tests/native-image/run.sh build/p015-host` | exit 0、PASS 40・FAIL 0。小さい木から `--raw` の UFS（16 MiB）、firmware 無し・有り（stand-in の SPL・itb）の 2 つの image: GPT の header と entry の CRC、partition の type の並び、ESP の開始（2048・16384）、root の label・開始・大きさ・中身が root.ufs と一致・UFS の validate・marker・file、scratch の label と大きさ、ESP の file（loader・cfg・vmunix、root.img は無し）と serial 5A45-4253、SPL・itb の位置と byte。断る場合 3 つ（root 2 つ、firmware の片方、大きすぎる itb） |
| VF2 の vmunix と image | `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p015-vf2 -j16 build/p015-vf2/vmunix build/p015-vf2/disk.img build/p015-vf2/native.img` | exit 0、`warning:` 0（build/tmp/p015-vf2-2.log）。native.img 404,750,336 byte、`fdisk -l`: p1 EFI System 2048〜133119、p2 FreeBSD UFS `zedBSD-root` 133120〜657407、p3 `zedBSD-scratch` 657408〜788479。ESP: `EFI/`・`zedbsd.cfg`（2 行）・`vmunix`、serial 5A45-4253 |
| virt（PCI）の vmunix と image | `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt-pci.mk BUILD=build/p015-virt -j16 build/p015-virt/vmunix build/p015-virt/disk.img build/p015-virt/native.img` | exit 0、`warning:` 0（build/tmp/p015-virt-1.log） |

- 試験 `plan/ws256/tests/native-image/` を回帰に残すかは Q1 の判断（image の道具を変える時に使う。残すなら Master の Tools・試験の一覧へ）。
- 未実施: QEMU（T1 へ、下）、実機、firmware 付きの image の本物の SPL・itb（host に `u-boot-starfive` が無い）。

## T1 への依頼文（Q1 が番号を付けて送る）

> WS256 p015 と q1295 (a)（P2、2026-10-19）。tree は P2 の commit 093814166 以降（Q1 の merge の後の main）。login は今までと同じ root/root。
> 1. 走り A（virt の直の起動、(a) の確かめと回帰）: `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt.mk -j16 build/riscv64-virt/vmunix build/riscv64-virt/root.img`、`BOOT_MODE=riscv64-virt INITRD=build/riscv64-virt/root.img BOOT_THEN=$'type:root\nexpect:[Pp]assword\ntype:root\nexpect:^root@\\S+:~\\$\ntype:uname -m\nexpect:^riscv64\\s*$\ntype:ifconfig lo0\nexpect:inet 127\\.0\\.0\\.1 netmask 255\\.0\\.0\\.0\ntype:ls -ld /home/kei\nexpect:^drwx------' plan/tools/boot-test.sh build/riscv64-virt/vmunix`。合格: login・`riscv64`・`lo0: flags=UP,…` の次の行に `inet 127.0.0.1 netmask 255.0.0.0`・`/home/kei` が 0700。boot の画面に `net: UP: No such file or directory`・`init: oneshot networking failed`・`mkdir: not found`・`homes: could not make` が無い（PNG で Q1 が見る）。
> 2. 走り D（新規、initrd 無しで disk の root から起動する道: virt の NVMe に native の image）: `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt-pci.mk BUILD=build/t1-p015-virt -j16 build/t1-p015-virt/native.img`、image を複写して NVMe 用にする（`cp build/t1-p015-virt/native.img build/t1-p015-virt/native-nvme.img`。U-Boot の virtio-blk と kernel の NVMe が同じ file を開かないため）、`BOOT_MODE=riscv64-uboot QEMU_EXTRA="-drive if=none,id=nv,file=$PWD/build/t1-p015-virt/native-nvme.img,format=raw -device nvme,drive=nv,serial=p015" BOOT_THEN=$'type:root\nexpect:[Pp]assword\ntype:root\nexpect:^root@\\S+:~\\$\ntype:mount\nexpect:nvme\\S* on / type ufs\ntype:echo p015 > /root/p015.txt; sync; cat /root/p015.txt\nexpect:^p015\\s*$' plan/tools/boot-test.sh build/t1-p015-virt/native.img`。合格: login・`mount` の `/dev/nvme…p2 on / type ufs`・書いた file が読める。画面に `vfs: rootpart selector PARTLABEL=zedBSD-root resolved to /dev/nvme…p2` が写れば記録（scroll で消えても可）。**見込みの外れ**: U-Boot が PCI を enumerate した後の ECAM の再 enumerate で NVMe が見えない、または `vfs: resolve rootpart selector failed (error N)`・`vfs: boot0 … failed` で `VFS initialization failed` → 画面の PNG を添えて FAIL（解析しない）。U-Boot は `boot_targets` で nvme0 を先に見るので NVMe の ESP から起動しても可。
> 3. 走り C（VF2 の kernel、ramdisk の道の回帰、T1-729 の C と同じ）: `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk -j16 build/visionfive2/vmunix build/visionfive2/root.img build/visionfive2/uefi/BOOTRISCV64.EFI build/visionfive2/disk.img`、`BOOT_MODE=riscv64-uboot-gop BOOT_PATTERN=$'^(?:\\S+ )?login: ?' plan/tools/boot-test.sh build/visionfive2/disk.img`。合格: bochs の画面の中央に login prompt、その上に `net: UP: …`・`init: oneshot networking failed`・`homes: could not make` が無い（Q1 が PNG で見る）。
> 結果: 各 PNG・PASS/FAIL・image の commit。

## 実機の手順（板の接続の後。ユーザーが framebuffer を目で見る: 2026-10-19 の決定 #10・#15）

前提: p011 の GOP の道で画面が出る事（出ない・#20 が (b) の時は UART の対話での受け入れ #27 をユーザーに聞く）。HDMI の monitor と USB keyboard（p020 の VL805）を板に。

1. firmware（板の SD から起動する時だけ。板の QSPI の U-Boot で起動するなら飛ばす）: Q1 が `sudo apt install u-boot-starfive`（Debian trixie 2025.01-3+deb13u1）。file の場所を `dpkg -L u-boot-starfive | grep -E 'u-boot-spl.bin.normal.out|u-boot.itb'` で確かめる（`/usr/lib/u-boot/starfive_visionfive2/` の見込み、未確認）。
2. image: `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk -j16 build/visionfive2/native.img`（firmware 付きは `RISCV64_SPL=<SPL の path> RISCV64_UBOOT_ITB=<itb の path>` を足す）。`/usr/sbin/fdisk -l build/visionfive2/native.img` で partition を見る（firmware 付き: p1 spl・p2 uboot・p3 EFI system・p4 zedBSD-root・p5 zedBSD-scratch。無し: p1 EFI system・p2 zedBSD-root・p3 zedBSD-scratch）。
3. SD に書く（host、Q1）: `lsblk` で SD の device（/dev/sdX）を確かめ、`sudo dd if=build/visionfive2/native.img of=/dev/sdX bs=4M conv=fsync status=progress`。**device を取り違えない**（host の disk を消す）。GPT の予備の header は image の最後（card の最後ではない）にあり、Linux が警告しても zedBSD は主の header だけを読む。
4. 板: firmware 付きなら boot の switch（RGPIO_1/0）を `01`（SD）に、QSPI の U-Boot で起動するなら `00`（flash）のまま（U-Boot が SD の ESP の `EFI/BOOT/BOOTRISCV64.EFI` を見つける）。SD を挿して電源を入れる（Q1 が SwitchBot で）。
5. 画面で見る行（kernel の log、上から）:
   1. p014 の行（[phase014](../phase014/phase.md)「p015 で画面に出る行」の 1〜6）: `vf2-mmc: sample phase …`、`dw-mmc: mmcblk0 controller version …`、`dw-mmc: mmcblk0 SDHC card …`、`dw-mmc: mmcblk0 N blocks of 512 bytes (M MiB)`、`dw-mmc: mmcblk0 bus 4-bit, 49500000 Hz, high speed`、`visionfive2: SD card is mmcblk0`。
   2. `visionfive2: no ramdisk; the boot device is mmcblk0`、`boot: platform devices detected: 1`。
   3. `visionfive2: mmcblk0 has 5 partitions`（firmware 無しは 3）と各 partition の行（`"spl"`・`"uboot"`・`"EFI system"`・`"zedBSD-root"`・`"zedBSD-scratch"`）。
   4. `boot: parameters: boot0=UUID=5A45-4253 rootpart=PARTLABEL=zedBSD-root`。
   5. `vfs: native boot disk=mmcblk0 physical disks=N`、`vfs: scan mmcblk0 … 5 entries`（無しは 3）と `vfs: mmcblk0 partition K start=… data=… blocks=…` の行。
   6. `vfs: boot0 UUID=5A45-4253 -> /dev/mmcblk0p3 (private FAT)`（無しは p1）、`vfs: rootpart selector PARTLABEL=zedBSD-root resolved to /dev/mmcblk0p4`（無しは p2）。
   7. `boot: … the VFS is initialized`、`boot: starting init /sbin/init`、init の行（`net: UP` と `oneshot networking failed` が無い事）、`login:`。
   8. 出てはいけない行: `vfs: resolve rootpart selector failed (error N)`、`vfs: boot0 … failed (error N)`、`VFS initialization failed`、`vfs: the root on mmcblk0p4 cannot be mounted read-write`（READ-ONLY の行）、`dw-mmc: read of … failed`・`write of … failed`、`visionfive2: no SD card disk`。出た時は行と errno を記録する。
6. USB keyboard で root/root で login し: `mount`（`/dev/mmcblk0p4 on / type ufs`）、`echo p015 > /root/p015.txt; sync`、`reboot` の後にもう一度 login して `cat /root/p015.txt` が `p015`。
7. design §3.4 の受け入れ: 6 を含めて 10 回の起動と login（電源の入れ直しは Q1）。

## 残り

- T1 の走り A・C・D の結果を Q1 が判定（それまで cleared にしない）。
- 実機の確かめ（上の手順、板の接続・#10・#15・#27 の後）。firmware 付きの image は本物の SPL・itb で確かめていない。
- `rootpart=` の無い SD（legacy の autoroot の予備）は試していない。
- 試験 `plan/ws256/tests/native-image/` の扱い（Q1）。

## T1-735 の結果（2026-10-19 Q1）

- A（virt の直の起動）PASS: login・uname -m・lo0 の 127.0.0.1・/home/kei の 0700、net: UP・networking failed・homes の失敗は出ない（net UP の直しが効いた）。
- C（VF2 の kernel の GOP）PASS。amd64 の既定の image PASS。pcat・pc98 の build PASS。
- D（virt の native.img を NVMe で U-Boot から）FAIL（2 回）: `vfs: rootpart selector PARTLABEL=zedBSD-root resolved to /dev/nvme0n1p2` の後に `ufs: nvme0n1p2: the volume refuses writes … a j3 transaction could not be committed (error 42)`、`nvme: I/O recovery failed (42); disk unavailable, DMA retained`、`kern: pid 1 killed by signal 10`。`native boot disk=none` も。未解析。
- x68k の build が FAIL: Makefile の必須の program に足した dhcpc の規則が x68k に無い（P2 に直しを頼んだ）。
- 再開: D の解析（NVMe の I/O の回復の失敗、U-Boot が NVMe を動かしたまま渡す件と INTx の道を疑う）と x68k の回帰の直し。

## x68k の build の回帰の調べと直し（2026-10-19 P2、T1-735 の x68k）

- 原因: x68k の root（`$(BUILD)/rootfs/.stamp`、既定の goal に入る）は platform/common/userland-rootfs.mk の `ZEDBSD_ROOTFS_COMMANDS` をそのまま使い、それは network の class の program（`USERLAND_SELECTED_NETWORK_PROGRAMS`）を含む。x68k の vmunix.mk は basic の class にしか link の規則を作らない。
- 「net・networkd は以前通っていた」の答え: **通っていなかった。** p015 の前の Makefile（46518392b^）で `make -n` の既定の goal は `No rule to make target '…/bin/net', needed by '…/rootfs/.stamp'` で止まる（sort した一覧の先頭の欠けが net から dhcpc に変わっただけ）。x68k の root は 1b3d6cd03（2026-10-11）から既定の goal で build できていない。T1 の以前の x68k の確かめ（master.md の Tools）は `vmunix` と `zedbsd-x68k.hd` だけを作り、root を作らない。
- 直し（8ffa62e1b、platform/x68k/vmunix.mk だけ）: sparcv9 と同じ形で、x68k の root の command を `sh sysctl mount umount` と basic の program に限る（network の program は x68k に network の device が無く規則も無いので入れない）。mode も同じ一覧から作る（無い file への `--mode` は rootfs の規則が error にする）。共有の userland-rootfs.mk と Makefile は変えていないので、riscv64（VF2）の root は変わらない。
- 確かめ:
  - `make -j16 BUILD=build/p2-x68k ZEDBSD_CONFIG=plan/tools/ports/config-x68k.mk build/p2-x68k/vmunix build/p2-x68k/zedbsd-x68k.hd` → exit 0、warning 0、`m68k kernel check: PASS`・`X68k image check: PASS`（master.md の Tools の x68k の確かめ）。
  - 既定の goal（`make -k -j16 BUILD=build/p2-x68k ZEDBSD_CONFIG=plan/tools/ports/config-x68k.mk`）: dhcpc・net の規則の欠けは消え、chown・mkdir を含む basic の program は link できる。**まだ exit 2**: `bin/lp`・`bin/lpr`・`bin/ntpdate` の link が `undefined reference to '__m68k_read_tp'`（9 件、userland/base/libc/resolver.c の `static __thread` の 2 変数）。これも p015 の前からある（resolver.c の `__thread` は 846464d1e、2026-10-03。lp・lpr・ntpdate は p015 の前から必須の一覧にある）。m68k の gcc は TLS の読みを `__m68k_read_tp` の呼び出しにし、x68k の static の runtime にそれが無い。
  - VF2: `make … ZEDBSD_CONFIG=config/ci/config-visionfive2.mk --eval` で `ZEDBSD_ROOTFS_FILES` に `/sbin/ifconfig=…/bin/ifconfig`・`/sbin/route`・`/sbin/dhcpc`・`/bin/chown`・`/bin/mkdir` と mode 0755 がある。加えて `make -j16 BUILD=build/p2-vf2 ZEDBSD_CONFIG=config/ci/config-visionfive2.mk build/p2-vf2/rootfs/.stamp` が exit 0・warning 0 で、`rootfs/sbin/ifconfig`（0755）・`sbin/route`・`sbin/dhcpc`・`bin/chown`・`bin/mkdir` がある。
  - 他の port: m68k-virt・mips32-malta・mips64-malta・ppc32-mac99・ppc64-pseries・riscv32-virt の `vmunix`（master.md の Tools の target）が exit 0・warning 0（userland を作らないので p015 の変更の影響は無い。既定の goal は前から `No rule to make target 'disk-image'` で、p015 とは無関係）。sun4u の既定の goal（disk-image、sparcv9 は前から自分の root の一覧）と pc98 は `make -n` で止まらない（T1-735 で pc98 は PASS）
- 残り（Q1 の判断）: x68k の lp・lpr・ntpdate の `__m68k_read_tp`。案 (a) x68k の user の runtime に `__m68k_read_tp` を足す（static-tls.c の `KERN_THREAD_SELF_GET_TLS` の syscall で読む。exec が m68k の PT_TLS の block を作るかの確かめと MAME での確かめが要る、WS268 の範囲）、(b) resolver.c の `__thread` を random.c・ftw.c と同じ `KERN_DYNAMIC_LIBC || KERN_STATIC_TLS` の guard に（guard の無い sparcv9 でも TLS で無くなるので要確認）、(c) x68k の root から 3 つを外す。x68k の vmunix.mk の `-DZEDBSD_STATIC_TLS` はどの source も見ていない（`KERN_STATIC_TLS` の書き違いの疑い、未確認）。

## q1301（2026-10-19 P2）: x68k の全部の base の build と T1-735 の D の解析

### (a) x68k: userland/base の program を他の arch と同じく全部 build して root に入れる（commit b2479377c → db658a2df）

- 経緯: b2479377c（lp・lpr・ntpdate を x68k の root から外す）はユーザーの指示（2026-10-19「x68kにもNICは実在するらしいので、lp,lpr,ntpdateもビルドが通るようにしましょう。」「userland/base/はよほどのことがない限りアーキごとに存在する・しないで分けたくないです。」）で取り消し、db658a2df で置き換えた。8ffa62e1b の「x68k は basic だけ」もこれで置き換え。
- 直し（`platform/x68k/vmunix.mk`・`platform/x68k/user.ld`・新規 `userland/base/libc/m68k-read-tp.c`）:
  - network の class の program（選ばれた物: dhcpc ifconfig net networkd route wifi）の link の規則を足した（riscv64 と同じく `net/netutil.o`・`net/dhcp.o` を付ける）。root は共通の `ZEDBSD_ROOTFS_INPUTS`・`ZEDBSD_ROOTFS_FILES`（他の platform と同じ一覧・mode・data）に戻した。
  - `__m68k_read_tp`（m68k の gcc が `__thread` の読みに呼び、結果を A0 から取る）を x68k の user の runtime に足した。`thread_self(KERN_THREAD_SELF_GET_TLS)` が返す control block（`struct __rtld_tcb`）の後の `KERN_TLS_TCB_RESERVE` の後に static TLS の block を置き、thread pointer = block + 0x7000（m68k の TLS の ABI の variant I）。objdump で D0 と A0 の両方に返す事、link した lp で `service_state`（block の offset 0）の参照が `addal #-28672` になる事（linker の TP = block + 0x7000）を確かめた。
  - `user.ld` に PT_TLS を足した（amd64 の user.ld と同じ形）。無いと `.tbss` が `.bss` に重なる orphan になり、TLS の program が実行時に control block の reserve の外を黙って読み書きする。PT_TLS があれば kernel は今は exec を ENOEXEC で断る（下の残り）。
- 確かめ: `make -k -j16 BUILD=build/p2-x68k ZEDBSD_CONFIG=plan/tools/ports/config-x68k.mk`（既定の goal）exit 0、`warning` 0（build/p2-x68k-default4.log）、`X68k image check: PASS`。root の `/bin` に lp・lpr、`/sbin` に dhcpc ifconfig net networkd ntpdate route wifi。m68k-virt の `vmunix`（`plan/tools/ports/config-m68k-virt.mk`、BUILD=build/p2-m68k-virt）exit 0・warning 0。
- `-DZEDBSD_STATIC_TLS` の綴りの疑い: x68k だけの書き違いではない。同じ名前を `toolchain/llvm/sysroot.mk`（全部の clang の arch の libc）と sparcv9 が渡し、どの source も見ない。source が見る `KERN_STATIC_TLS`（random.c・ftw.c・pthread.c・utmpx.c・account.c・crypt.c）はどこでも定義されない。x68k だけ直すと他と違う形になるので変えていない。両方の名前をどうするか（死んだ guard か）は Q1 の判断（toolchain の sysroot.mk は P2 の範囲の外）。
- **残り（実行時、計画に無い依存）**: kernel の exec（`src/kern/elf.c` の `load_static_tls`）は static の program の PT_TLS を amd64・i386 にしか作らず、他の arch では ENOEXEC。x68k は static だけなので、lp・lpr・ntpdate は link できるが exec されない（実機・MAME での動作の確かめは今回の範囲外、ユーザー指示）。走らせるには: m68k の variant I の配置（mapping の先頭に control block の reserve、その後に TLS の block、`hal_task_set_tls` には control block の address）を elf.c に足す事と、`static-tls.c` の `__rtld_thread_alloc`（x86 の配置で template を写す）の m68k の形。WS268 か新しい Phase で。
- sparcv9 にも同じ「basic だけ」の root の形がある（`platform/sparcv9/vmunix.mk`）。ユーザーの担当なので触っていない。

### (b) T1-735 の D の FAIL の解析と直し（commit d0869aca7、`src/drivers/pci/pci-nvme.c`）

- 方法: QEMU（`qemu-system-riscv64 -machine virt -smp 4`、U-Boot qemu-riscv64_smode、virtio-blk に native.img、NVMe に native.img の複写の raw）を gdbstub 付きで起動し、gdb で `nvme_io_fail_all_locked` に break。controller の構造体の offset は pci-nvme.c を `-g` で compile した object の `ptype /o` で取った。serial・console の log は使っていない（serial は login と command の入力だけ、画面は QMP の screendump）。
- 再現: 直しの前は 2 回中 2 回 break（どちらも read の `nvme_io_wait_completion` の ETIMEDOUT。error 42 は ETIMEDOUT、include/uapi/errno.h）。break の時、I/O の completion queue の head（6）と次の 2 つの entry の phase bit が driver の phase（1）と一致＝completion は posted されているのに handler が走っていない。PLIC の pending（0x0c001000）は 0。
- 原因: QEMU の PLIC（hw/intc/sifive_plic.c、QEMU 10.0）は source の線が上がった時にだけ pending を立て、claim で消し、complete の時に線がまだ high でも pending を立て直さない。QEMU の NVMe の INTx は completion queue に未消費の entry がある間 high のまま。handler が queue を読み終えて doorbell を書く前に次の entry が posted されると、線は high のまま claim・complete を越え、立ち上がりが二度と来ず、その後の I/O と回復の admin の command（`nvme: I/O recovery failed (42)`）がすべて timeout する。U-Boot の NVMe の受け渡し（CC.EN）と errno の読み違いは原因ではない（admin・最初の read は割り込みで完了している）。`native boot disk=none` は loader の handoff に disk が無い時の情報の log（rootpart= で探す道）で、異常ではない。
- 直し: `nvme_irq` が admin と I/O の queue を、1 回の pass で何も消費しなくなるまで繰り返し読む。最後の pass は最後の doorbell の書き込みの後に両方の queue が空なのを見るので、その時点で線は下がっており、次の entry は新しい立ち上がりを作る。command の投入は command_lock を要るので、pass は queue の深さの内で終わる。実機の PLIC（level の source を complete の後に再び見る）と MSI-X では余分の 1 pass だけ。
- 確かめ: 直しの後、同じ形で 4 回起動（gdb の break を張ったまま）、break は 0 回。うち 2 回で login して `dd` で 1 MiB の file を 8 つ書き `sync`、`cat /etc/passwd > /root/p; sync; cmp` が一致（CMP-OK）。1 回は login まで、1 回は serial の入力の取りこぼしで login できず（画面で `Login incorrect`、NVMe とは無関係、break 0）。build: virt（`plan/ws256/tests/config-riscv64-virt-pci.mk`、BUILD=build/p015-virt）・VF2（`config/ci/config-visionfive2.mk` の vmunix、BUILD=build/p015-vf2）・amd64（`config/ci/config-amd64.mk` の vmunix、BUILD=build/p2-amd64、`amd64 vmunix check: PASS`）が exit 0・warning 0。NVMe の irq の道の host 試験は無い。
- 証拠の file（P2 の worktree）: `build/p2-nvme/gdb2.out`（break の時の queue と PLIC）、`build/p2-nvme/watch2.out`〜`watch4.out`・`run2.out`・`run3.out`。

### T1 への依頼文（Q1 が番号を付けて送る）

> WS256 p015 の D のやり直しと x68k の build（P2、2026-10-19、q1301）。tree は P2 の commit db658a2df 以降（Q1 の merge の後の main）。
> 1. 走り D（T1-735 の D と同じ command）: `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt-pci.mk BUILD=build/t1-p015-virt -j16 build/t1-p015-virt/native.img`、`cp build/t1-p015-virt/native.img build/t1-p015-virt/native-nvme.img`、`BOOT_MODE=riscv64-uboot QEMU_EXTRA="-drive if=none,id=nv,file=$PWD/build/t1-p015-virt/native-nvme.img,format=raw -device nvme,drive=nv,serial=p015" BOOT_THEN=$'type:root\nexpect:[Pp]assword\ntype:root\nexpect:^root@\\S+:~\\$\ntype:mount\nexpect:nvme\\S* on / type ufs\ntype:echo p015 > /root/p015.txt; sync; cat /root/p015.txt\nexpect:^p015\\s*$' plan/tools/boot-test.sh build/t1-p015-virt/native.img`。2 回流す。合格: 2 回とも login・`/dev/nvme…p2 on / type ufs`・書いた file が読める。画面に `nvme: I/O recovery failed`・`the volume refuses writes` が無い。
> 2. 回帰: amd64 の既定の image の起動（NVMe の irq の handler を変えたため）と、走り A（T1-735 の A と同じ command）。
> 3. x68k の build だけ: `make -j16 ZEDBSD_CONFIG=plan/tools/ports/config-x68k.mk BUILD=build/t1-x68k` が exit 0・warning 0、`X68k image check: PASS`、`build/t1-x68k/rootfs/sbin/networkd` と `build/t1-x68k/rootfs/bin/lp` がある事。
> 結果: 各 PNG・PASS/FAIL・image の commit。

## T1-739 の結果（2026-10-19 Q1）

D（virt の native.img を NVMe で U-Boot から）2 回とも PASS: `/dev/nvme0n1p2 on / type ufs (rw)`、書いて sync して読み戻し。A・amd64 の NVMe の回帰 PASS、x68k の build PASS。QEMU の部分は済み、実機の SD の起動はユーザーの目視を待つ。
