<!-- awesome-plan project=zedbsd record=ws289-p006 -->

# ws289-p006 ZYBO の SD からの起動（SPL の Falcon の BOOT.BIN、FIT の vmunix.itb ＋ system.dtb、`make sd-image`、`deploy.sh`）

- 親: [ws289](../ws.md)。設計: [design.md](../design.md) 版 2 の §5（SD からの起動）・§6・§7・§8 の p006 の行・§9 の p006 の行・§10 の R5〜R8・R13・R18・R19。
- Queue: Q1 の割当（P3、2026-10-23、worktree `/home/awe/zedBSD-worktrees/p3-ws289-p006`、branch `agent/p3-ws289-p006`、基点 main d3c9f6505）。Fable は使わない（ユーザー）。
- 判断と許可: U1（JP5 の jumper、card の差し込み、host の `dd` はユーザー。Q1 が頼む）・U2（復旧は JTAG だけ、U-Boot 本体は FAT に入れない）はユーザー「はい」で推しのとおり（design §11）。WS286 の U3（ZYBO の実機は生きた serial で判定）・U4（担当が板を JTAG で直に操作、板の lock）。U-Boot は tree の外（`build/` の中）で build し、commit・配布しない（design §12）。host の block device には書いていない。toolchain・HAL の API は変えていない。共有の LLVM・NoctLang は main の物への symlink で読むだけ。
- Status: in-progress（実装と host の確認、実機の JTAG の道の確認は済み。QEMU は T1 の結果待ち。**実機の SD からの起動は未実施**: 板の今の microSD は ACMD41 が busy から出ず初期化できない（p004、U-Boot でも同じ。ユーザーの試験 U41）。card が使えるようになったら下の「実機の SD 起動の手順」）

## 受け入れの条件（design §8.2・§8.3・§9 の p006）

1. zybo・armv7-virt・amd64・rpi4 の kernel の build が warning 0。`zybo-sd-image-host-test` PASS（FAT の 3 つの file、FIT の load・entry = 0x00100000、DTB の bootargs）。
2. QEMU xilinx-zynq-a9（T1、生きた serial ＋ SSH）: `make sd-image`（BOOT.BIN 無し）の image と、その FAT の `system.dtb` で、SD の root・login・SSH（p004・p005 の回帰）。`deploy.sh` で `/boot/boot0` の `vmunix.itb` を入れ替え、FAT が host の mtools で読めて中身が一致。BootROM → SPL の鎖は QEMU では確かめられない（design D7）。
3. 実機の ZYBO（JP5 = SD、ユーザー、JTAG 無し）: 電源で `U-Boot SPL 2025.07` → kernel → `login:`、root が SD、SSH、`deploy.sh` で入れ替えて `reboot` → 新しい物で起動、`reboot` をもう 1 回。JP5 を JTAG に戻して JTAG の起動が生きている。

## 決めた事: 既定の `config-zybo.mk` に network と SSH を入れた

- 決定: `config/ci/config-zybo.mk` の `ZEDBSD_USER_PROGRAMS` に `dhcpc hostname ifconfig net networkd ping route ca-certificates openssh openssl zlib` を足した（p005 の試験の config [tests/config-zybo-ssh.mk](../tests/config-zybo-ssh.mk) と同じ組）。
- 理由: (1) この WS の到達点は単独の起動で、kernel の更新は SSH（design §5.6、ユーザーの最初の希望「SDブートで、SSHでカーネルを更新して」）。`make sd-image` の既定の image が SSH 無しだと、SD で起動した板の kernel を入れ替える道が無い（JTAG に戻すしかない）。(2) RAM: SD の起動では root は SD の UFS で ramdisk を使わない。sshd・networkd の常駐は数 MiB で、512 MiB の板で問題にならない（JTAG の実機で `ARMV7 MEMORY base=100000 total=511 MiB free=487 MiB`）。(3) 費用は JTAG の ramdisk の流れの方: OpenSSH を含む root は 16 MiB（`root.img` 17 MiB）になり、JTAG の load が約 20 s → 69 s に増える。`rootfs.mk` の `ARMV7_ROOT_MIB` の既定を「openssh が有れば 16、無ければ 4」にした（config の file は menu の形のまま）。build は OpenSSL・OpenSSH の分だけ長くなる（CI は zybo を build しない）。
- 付随: OpenSSH の有る root に `mv` を足した（`deploy.sh` が `/boot/boot0` の file を rename する。`chmod mkdir` と同じ条件、`platform/armv7/userland.mk`）。
- `tests/config-zybo-ssh.mk` は既定と同じになったので不要（削除と master.md の Tools の行の直しは Q1 へ、下の「残り」）。

