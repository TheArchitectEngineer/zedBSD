<!-- awesome-plan project=zedbsd record=ws232-p004 -->
# ws232-p004: Drivers に Bluetooth、board の driver は Platform だけ、Pi の WiFi・Bluetooth が使えない件の config・DTB の確かめ

Status: in-progress（2026-10-14 P1。menuconfig の実装と host 試験 PASS、rpi4 の image の build は warning 0 で PASS。Pi の実機は T1 待ち、VCHIQ の既定は Q1 の判断待ち）
Disposition: normal
Parent: [WS232](../ws.md)
依存: [p003](../phase003/phase.md)

## なぜ WS232 か

menuconfig の Drivers の分類は WS232 p003（PC-98 の driver を Platform へ）で `config/drivers/menu.list` と `driver_groups` を作った所で、
今回の指示はその規則の作り直しになる。Pi の WiFi・Bluetooth の driver の実装は WS206・WS215（どちらも実機の確かめ待ち）だが、
この Phase で扱うのは config・config.txt・DTB・menu の側の確かめだけで、driver の source は変えていない。

## 原文（2026-10-14 ユーザー、Q1 経由）

1. 「make menuconfigのDriversのWiFiの1つ次の項目としてBluetoothを追加し、IntelとPiのBluetoothドライバを選べるようにしてください。
   また、PiのイメージでWiFiとBluetoothが使えていないようなので、configやDTBの確認をお願いします。」
2. （変更）「WiFiとBluetoothのドライバですが、プラットフォームによって選べるドライバが変わらない実装にしてください。Platformドライバだけ変えます。
   また、PiのドライバはPlatformにまとめていいです。LANもWiFiもBluetoothもオーディオも。変更してすみません。」
3. （追加）「PiのGPUとPlatformドライバにSDも入れましょう。Piのプラットフォームドライバはコンフィグですべて有効にしておいてください。」

## 設計（実装済み）

- `config/drivers/menu.list` を `category|keys` の 2 列にした。並びは Disk・Input・GPU・Audio・Ethernet・WiFi・**Bluetooth**・USB・Platform。
  Platform 以外の分類は bus の driver（`config/drivers/{isa,pci,usb}.drivers`）だけで、どの CPU でも同じ一覧。
- Platform は `config/drivers/architecture/*.drivers` の行のうち選んだ platform を名指す物（常に build する fixed の行も）を、その file の順に並べる。
  board の driver は Platform にだけ出て、その board の時にだけ出る。menu.list に architecture の driver を書くと、bus の driver を
  どこにも書かないと、menuconfig は止まる（書き落とし・二重の検出）。
- Bluetooth の行: `CONFIG_DRIVER_USB_BT`（表示を「USB Bluetooth: Intel AX211 and other USB HCI controllers (/dev/btN)」に。Intel の AX211 の BT は
  USB の HCI で、この driver と bluetoothd の Intel の firmware の読み込み（`userland/base/bluetoothd/intel.c`）で動く。Intel 専用の config は無い）。
  Pi の内蔵の BT（`CONFIG_DRIVER_BCM2711_BT`、表示を「BCM43455 Bluetooth: …」に）は Platform。
- Raspberry Pi 4 の Platform: SDHCI（SD、固定）・PL011 と platform の device（固定）・PCIe（固定）・GENET（LAN）・BCM43455 SDIO（WiFi）・
  BCM43455 Bluetooth・PWM audio・GPU（HVS と V3D）・VCHIQ・H.264 の Vulkan Video。`arm64.drivers` の行をこの順に並べ替えた。
- GPU の行の V3D（`CONFIG_DRIVER_BCM2711_GPU`）は Platform に移した。理由: Pi でしか build されない board の driver で、ユーザーの 3 の指示も同じ。
- 同じ規則で、amd64 の Framebuffer（`CONFIG_DRIVER_GRAPHICS`）・ACPI・USB-C（`CONFIG_DRIVER_TYPEC`）、i386 の Cirrus GD5446、PC-98 の Cirrus Core Graph・LGY-98 も
  Platform に出る（どれも architecture の catalog の driver で、board ごとに表示も build の有無も違う）。GPU・USB・Ethernet の行からは外れた。
- CPU / Board で platform を変えた時、新しい platform の Platform の driver（bool）は catalog の既定に戻す（`choose_platform`）。
  **前は x86_64 の既定から arm64 を選ぶと Pi の GENET・WiFi・BT・audio・GPU が全部 n のまま書き出された**（下の「原因」の 1）。
