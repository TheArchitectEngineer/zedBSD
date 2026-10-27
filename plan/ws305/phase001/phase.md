<!-- awesome-plan project=zedbsd record=ws305-p001 -->

# ws305-p001 passthrough の調査と設計

- 親: [WS305](../ws.md)。Queue q1619 / q1619-i01（P4）。
- 状態: in-progress（2026-10-27 11:00 P4 着手。host の事実の読み取りは Debian が上がるのを待つ）

## 範囲

IOMMU の有無と group、GPU と HDMI audio の分離、host の amdgpu から外す手順と戻す手順、RDNA1（Navi10）の reset の扱い、
QEMU の構成、guest の zedBSD の image、script の形、素の起動との使い分け、RTX に差し替えた時の差。設計を Q1 に送り、OK の後に p002。

## host の事実（10.0.10.3）

読むだけの調べ（lspci・iommu_groups・dmesg・cmdline・grub）は Debian が上がっている時に行う（2026-10-27 10:58 Q1: P2 が zedBSD を起動中、Debian は落ちている、再起動しない）。

- 未確認: CPU（AMD-Vi か VT-d）、IOMMU の有効の有無、GPU の slot と group、grub の構成、qemu・OVMF の導入の有無、RAM。

## 設計

### 1. host の三つの起動と使い分け

| 起動 | 選び方 | 用途 | GPU |
| --- | --- | --- | --- |
| Debian（既定、今のまま） | 既定の grub の項目 | Linux の基準の計測（amdgpu・RADV、P2）、build の補助 | amdgpu |
| Debian（VFIO） | `grub-reboot "Debian VFIO"` で一回だけ | VM の guest の zedBSD で driver の開発（WS305） | どの driver にも結ばない → VM を起こす時に vfio-pci |
| zedBSD（素） | UEFI の BootNext（P2 の measure.sh） | 速さの計測、cold の初期化、VM で再現しない不具合 | amdrdna |

- 既定の項目・`/etc/default/grub` の既定の行・BootOrder は変えない。VFIO の項目は `/etc/grub.d/40_custom` に足す（`update-grub` で grub.cfg に入る、既定は変わらない）。
  grub-reboot は grubenv の next_entry を使い、GRUB_DEFAULT=0 のままで効く（00_header の next_entry の処理）。戻りは普通の reboot（次は既定の項目）。
- 実行中の amdgpu からの unbind（既定の起動のまま渡す）は採らない: 起動の GPU（host の console・efifb）を amdgpu から外すと固まる例があり、
  VM の後に amdgpu へ戻すのは Navi10 の reset の問題でまず失敗する。host の Linux を壊さないことを優先して、起動の項目で分ける。
- VFIO の項目の引数（案、CPU が分かってから確定）: 既定の行 ＋ `iommu=pt`（Intel なら `intel_iommu=on` も）`modprobe.blacklist=amdgpu initcall_blacklist=sysfb_init`。
  - `modprobe.blacklist=amdgpu`: GPU は driver 無しのまま（host の console は無くなる、SSH で使う）。
  - `initcall_blacklist=sysfb_init`: efifb・simpledrm が GPU の BAR の framebuffer を握らない（vfio が BAR を予約できない問題を避ける）。
  - `vfio-pci.ids=` は入れない: HDMI audio に snd_hda_intel が付いても、vm-start が driver_override で vfio-pci に結び直す（RTX でも同じ手順）。

### 2. GPU と HDMI audio の分離

- RX 5600 XT（Navi10、`1002:731f`）と HDMI audio（`1002:ab38`）は同じ slot の function 0・1。vfio は IOMMU の group の全 device が vfio-pci か
  driver 無しであることを求める。group に PCIe の switch（Navi10 の板の上流・下流の port、`1002:1478`・`1002:1479`）が入るのは普通（bridge は pcieport のままでよい）。
- group に別の device（chipset の SATA・USB など）が混ざっていたら、slot を変える（ACS override の patch は使わない）。→ host の事実で確かめる。

### 3. reset（Navi10）と使い分け

Navi10 は FLR が無く、bus の reset（SBR）の後は SMU・PSP が半端な状態に残り、次の post に失敗することがある（いわゆる AMD の reset bug）。
guest の amdrdna は「前の driver が残した secure OS」を見て、`amdrdna.reset=1` なら起動の時に PSP の mode-1 reset と VBIOS の post を自分で行う
（`src/drivers/gpu/amdrdna/reset.c`、Linux の guest の amdgpu と同じ考え）。これを第一の手段にし、だめな時だけ下の段へ:

| 段 | 方法 | host | 時間 |
| --- | --- | --- | --- |
| R1 | guest の amdrdna の mode-1 reset（image の boot line に `amdrdna.reset=1`）。VM を quit・kill して起こし直すだけ | 止まらない | 秒 |
| R2 | vendor-reset（gnif、DKMS、BACO の入り切り）を host に入れ、`reset_method` を `device_specific` に。6.12 で build が通らない報告（#86）があるので要る時だけ、Q1 に案 | 止まらない | 秒 |
| R3 | host の warm reboot（`grub-reboot "Debian VFIO"` ＋ `reboot`、platform の reset で GPU も初期化） | 再起動 | 1〜2 分 |
| R4 | cold reset（Q1 の SwitchBot、AC276E42B252） | 電源 | 数分 |