## 作った物

- `plan/tools/zybo/spl-build.sh`（28ca50933）: design §5.2 の fragment（`SPL_FS_LOAD_KERNEL_NAME="vmunix.itb"`、`SPL_FS_LOAD_ARGS_NAME="system.dtb"`、`SPL_PAYLOAD_ARGS_ADDR=0x01f00000`、`SPL_BSS_START_ADDR=0x1f000000`、`SPL_STACK_R_ADDR=0x1ff00000`、`SPL_HAS_CUSTOM_MALLOC_START=y`、`SPL_CUSTOM_SYS_MALLOC_ADDR=0x1f100000`、`SPL_SYS_MALLOC_SIZE=0xc00000`）。merge の後に fragment の**全部の行**を `.config` で確かめ（L14）、`SPL_OS_BOOT=y` も確かめる。`spl/boot.bin`（BootROM の image = BOOT.BIN）も作る。大きさ ≤ 0x30000（OCM、R6）と `__bss_start` = `0x1f000000` を確かめる。BUILD の引数が相対 path だと `merge_config.sh`（source の tree で走る）が fragment を見つけられなかった bug を直した（path を絶対に）。1 つの SPL を JTAG と SD の両方に使う。
- `platform/armv7/tools/make-zybo-boot-files.py`（新規、Zlib）: `fit --kernel vmunix.bin --elf vmunix OUT`（ITS を書いて `dtc`。kernel の node だけ: `type kernel`・`arch arm`・`os linux`・`compression none`・`load`/`entry` = `0x00100000`・`data = /incbin/`。configuration `zybo` に `description`）と `dtb --dts zybo-sd.dts [--bootargs TEXT] OUT`（source の bootargs の後に TEXT を足す）。作る時と確かめる時の同じ検査（ELF の最下位の PT_LOAD = entry = `0x00100000`、raw の大きさ = ELF の file の bytes、BSS を含む kernel の終わり ≤ `0x01F00000`、raw の終わり＋64 KiB（SPL が append する自分の DTB）≤ `0x01F00000`、FIT ≤ 8 MiB（SPL の pool 12 MiB）、FIT の既定の configuration・`firmware` が無い・kernel の property・data = raw、DTB の `memory@0` = 512 MiB と `rootpart=/dev/mmcblk0p2`）。dtc が warning を出したら失敗。FDT と ELF の読みは Python で自前（外の library 無し）。
- `platform/armv7/zybo/zybo-sd.dts`（新規、Zlib）: `zybo-common.sh` の雛形と同じ `memory@0`・`/chosen`、bootargs = `rootpart=/dev/mmcblk0p2`。
- `platform/armv7/tools/check-zybo-sd-image.py`: `--kernel vmunix.bin [--bootarg P]...` で FAT の `vmunix.itb`・`system.dtb` を上の検査に通す。BOOT.BIN が有れば BootROM の header（`0xaa995566`・`XNLX`・checksum = 0x20〜0x44 の和の補数・image の offset と OCM に収まる大きさ）。
- `platform/armv7/disk-image.mk`: `$(BUILD)/vmunix.itb`・`$(BUILD)/system.dtb`（`ZYBO_BOOTARGS` の値は stamp で、変わった時だけ作り直す）、`sd-image` の FAT に常に `vmunix.itb`・`system.dtb`、`ZYBO_BOOT_BIN` が有れば `BOOT.BIN`、他は `ZYBO_SD_EXTRA_FILES`。image の検査は `--kernel` 付き。`zybo-sd-image-host-test` は SD 起動の file と mac0 の DTB を入れた image で PASS、FIT でない「kernel」と無い bootarg で checker が FAIL する事も確かめる。
- `plan/tools/zybo/deploy.sh KERNEL [--host] [--port] [--dtb] [--no-reboot]`（新規、Zlib、1a1ecc9e9）: FIT を `vmunix` の横に作り（検査付き）、板が armv7 で `/boot/boot0/vmunix.itb` が有る事を確かめ、`/boot/boot0/vmunix.itb.new` に scp → 板の `cksum` が一致 → `sync` → `vmunix.itb` を `.old` に、`.new` を `vmunix.itb` に rename → `sync` → `reboot`。`--dtb` は `system.dtb` を同じ道で。password は `SSHPASS`（`sshpass -e`）か ssh の key・prompt。host key は記録しない（card ごとに変わる）。
- `plan/tools/zybo/qemu-zynq.sh --dtb FILE`（1a1ecc9e9）: 作った tree の代わりに FILE（SD 起動の `system.dtb`）を `0x01F00000` に。`--append`・`--initrd` とは併用しない。