- `usb.drivers` の USB の class の driver と `pci.drivers` の xHCI の platforms に rpi4 を足した（arm64 の vmunix.mk はこれらを build する。
  前は rpi4 で catalog の既定が n になり、config に無い時に menu が n を出していた）。
- `config/ci/config-rpi4.mk` に Pi の Platform の driver を明示した: GENET・WIFI・BT・AUDIO・GPU は y。VCHIQ・VIDEO は n（下の「判断が要る点」）。
  前の file はこれらの行が無く、Makefile の既定（rpi4 で y）で build されていたので、build の結果は変わらない。
- Firmware の package は driver に付いて選ばれる（2026-10-14 の追加の指示、下の「driver の firmware の自動の選択」）。前はこの Phase でも
  Firmware の menu の別の選択で、driver の選択とは結び付けていなかった。

## 判断が要る点（Q1 へ）

- `CONFIG_DRIVER_BCM2711_VCHIQ` を y にするか。ユーザーは「Pi の Platform の driver は全部有効に」だが、VCHIQ は WS236 p002 の開発中（実機の確かめ前、
  表示も experimental）で、Q1 の「開発中の物は y にしない」に当たると判断して n のままにした。VIDEO・VIDEO_PROBE も n のまま。
- menuconfig で Pi を選んだ時に bcm43455-firmware・brcmbt-firmware も自動で選ぶか → 2026-10-14 ユーザー「yes」。下の節で実装。

## Pi の WiFi・Bluetooth が使えない件の確かめ（source と build の成果物を読んだ結果）

1. **menuconfig の既定の誤り（直した）**: menuconfig は x86_64 の既定で始まり、`driver_defaults("amd64")` で Pi の driver を n にする。
   CPU を arm64 に変えても値を直さないので、menuconfig で作った Pi の config.mk は `CONFIG_DRIVER_BCM2711_{GENET,WIFI,BT,AUDIO,GPU} := n`
   （host で main の 797e54fa5 の menuconfig.py を使って再現）。さらに firmware の package は既定 n。menuconfig で作った image なら WiFi・BT は
   kernel に入らず firmware も無い。CI の `config/ci/config-rpi4.mk` は行が無かったので Makefile の既定の y で入る。
2. **config.txt（問題なし）**: `CONFIG_DRIVER_BCM2711_BT=y`・`CONFIG_RPI4_SERIAL_CONSOLE=n` の build では `make-rpi4-config.py` が
   `enable_uart=1`・`uart_2ndstage=1`・`dtoverlay=disable-bt` を外す。main の build/arm64/config.txt（2026-10-14 20:57、rpi4 の config）で外れていることを確かめた。
   PL011 は BT に、`init_uart_clock=48000000` は残る（rpi4-bt.c の divisor の前提）。
3. **DTB（問題なし）**: firmware の commit `3d301dd9…`（WS233 の前の submodule と同じ commit、同じ 5 file）の `bcm2711-rpi-4-b.dtb` を dtc で読んだ:
   `serial@7e201000`（`arm,pl011`、okay、`uart-has-rtscts`）の下に `bluetooth`（`brcm,bcm43438-bt`、okay、`max-speed` 3000000、`shutdown-gpios` は firmware の GPIO 0 = BT_ON）、
   `mmcnr@7e300000`（okay、`non-removable`）の下に `wifi@1`（`brcm,bcm4329-fmac`）。driver が探す compatible（rpi4-bt.c の `brcm,bcm43438-bt` で親が `arm,pl011`・両方 enabled）と一致。
   BT の build では overlay は使わない（`disable-bt.dtbo` は FAT にあるが config.txt が名指さない）。
4. **image の FAT と rootfs（問題なし）**: main の build/arm64/hdd-image.img の FAT に start4.elf・fixup4.dat・bcm2711-rpi-4-b.dtb・LICENCE.broadcom・
   overlays/disable-bt.dtbo・config.txt・cmdline.txt・vmunix。rootfs に `/lib/firmware/cypress/cyfmac43455-sdio-minimal.bin`・`.clm_blob`・
   `/lib/firmware/brcm/brcmfmac43455-sdio.txt`・`/lib/firmware/brcm/BCM4345C0.hcd`（driver・bluetoothd が開く path と一致）。kernel.elf に
   `drv_rpi4_bt_*`・`bcm43455_device_*` の symbol。
