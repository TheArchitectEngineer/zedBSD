<!-- awesome-plan project=zedbsd record=ws256-p015 -->

# ws256-p015 rootfs からの起動（SD の root、板の前の code）

Status: in-progress（2026-10-19 P2、q1295-i01。板の前の code と host 試験・build は済み。T1 の QEMU の確かめ（下の依頼文）と実機の確かめ（ユーザーの目視）を待つ）

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