## SPL の Falcon の道（U-Boot v2025.07 の source で確かめた事、`build/p006-spl/u-boot-2025.07`）

- `spl_boot_device()` は SD の mode で `BOOT_DEVICE_MMC1`、`spl_mmc_boot_mode` は FAT が有るので FS の mode（RAW の道は通らない）、partition 1、`spl_start_uboot()` = 0 なので `spl_load_image_fat_os`: `system.dtb` を `0x01F00000` に読み（`SPL_ENV_SUPPORT` は無いので env の file の名前は見ない）、`vmunix.itb` を `spl_load` で読む。header は `TEXT_BASE − 64` = `0x03FFFFC0` に読む（kernel・DTB と重ならない）。
- FIT（`SPL_LOAD_FIT_FULL` は無い → `spl_load_simple_fit`）: **FIT 全体は SPL の malloc の pool に読む**（`spl_get_fit_load_buffer` = `malloc_cache_aligned`。design §5.1 の「`SPL_LOAD_FIT_ADDRESS` = `0x10000000` に読む」は v2025.07 では違う。pool が足りない時だけ `TEXT_BASE` = `0x04000000` に）→ FIT の上限 8 MiB を tool で確かめる。`fit_find_config_node` は **configuration に `description` が無いと失敗する**（design の ITS に無かった。入れて検査も）。`firmware` が無いので `kernel`、`os = linux` → `IH_OS_LINUX`、data は `load` へ memcpy（`load` より下に触れない）、`SPL_FIT_SIGNATURE` は無いので hash は見ない（hash の node は入れていない）。`os_takes_devicetree` で自分の control DTB を `ALIGN(load + size, 8)`（kernel の BSS）に写し `fdt_shrink_to_minimum(+8 KiB)`。jump の `r2` は `spl_image.arg` = `SPL_PAYLOAD_ARGS_ADDR`（FIT の道は arg を書き換えない）= `system.dtb`。`spl_fixup_fdt(0x01F00000)` は DTB 自身の memreserve を足す（R5、`ARCH_FIXUP_FDT_MEMORY` は無い）。`jump_to_image_linux`: r0 = 0、r1 = 0xffffffff、r2 = DTB、`cleanup_before_linux()`。その前に `spl_board_prepare_for_boot` = `ps7_post_config()`。
- `SPL_FPGA=y` の `spl_fit_load_fpga` は configuration に `fpga` が無ければ何もしない。`SPL_SYS_MALLOC_CLEAR_ON_INIT=y` は pool 12 MiB を消す（cache off の DDR で数百 ms 程度と見込む、未測定）。

## design からの差（Q1 の確認用）