5. **WS233 の取り方の変更（落ちた物なし）**: `platform/arm64/firmware.mk` は submodule と同じ commit の同じ 5 file を取る。
6. **残る候補**: WS206（p002b〜p005・p008）と WS215（p002〜p005）の driver は実装・host 試験・build までで、Pi の実機では一度も確かめていない。
   config と DTB が正しい image で使えないなら、原因は driver の bring-up（SDIO の CMD5・firmware の download、PL011 の HUNT・patchram）の側にある。
   下の手順の dmesg の行でどこで止まるかが分かる。

## 確かめ

- `timeout 300 make menuconfig-host-test` → `MAC-T001 menuconfig round-trip: PASS (… the board drivers under Platform alone, Bluetooth after WiFi)`。
  `check_platform_drivers`（p003 の `check_pc98_platform_drivers` を置き換え）: 全 CPU で分類の並び（Bluetooth は WiFi の次）、Platform 以外が全 CPU で同一、
  Platform が architecture の catalog のその platform の行と一致、Pi の Platform に SD・LAN・WiFi・BT・audio・GPU、PC-98 の Platform の walk（p003 の内容）、
  x86_64 の既定から arm64 を選ぶと Pi の driver が y（VCHIQ・VIDEO は n）、`config/ci/config-rpi4.mk` の明示の行。
  （p003 の試験は WS236 が Platform に VCHIQ を足した後で既に FAIL していた。）
- rpi4 の image の build: `yes | make ZEDBSD_CONFIG=config/ci/config-rpi4.mk firmware-licenses` の後 `make -j32 ZEDBSD_CONFIG=config/ci/config-rpi4.mk disk-image`（worktree の build/arm64、commit 2fbb7ffcb）→ exit 0、`Raspberry Pi 4 image check: PASS`、UFS OK。warning は zedBSD の source（src/kern・drivers・hal・libc・rtld、userland、platform、tools）で 0。log の warning は外部の package（openssh・openssl・libavcodec の上流の source）と perl の locale だけ（前からある物）。image（sha256 e35e3176…c9bd6）: build/arm64/config.txt に `enable_uart`・`uart_2ndstage`・`disable-bt` が無い、FAT に DTB・config.txt・vmunix、rootfs に cypress・brcm の firmware の 4 file、kernel.elf に rpi4-bt・rpi4-wifi・bcm43455 の symbol（10 個）。
- 未実施: QEMU（raspi4b には WiFi・BT の chip が無く、driver は QEMU を見て止まる）、Pi の実機（T1）、curses の実画面。

## T1 への依頼（Pi 4 の実機、kei@10.0.30.2）

image: `config/ci/config-rpi4.mk` で build した `build/arm64/hdd-image.img`（この Phase の commit を含む main）。SD に書いて Pi 4 を起動する（書き込みと電源はユーザーか T1 の既存の手順）。
`ssh kei@10.0.30.2` で login して次を読む。判定は dmesg の行と command の出力（serial・console の log は使わない）。

1. `cat /proc/version` か `uname -a` で image の版を確かめる。
2. `dmesg | grep -E 'rpi4-wifi|bcm43455|wlan|sdio'`:
   - 合格の目安: `rpi4-wifi: EMMC clock … Hz`、card の function 2 個、firmware の download の後に wlan0 の公開の行。`ifconfig -a` に `wlan0`。
   - 不合格の行の例: `rpi4-wifi: no onboard Wi-Fi to bring up (N)`（DT の slot）、`package bcm43455-firmware not installed`、`WL_ON not driven …`、
     `card has N functions, not 2`、`onboard Wi-Fi not published (N)`。出た行を全部写して返す。
3. `wifi wlan0 up`、`wifi wlan0 search start`、数秒後 `wifi wlan0 list`（AP が並ぶか）、`wifi wlan0 status`。接続の試験は `.wifi` の認証情報で
   `wifi wlan0 connect SSID PASSPHRASE`（認証情報は記録に写さない）、`dhcpc wlan0` か networkd の自動、`ping -c 3 <gateway>`。
4. `dmesg | grep -E 'rpi4-bt|bluetooth|BT_ON|hci'`:
   - 合格の目安: `rpi4-bt: BT_ON was level N`、clock の行、controller の公開（`/dev/bluetooth0`）。
   - 不合格の行の例: `the PL011 is the serial console`（serial の console の build）、`no enabled Bluetooth node on the PL011`（disable-bt が効いている）、
     `BT_ON not readable`、`controller did not come up (N)`、`controller not published (N)`。