- p002 で R1 を表に: 新しい host の起動の後の 1 回目、guest の reboot（QMP の system_reset）、quit → start、kill -9 → start（guest が固まった時の形）。
  R1 で戻らない形が出たら R2 の案を Q1 に送る。R3・R4 は「使い分け」（ユーザー）として script の `vm-reset --host`（R3）と Q1 への依頼（R4）。
- QEMU の reset（system_reset・quit）で vfio が SBR を打つことがある（function の reset が無い時の hot reset）。SBR の後でも R1 で戻るかを p002 で見る。
  戻らなければ `reset_method` を空にして（`echo > /sys/bus/pci/devices/<BDF>/reset_method`、host の reset を止める）R1 だけに任せる形も試す。

### 4. QEMU の構成（host 10.0.10.3 で動かす）

```
qemu-system-x86_64 -accel kvm -machine q35 -cpu host -smp 8 -m 8G \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=$RUN/vars.fd \
  -device pcie-root-port,id=rp1,chassis=1,slot=1 \
  -device vfio-pci,host=$GPU.0,bus=rp1,addr=00.0,multifunction=on[,romfile=$RUN/vbios.rom] \
  -device vfio-pci,host=$GPU.1,bus=rp1,addr=00.1 \
  -device qemu-xhci,id=xhci,p2=8,p3=8 \
  -drive if=none,id=boot,file=$RUN/disk.img,format=raw -device nvme,serial=zedbsd-boot,drive=boot,bootindex=1 \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:$SSHPORT-:22 -device usb-net,bus=xhci.0,netdev=net0,mac=52:54:00:33:05:01 \
  -device usb-kbd,bus=xhci.0 -vga none -display none -no-reboot? \
  -qmp unix:$RUN/qmp.sock,server,nowait -serial unix:$RUN/serial.sock,server,nowait \
  -gdb tcp:127.0.0.1:$GDBPORT -pidfile $RUN/qemu.pid -daemonize
```

- OVMF（Debian の ovmf）が GPU の option ROM の GOP を走らせて board を post する。amdrdna は firmware の post を前提にする（identify.c の posted）。
  ROM を QEMU が読めない・GOP が走らない時は、host で `rom` を読んで（vfio に結んだ状態で `echo 1 > rom; cat rom`）`romfile=` に渡す。
- amdrdna の VBIOS は ACPI の VFCT（OVMF には無い）か flash（SMU の ROM の index・data の register、MMIO）から読む（vbios.c）→ VM でも flash の道で読める。
- BAR: 既定の 64 bit の窓で 256 MiB の BAR0 は入る。host で resizable BAR が大きく開いていたら `-fw_cfg name=opt/ovmf/X-PciMmio64Mb,string=65536`。
- 画面: passthrough の GPU の出力は板の HDMI/DP の物理の monitor に出る（今のまま繋いでおく。monitor が無いと amdrdna の display は sink を見つけない）。
  QEMU の QMP の screendump は QEMU が描く display しか撮れず、`-vga none` では GPU の画面を撮れない。→ 判定は下の 6。
- `-no-reboot` は付けない（guest の reboot を R1 の試験に使う）。guest が固まった時は QMP の `quit`、答えなければ pid に SIGKILL。
- 解析: QMP（`query-status`・`system_reset`・`quit`・`human-monitor-command` の `info registers`・`xp`）と gdbstub（127.0.0.1 だけ）。
  QEMU の console・serial の log では判定しない（AGENTS.md §9）。

### 5. guest の image

- `plan/ws305/tests/config.mk`: WS291 の config（CI の amd64 ＋ amdrdna の組み込み・control・Navi10 の firmware・aat-input・keiland-shot・mview）と同じ中身に
  `ZEDBSD_BOOT_EXTRA_LINES := amdrdna.reset=1`。build は `plan/tools/guest/test-image.sh plan/ws305/tests/config.mk build/ws305/vm`（guest の harness の
  SSH の鍵と net.conf が入る）、`--file /etc/keiland/autologin=plan/ws291/tests/autologin`（kei の自動の login、desktop が出る）。
  過去の build の成果は入力にしない（AGENTS.md）。toolchain は main の checkout の `build/llvm` などへの symlink（P2 と同じ）。
- image（`hdd-image.img`）は host の `~/ws305/` へ scp し、VM は毎回その複写（`disk.img`）で起こす（guest.py と同じ）。kernel だけの差し替えは
  guest の SSH で `/boot/boot0/vmunix` を置き換え（measure.sh の deploy と同じ形）、または image を作り直す。

### 6. 判定（AGENTS.md の SSH/QMP の例外の形）

- SSH: host の 127.0.0.1 の転送 port へ、build の host から `ssh -J awe@10.0.10.3 -p $SSHPORT root@127.0.0.1`（ProxyJump、接続は 1 本を使い回す ControlMaster）。
  amdrdna の段（`dmesg` の `A0 ok`・`reset: mode1 done`・最後の段）、session の有無（`/run/user/1000/wayland-0`）を読む。