1. FIT の読み先は SPL の pool（上）。design §5.1・R18 の `0x10000000` の staging は v2025.07 に無い。上限は tool の 8 MiB。
2. FIT の configuration に `description`（無いと SPL が `Missing FDT description in DTB` で止まる）。
3. FIT と DTB は image の tool（`make-zybo-sd-image.py`）でなく新しい `make-zybo-boot-files.py` が作る（`make` と `deploy.sh` が同じ物を使う。image の tool は FAT に file を置くだけのまま）。design の `--fit-only` の代わり。
4. `ZYBO_MAC` は作らず、`mac0=` も `ZYBO_BOOTARGS` で渡す（1 つの口）。
5. ELF の `e_entry` は物理の `0x100000`（仮想の `0x80100000` でない）: tool は物理の範囲を先に見る。
6. BOOT.BIN の header の image の長さは U-Boot の `mkimage` が file 全体の長さを書く（`zynqimage.c` の `image_size = st_size`）: checker は「offset から file の終わりまでが OCM に収まる」を見る。
7. `jtag-hello.sh` は tree に無い。同じ確かめを `jtag-boot.sh`（新しい SPL）で行った（下）。
8. root に `mv` を足した（deploy の rename）。
9. README（docs の howto）は書いていない: 道具（`spl-build.sh`・`deploy.sh`）が `plan/tools/zybo/` に有り、`docs/` は `plan/` を指せない（docs/style.md）。手順は下の「実機の SD 起動の手順」に置いた。docs に書くなら道具の置き場所を先に決める（Q1）。
10. （2026-10-23、T1 の 1 回目の後）boot の FAT は design L13 の FAT16 でなく **FAT32**（下の「T1 の 1 回目」）。kernel の FAT の driver は FAT16 で長い名前を扱わず、deploy の `.new`・`.old` が作れない。

## 確認

### host（2026-10-23 P3）

| command | 結果 |
| --- | --- |
| `plan/tools/zybo/spl-build.sh build/p006-spl` | rc 0、fragment の 11 行と `SPL_OS_BOOT=y` が `.config` に、`spl-build: 127253 bytes, board_boot_order 0x00000fd8, BSS 0x1f000000`、`u-boot-spl.bin` sha256 `e6ca79da…`、`boot.bin` 129496 byte sha256 `b743c8f8…`（header `aa995566 584c4e58`、offset 0x8c0、checksum `fd165fd1` を手で検算）。`make.out` に warning 0 |
| `make ZEDBSD_CONFIG=config/ci/config-zybo.mk BUILD=build/p006-zybo -j16 vmunix build/p006-zybo/root.img build/p006-zybo/root.ufs` | rc 0。tree の code の warning 0（warning は OpenSSL・OpenSSH の package の deprecated・unused と OpenSSH の Makefile、perl の locale だけ）。`vmunix.bin` 1032800 byte、kernel の PT_LOAD `0x100000`〜`0x7ce000`（BSS 込み）、`root.img` 17825792 byte（16 MiB の UFS）、root に `/bin/mv`・`/sbin/networkd`・`/usr/sbin/sshd` |
| `make … BUILD=build/p006-zybo ZYBO_BOOT_BIN=build/p006-spl/out/spl/boot.bin zybo-sd-image-host-test sd-image` | `ZYBO SD image check: PASS build/p006-zybo/tests/zybo-sd-image-test.img`、FIT でない kernel で `the FIT's kernel data is not the raw kernel`、無い bootarg で `… lacks rootpart=/dev/mmcblk0p3`（どちらも期待どおり FAIL）、`zybo-sd-image-host-test: PASS`、`ZYBO SD image check: PASS build/p006-zybo/zybo-sd.img` |
| `mdir -i build/p006-zybo/zybo-sd.img@@1048576 ::/` と MBR | FAT16 `ZEDBSD` に `vmunix.itb` 1033303・`system.dtb` 413・`BOOT.BIN` 129496。MBR p1 = active 0x80・type 0x06・LBA 2048・131072 sector、p2 = type 0xA5・LBA 133120・524288 sector（1 回目。FAT32 にした後は下の追記） |
| FAT32 にした後（2026-10-23、main cab2a803b を merge した後）: `make … ZYBO_BOOT_BIN=… -j16 zybo-sd-image-host-test sd-image` | `ZYBO SD image check: PASS build/p006-zybo/zybo-sd.img`、`… zybo-sd-image-test.img`、期待どおりの 2 つの FAIL、`zybo-sd-image-host-test: PASS`。BPB: 512 byte の cluster、reserved 32、table 1009 sector、cluster 129022、root cluster 2、`FAT32   `。MBR p1 = 0x80・type 0x0C・LBA 2048・131072。`mdir` に `vmunix.itb`・`system.dtb`・`BOOT.BIN`。`cksum build/p006-zybo/system.dtb` = `480521733 413` |
| `deploy.sh … --host 127.0.0.1 --port 1 --wait 12` | 0・10・20 s に 3 回試して `deploy: cannot reach root@127.0.0.1:1`、rc 1 |
| `dtc -I dtb -O dts` で FIT と DTB を戻す | FIT: `load = <0x100000>`、`entry = <0x100000>`、`os = "linux"`、`arch = "arm"`、`type = "kernel"`、`compression = "none"`、`configurations { default = "zybo"; zybo { description; kernel = "kernel"; }; }`。DTB（`--bootargs 'mac0=02:5a:59:42:4f:31 x"y'` の試し）: `bootargs = "rootpart=/dev/mmcblk0p2 mac0=02:5a:59:42:4f:31 x\"y"`（引用符の escape） |
| `plan/tools/zybo/deploy.sh build/p006-zybo/vmunix --host 127.0.0.1 --port 1` | FIT を作り、`deploy: cannot reach root@127.0.0.1:1` で rc 1。引数無しで usage・rc 2。`sh -n`（deploy.sh・spl-build.sh・qemu-zynq.sh）OK。shellcheck は host に無い |
| `qemu-zynq.sh … --dtb build/p006-zybo/system.dtb --sd … --net 2289 --print` | `-device loader,file=build/p006-zybo/system.dtb,addr=0x01f00000` が入る（QEMU は起動していない）。`--dtb` と `--append` の併用は rc 2 |
| `make -j16 ZEDBSD_CONFIG=config/ci/config-armv7-virt.mk BUILD=build/p006-armv7-virt vmunix` | rc 0、warning 0 |
| `make -j16 -o build/p006-rpi4/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p006-rpi4 vmunix` | rc 0、warning 0、`arm64 vmunix check: PASS` |
| `make -j16 -o build/p006-amd64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p006-amd64 vmunix` | rc 0、warning 0、`amd64 vmunix check: PASS` |
| `git diff --check` | clean |