5. `ls -l /dev/bluetooth*`、`bt show`（controller・address・firmware の版）、`bt scan 10`、`bt devices`。bluetoothd の log（`/var/log` の bluetoothd の行）。
6. `ls -l /lib/firmware/brcm /lib/firmware/cypress`（firmware の package が載っているか）、FAT の `config.txt` に `disable-bt`・`enable_uart` が無いこと
   （`/boot` に FAT が mount されていれば `grep -E 'disable-bt|enable_uart' /boot/config.txt` が何も出さない）。

結果は dmesg の該当の行の全文と各 command の出力を返す。WiFi・BT が出ない時は、その行で WS206・WS215 のどの段で止まったかを Q1 が判定する。

## driver の firmware の自動の選択（2026-10-14、P1）

原文（ユーザー、Q1 経由）: 「menuconfig で Pi を選んだ時、WiFi と Bluetooth の firmware（bcm43455・brcmbt）も自動で選びますか？-->yes」
「Intel GPUのドライバも、選択したらファームウェアも選択されるようにお願いします。そのほかのドライバも、ファームウェアを選択してください。」

- 対応の表は 1 か所: `config/drivers/firmware.list`（`driver|package`）。i915 → i915-firmware、NVRTX → nvidia-gsp-firmware、AX211 → intelax211-firmware、
  RTL8822BU → rtl8822b-firmware、USB の BT → intelbt-firmware（bluetoothd が AX211 の BT に読み込む）、BCM2711 の WiFi → bcm43455-firmware、
  BCM2711 の BT → brcmbt-firmware。firmware の package（`userland/firmware/` の 7 つ）は全部どれかの driver に付く（source で firmware を読む所を
  grep して確かめた: i915・nvrtx・intel-ax211・rtl8822b・bcm43455、bluetoothd の intel と broadcom。他に firmware を読む driver は無い）。Pi の boot の
  firmware（rpi4-boot-firmware）は platform の物で user program ではないので表の外。
- `tools/menuconfig.py`: `driver_firmware(values, platform)` = その platform で build される y の driver が要る package の集まり。driver の切り替え
  （Drivers の menu）と CPU / Board の選択（`choose_platform`、新しい board の driver の既定）の前後で比べ、前に要って今は要らない package を外し、
  今要る package を足す。driver と関係なく Firmware の menu で選んだ物は残る。`defaults()` も同じ（x86_64 の既定は i915 と USB の BT が y なので
  i915-firmware・intelbt-firmware が選ばれる）。license の同意は今までどおり Build boot image の画面（と make の firmware-licenses）。
- `config/ci/`: 有効な driver の firmware が足りなかった 3 つに intelbt-firmware を足した（`CONFIG_DRIVER_USB_BT` は Makefile の既定で y）:
  config-intelmac.mk・config-pcat.mk・config-rpi4.mk。config-amd64.mk は足りていた。config-pc98.mk は driver が n の firmware
  （i915・intelax211・rtl8822b）を選んでいるが、足りない物は無いので触らない。config-amd64-server.mk はユーザーの物で触らない。
  pcat と intelmac は bluetoothd を選んでいないので、intelbt-firmware は今は使われない（driver の規則に合わせた）。
- `plan/tools/menuconfig-target-host-test.py` に `check_driver_firmware`: 表の driver と package が実在し、firmware の package が全部表にあること、
  x86_64 の既定の選択、GPU の NVIDIA・i915 の on・off で firmware が付いて外れること、手で選んだ rtl8822b-firmware が残ること、x86_64 から arm64
  を選ぶと bcm43455・brcmbt が付き i915 が外れること、Pi の BT を off で brcmbt が外れること、x86_64 に戻すと Pi の物が外れ i915 が戻ること、
  server 以外の CI の config が有効な driver の firmware を全部選んでいること。
- 既存の試験の直し: `check_platform_drivers` は config-rpi4.mk の VCHIQ・VIDEO を n と見ていたが、main の `2a8f5054a` で y になった
  （menuconfig で作り直された file）。ここは値を問わず行があることだけを見る形にした（x86_64 から選んだ時の既定 n は今までどおり確かめる）。
- 確かめ: `timeout 600 make menuconfig-host-test` → `MAC-T001 firmware licenses: PASS`、`MAC-T001 menuconfig round-trip: PASS (…, the drivers' firmware
  selected with them)`。変えた CI の 3 つは `make ZEDBSD_CONFIG=… validate-image-config` が rc 0。curses の実画面と image の build は未実施。