- 画面: QMP の screendump は GPU の画面を撮れない（上の 4）。代わりに guest の中の `keiland-shot`（test image の screen capture、WS291 の measure.sh shot と同じ）の
  PNG を SSH で取り出す。boot の途中（amdrdna の前）は画面の証拠が無いので、SSH と QMP の `query-status` で判断する。
  → この置き換えは AGENTS.md の例外（「SSH と QMP の screendump の PNG」）の形と違うので、Q1 の判断を求める（下の「Q1 への問い」）。

### 7. script（`plan/ws305/tests/vfio.sh`、build の host から 1 本で。host の半分は `vfio-host.py`、root で）

| 命令 | 動き |
| --- | --- |
| `vfio.sh mode` | host の起動が既定・VFIO・zedBSD・落ちているのどれか（/proc/cmdline の印、uname） |
| `vfio.sh enter` / `leave` | `grub-reboot "Debian VFIO"` ＋ reboot、/ 既定へ reboot。戻るまで待つ（上限 300 s） |
| `vfio.sh push IMAGE` | image を host の `~/ws305/hdd-image.img` へ |
| `vfio.sh start` | GPU の slot の全 function を vfio-pci に（driver_override → unbind → drivers_probe）、disk・vars の複写、QEMU を daemonize、SSH を待つ（上限 180 s） |
| `vfio.sh stop` | QMP の `quit`、10 s で終わらなければ SIGKILL。GPU は vfio-pci のまま |
| `vfio.sh reset [--host]` | QMP の `system_reset`（R1）。`--host` は R3（warm reboot で VFIO の項目へ） |
| `vfio.sh shot OUT` | guest の keiland-shot の PNG を OUT へ |
| `vfio.sh ssh CMD` | guest で CMD（root、ControlMaster の 1 本） |
| `vfio.sh qmp CMD` | QMP の命令（query-status など） |

- どの命令も時間の上限を持つ。rm を使わない（disk の複写は `cp` の上書き、一時物は host の `~/ws305/run/` に上書きで置く）。
- 機械の割り当て: 使う前に Q1 に送り OK を待つ（P2 と同じ機械）。lock の file は Q1 が決める（案: build の host の `/tmp/zedbsd-desktop.lock` を flock）。

### 8. RTX に差し替えた時（p003）

- 違い: device ID（`10de:...`）、function が多い（GPU・HDMI audio、Turing は USB-C の xHCI・UCSI も同じ slot）→ slot の全 function を vfio に（script は slot で扱う）。
  VFIO の項目の blacklist に `nouveau`（と nvidia があれば）。NVIDIA は bus の reset で戻るのが普通で、R1〜R2 の悩みは小さい見込み。
- guest の driver は nvrtx（WS037・WS242）。GSP の firmware を image に。VBIOS は ROM BAR から（必要なら romfile）。
- 同じ VFIO の項目・script で、GPU の BDF だけを見つけ直す（`lspci -d ::0300` の最初の discrete）。

## Q1 への問い と答え

1. host の変更: 「Debian VFIO」の grub の項目と `update-grub`、`qemu-system-x86`・`ovmf` の apt。→ 2026-10-27 11:10 Q1「可: /etc/grub.d/40_custom（か 4x の新しい file）に「Debian VFIO」を足して update-grub（既定は不変、変える前の file は残す）、apt で qemu-system-x86・ovmf。」
   → `/etc/grub.d/41_zedbsd_vfio`（生成 script、`plan/ws305/tests/41_zedbsd_vfio`）。追加の依頼（2026-10-27 11:20 Q1）: `/etc/default/grub` に `GRUB_RECORDFAIL_TIMEOUT=5`（SwitchBot の入れ直しの後に GRUB の menu で止まらない、変える前の file を残す）。
2. 画面の判定。→ Q1「可: passthrough の画面は SSH（guest の dmesg の amdrdna の段）と guest の keiland-shot の PNG で判定（WS291 の measure.sh shot と同じ形）。PNG は Q1 に。」
3. 機械の lock。→ Q1「可: build host の /tmp/zedbsd-desktop.lock を flock（P2 にも同じ lock を使うよう伝える）。」

## 記録

- 2026-10-27 11:00 P4: 着手。host は不達（P2 の zedBSD 起動中）。amdrdna の VBIOS の取り方（VFCT か flash）と boot の reset（amdrdna.reset=1）を source で確かめ、設計の草案。
- 2026-10-27 11:05 P4: 草案を Q1 へ（28ea66ed5）。11:10 Q1: 方向と問い 1〜3 を了解（上の答え）。
- 2026-10-27 11:30 P4: script と image の config を作成（9c15842ac）。41_zedbsd_vfio は build host で dry-run（grub-mkconfig の環境を手で渡し、menuentry の出力を確認、install はしていない）。
  image は build/ws305/vm/hdd-image.img（sysroot は Q1 の指示で P2 の build/amd64/sysroot を複写、zedBSD の source の warning 0）。