### 実機 ZYBO（JTAG、JP5 = JTAG のまま、2026-10-23 16:0x〜16:1x P3、board の lock、実機の証拠）

新しい SPL（`build/p006-spl/out/spl/u-boot-spl.bin`、sha256 `e6ca79da…`、BSS・stack・pool を `0x1F000000` 以上に移した物）で JTAG の道が生きている事と、既定の config の root（network・SSH 入り）:

- `ZYBO_SPL=… jtag-boot.sh build/p006-zybo/vmunix.bin --initrd build/p006-zybo/root.img …`: `U-Boot SPL 2025.07 (Jul 07 2025 - 19:58:33 +0000)`、`zybo: SPL halted at 0x00000fd8`（breakpoint は効く）、kernel 1032800 byte・DTB・initrd 17825792 byte（69.4 s）の load と read back、`zybo: entered 0x00100000, DTB 0x01f00000`、`ARMV7 FDT PASS phys=01f00000`、`ARMV7 MEMORY base=100000 total=511 MiB free=487 MiB`、`gem: PHY 0 id …`、`init: started sshd`、`login:`、login、`ls /bin/mv /sbin/networkd /usr/sbin/sshd` rc 0、`gem: en0 link up 1000 Mbps full duplex`、`dhcpc: en0: bound 10.0.30.2/16`、host から `plan/ws289/tests/ssh-check.sh 10.0.30.2 22 build/tmp/p006-board` → `uname -m = armv7`、cksum `499250553 8388608` が一致、scp 上り 5 s・下り 10 s、`ssh-check: PASS`（最後の step まで serial で見た。jtag-boot.sh の終了 code は写していない）。
- card は差したまま（ramdisk が有るので root は ramdisk、p004 の H1）。

### 未実施

- **実機の SD からの起動の全部**（BootROM が FAT32 の BOOT.BIN を読む事（R7）、SPL の MMC の初期化、Falcon の load と jump、SD の root、`deploy.sh` と `reboot` 後の boot mode（R8）、R5 の memreserve の影響）: card が初期化できないため（U41）。JTAG で SPL に card から FIT を読ませる試しも、同じ card の問題で意味が無いので行っていない。SPL の Falcon の道は source を読んで確かめただけ（上）。
- QEMU（T1）: 下の依頼。

## T1 の 1 回目（2026-10-23、283f1a191 = main 3caf97354）: A が FAIL

- Q1 の報告: login までは PASS（`vfs: boot0 <loader-origin> -> /dev/mmcblk0p1 (private FAT)`、`rootpart selector /dev/mmcblk0p2 resolved`、`vfs: boot0 (BOOT) published at /boot/boot0`、en0 1000 Mbps、DHCP 10.0.2.15、sshd の起動）。`ls /boot/boot0; echo boot0-rc=$?` で `ls` が何も出さず `boot0-rc=0` だけ → `vmunix.itb` が見えず、ssh-check と deploy は走らなかった。QEMU の後の `mdir` では FAT に `vmunix.itb` 1033303・`system.dtb` 413 が有る。25 分（timeout まで待った）。
- 解析（担当、source を読んで。QEMU と板は使っていない）: zedBSD の FAT の driver（`src/drivers/fs/fat.c`）は FAT16 では長い名前を扱わない（`fat_raw_readdir` と名前の探しの両方で「Only FAT32 volumes carry long names at all」、FAT16 は 8.3 の短い名前だけ）。`deploy.sh` の `vmunix.itb.new`・`.old` は 8.3 に収まらないので、FAT16 の card では `ls` が見えても deploy は失敗する。他の板の boot の FAT（Pi 4/5 の SD、VF2 の ESP）は全部 FAT32 で、zedBSD の FAT の driver が実際に使われている道も FAT32。FAT16 の root（固定の root directory）の readdir が空を返した直接の原因は source の上では特定できていない（BPB からの root の位置・cluster の数（65264 で FAT16）・volume label の飛ばし・短い名前の復号は読んだ限り正しい）。**未確認**: FAT32 にして `ls` が見えるかは下の A1 で確かめる。
- 直し（design L13 の FAT16 を取り消す）: p1 を **FAT32**（type `0x0C`、`mformat -F`、64 MiB で 512 byte の cluster 129022 個 = FAT32 の下限 65525 を超え、kernel・U-Boot・BootROM の誰が数えても FAT32）。`make-zybo-sd-image.py` は `--fat-mib` の下限を 64 に、checker は `FAT32   `・固定の root が無い・16 bit の table の大きさ 0・cluster の数 ≥ 65525 を見る。BootROM が FAT32 を読む事は UG585 で未確認（Xilinx の SD 起動の手順は FAT32 の card が普通、R7 は実機で）。`deploy.sh --wait S`（sshd が host key を作り終えるまで再試行。QEMU で数分、実機の初回の起動でも）。

## T1 への依頼（2 回目、2026-10-23、Q1 経由。QEMU、BOOT.BIN 無し）

tree: branch `agent/p3-ws289-p006` の最新（Q1 が merge した後なら main）。build（worktree に main の `build/llvm`・`llvm-source`・`llvm-build`・`NoctLang` への symlink、`build/distfiles` に `openssh-10.5p1.tar.gz`・`openssl-3.5.8.tar.gz`・`zlib-1.3.2.tar.xz`・`cacert-2026-08-13.pem`、host に `sshpass`・mtools）:
`make ZEDBSD_CONFIG=config/ci/config-zybo.mk BUILD=build/p006-zybo -j16 vmunix sd-image`（`ZYBO SD image check: PASS` を見る）
`cp build/p006-zybo/zybo-sd.img build/tmp/p006-sd.img`（QEMU が書くので写し。A1 と A2 は同じ写しを続けて使う）

短くするため network の無い A1（数分）を先に、A1 が PASS の時だけ A2。A1 が FAIL なら A2・B は流さずに返す。

- A1（zynq、card の `system.dtb` で SD の root、FAT32 の読み・長い名前の作成と rename、serial U3。network 無し）:
  `plan/tools/zybo/qemu-zynq.sh build/p006-zybo/vmunix --dtb build/p006-zybo/system.dtb --sd build/tmp/p006-sd.img --timeout 600 --expect 'rootpart selector /dev/mmcblk0p2 resolved to /dev/mmcblk0p2' --expect 'login: *$' --then type:root --then expect:Password: --then type:root --then 'expect:root@kei' --then 'type:ls -la /boot/boot0' --then 'expect:vmunix\.itb' --then 'type:cksum /boot/boot0/system.dtb' --then 'expect:^480521733 413' --then 'type:echo zybo > /boot/boot0/longname-test.new && mv /boot/boot0/longname-test.new /boot/boot0/longname-test.old && sync && test -f /boot/boot0/longname-test.old && echo lfn-ok' --then 'expect:^lfn-ok'`
  → exit 0 で PASS。`ls -la /boot/boot0` の出力の行と `vfs:` の行を写して返す（`cksum` の値は 283f1a191 以降の `system.dtb` の物。build が違う値を出したら `cksum build/p006-zybo/system.dtb` の値で読み替える）。
- A2（同じ写しで QEMU を起動し直し、LAN、SSH U4、deploy）:
  `plan/tools/zybo/qemu-zynq.sh build/p006-zybo/vmunix --dtb build/p006-zybo/system.dtb --sd build/tmp/p006-sd.img --net 2289 --timeout 1200 --expect 'gem: en0 link up' --expect 'login: *$' --then 'run:env SSHPASS=root plan/tools/zybo/deploy.sh build/p006-zybo/vmunix --host 127.0.0.1 --port 2289 --dtb build/p006-zybo/system.dtb --wait 900 --no-reboot'`
  → exit 0 で PASS。`deploy:` の行（`host|`）を写して返す。
- B（A2 の後、QEMU を終えてから host で FAT を読む: zedBSD の FAT の書き込みを外の読み手（mtools、SPL の FAT と同じく別の実装）が読める事）:
  `mdir -i build/tmp/p006-sd.img@@1048576 ::/` → `vmunix.itb`・`vmunix.itb.old`・`system.dtb`・`system.dtb.old`・`longname-test.old` が有り `.new` が無い。`mcopy -n -i build/tmp/p006-sd.img@@1048576 ::/vmunix.itb build/tmp/p006-vmunix.itb && cmp build/tmp/p006-vmunix.itb build/p006-zybo/vmunix.itb`、`system.dtb` も同じく `cmp` → 一致で PASS。
- 失敗の時: 最後に見えた serial の行と `host|` の行、B の `mdir` の出力を返す（解析は担当）。`build/tmp/p006-*` は Q1 が `q1-clean.sh` で消す。

## 実機の SD 起動の手順（card が使えるようになってから。Q1 がユーザーと T1 に渡す）

用意（Q1 か T1、host）:
1. `plan/tools/zybo/spl-build.sh`（`build/zybo-spl/out/spl/boot.bin` ができる。tree の外の GPL の binary、配布しない）。
2. `make ZEDBSD_CONFIG=config/ci/config-zybo.mk ZYBO_BOOT_BIN=$PWD/build/zybo-spl/out/spl/boot.bin -j16 sd-image` → `build/zybo/zybo-sd.img`（512 MiB、`ZYBO SD image check: PASS` を見る。FAT に BOOT.BIN・vmunix.itb・system.dtb）。

ユーザー（U1）:
3. ZYBO の電源を切る（USB の給電を抜くか電源の switch）。
4. 初期化できる microSD を host の card reader に差し、`lsblk -o NAME,SIZE,MODEL,TRAN,MOUNTPOINTS` で card の device（例 `/dev/sdX`。大きさが card と合い、host の disk でない物）を確かめ、mount されていれば `sudo umount /dev/sdX?*`。
5. `sudo dd if=build/zybo/zybo-sd.img of=/dev/sdX bs=4M conv=fsync status=progress` → `sync` → card を抜く。
6. card を ZYBO の microSD の slot に差す。boot mode の jumper **JP5 を JTAG から SD に**替える。USB-UART（serial）と LAN の cable は繋いだまま。
7. （T1 が serial の監視を始めてから）電源を入れる。

T1（実機、板の lock、生きた serial）:
8. 電源の前に監視を始める: `sg dialout -c "python3 -I plan/tools/zybo/uart-expect.py --device /dev/serial/by-id/usb-Digilent_Digilent_Adept_USB_Device_210279785182-if01-port0 --timeout 600 'U-Boot SPL 2025\.07' 'ARMV7 MEMORY' 'sdhci: mmcblk0 ready' 'rootpart selector /dev/mmcblk0p2 resolved' 'login: *\$'"`（lock は `flock /tmp/zedbsd-zybo-board.lock` の中で）。合格: SPL の banner、`ARMV7 MEMORY … total=511 MiB`（R5: memory node が壊れていない）、SD の root、`login:`。SPL の後に何も出なければ SPL の行（`spl: … error reading image …` など）を返す。何も出なければ（SPL の banner も無い）BootROM が BOOT.BIN を読めていない（R7: FAT32 の読みを疑う。BOOT.BIN が FAT の最初の file である必要は無いと見込むが未確認）。
9. login して `df /`（`/dev/mmcblk0p2`）、`ls /boot/boot0`（BOOT.BIN・system.dtb・vmunix.itb）。DHCP の address で `plan/ws289/tests/ssh-check.sh ADDR 22 build/tmp/p006-board-sd`。
10. host で `make ZEDBSD_CONFIG=config/ci/config-zybo.mk ZYBO_BOOTARGS=mac0=02:5a:59:42:4f:31 build/zybo/system.dtb` → `SSHPASS=root plan/tools/zybo/deploy.sh build/zybo/vmunix --host ADDR --dtb build/zybo/system.dtb`（kernel は別の commit の build なら `uname -v` の git hash も変わる）。合格: serial に再び SPL の banner と kernel、`zynq-ethernet: address from mac0=`・`gem: en0 ready MAC 02:5a:59:42:4f:31`（入れ替えた DTB を SPL が読んだ）、`login:`。`reboot` をもう 1 回して `login:`（R8: soft reset の後も SD の boot mode）。
11. 終わったらユーザーが JP5 を JTAG に戻し、T1 が `plan/tools/zybo/jtag-boot.sh --spl-only`（JTAG の道が生きている）。戻らない時の復旧も JP5 を JTAG にして `jtag-boot.sh`（U2）。card の `.old` の file で戻すなら JTAG の ramdisk の起動から `mv`。

## 残り・引き継ぎ（Q1 へ）

- T1 の 2 回目の A1・A2・B の結果と Q1 の判定・merge。
- 実機の SD 起動: card が初期化できるようになってから（U41）。上の手順の 3〜7 をユーザーに。
- Master の Tools: `spl-build.sh`（Falcon の fragment と `boot.bin`）、`deploy.sh`、`qemu-zynq.sh --dtb`、`make-zybo-boot-files.py`、`check-zybo-sd-image.py --kernel`、`ZYBO_BOOT_BIN`・`ZYBO_BOOTARGS` の行（担当は master.md を書かない）。master.md の ssh-check.sh の行の「image は `plan/ws289/tests/config-zybo-ssh.mk`」は `config/ci/config-zybo.mk` に。
- 削除の依頼（担当は rm しない）: `plan/ws289/tests/config-zybo-ssh.mk`（既定の config と同じ組になった。p005 の記録の中の参照は履歴として残す）、`/home/awe/zedBSD-worktrees/p3-ws289-p006/platform/armv7/tools/__pycache__/`（py_compile と checker の import が作った、ignore の対象）、`build/tmp/p006-board/`、`build/p006-spl/zybo-jtag.config`（直す前の script が残した）。
- docs の howto（ZYBO の SD 起動の手順）: 道具が `plan/` に有る間は書けない（上の差 9）。

- 2026-10-23 Q1: `plan/ws289/tests/config-zybo-ssh.mk` を git rm（既定の `config/ci/config-zybo.mk` と同じ組）、Master の Tools を直し `spl-build.sh`・`deploy.sh`・`make-zybo-boot-files.py`・`qemu-zynq.sh --dtb` を登録。p005 の phase.md の command の記録はその時の物として残す（今は `config/ci/config-zybo.mk` で同じ image）。

- 2026-10-23 Q1: ユーザーの許可（「書き込みを許可します」）で、新しい card に main 5a28d28ca の `zybo-sd.img`（FAT32、BOOT.BIN = spl-build.sh の boot.bin b743c8f8…）を dd して cmp で一致。次: ユーザーが card を ZYBO に差し JP5 を SD に、T1 が serial を監視して電源。
