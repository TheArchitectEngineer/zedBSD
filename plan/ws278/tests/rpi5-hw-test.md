# WS278 Raspberry Pi 5 の実機の試験の手順

QEMU に raspi5 は無いので、Pi 5 の実機で確かめる（ユーザーの手元の機材、Q1 経由で依頼）。

## 用意

- Raspberry Pi 5（RAM の量と、基板の stepping C1/D0 が分かれば記録）、電源（5V 5A の USB-C 推奨）。
- micro SD card（8 GB 以上、中身は消える）。
- micro HDMI → HDMI の cable と monitor。Pi 5 の HDMI0（USB-C の電源の側の port）につなぐ。
- 任意: Raspberry Pi Debug Probe（3 pin の「UART」connector、115200 8N1）。serial の build（下）の時だけ使う。
- USB keyboard（p004）、USB memory（任意、p004）、LAN の cable と DHCP の router（p005）、WiFi の AP の SSID と鍵（p006）、近くの Bluetooth の device（p007、scan で見える物なら何でも）。

## image

- build: `make ZEDBSD_CONFIG=config/ci/config-rpi5.mk -j16`（出来る物は `build/rpi5/hdd-image.img`）。
- serial の console も出す build（解析用）: `make ZEDBSD_CONFIG=config/ci/config-rpi5.mk BUILD=build/rpi5-serial CONFIG_RPI5_SERIAL_CONSOLE=y -j16`。
- SD に書く: `sudo dd if=build/rpi5/hdd-image.img of=/dev/sdX bs=4M conv=fsync status=progress`（/dev/sdX は SD card の device。間違えると host の disk が消えるので `lsblk` で確かめる）。

## 手順と期待（p002）

1. SD を Pi 5 に入れ、monitor をつないでから電源を入れる。
2. 期待:
   - 緑の ACT LED（HAL が段階を点滅の回数で示す）: 1 回＝kernel に入った、2 回＝device tree を読めた、3 回＝framebuffer ができた（6 回なら framebuffer が無く UART だけ）、4 回＝memory・表・割り込みの用意が済み kernel に入る、その後は点灯のまま。1 回も点滅しなければ kernel に入っていない（design.md の R1、番地か dtb の選択）。
   - HDMI: bootloader の画面の後、3 秒の赤・緑・青の試験の模様、その後に黒地の文字の log（`RPI5 ENTRY`、`RPI5 FDT PASS phys=…`、`RPI5 FRAMEBUFFER …`、`ARM64 MEMORY MAP PASS`、`ARM64 PAGING PASS`、kernel の log、`sdhci: mmcblk0 ready`）、最後に `login:` の prompt。
3. 撮る: 画面の写真（login の prompt、または止まった所の最後の数行）と、LED の点滅の回数。
4. serial の build なら Debug Probe の log（115200）も保存する。

## 止まった時に知りたい事

- LED の点滅の回数と、点灯・消灯のどちらで止まったか。
- 画面: bootloader の画面のまま（kernel に入っていない、R1 の番地か dtb の選択）か、試験の模様まで出たか、log の最後の行。
- `sdhci: init error=…` や `CMD8`・`ACMD41` の行があれば SD の初期化（design.md の R3）。

## 一回の試験の手順（p002〜p007 をまとめて、2026-10-21 版）

1. 上の image を SD に書く（serial の build も作っておくと、止まった時に 2 回目で使える）。
2. Pi 5 に SD、HDMI0 の monitor、USB keyboard（どの口でも）、LAN の cable を挿してから電源を入れる。
3. 起動の間、画面の log を撮る（流れて消えるので、止まったら最後の画面。login まで行ったら `dmesg > /tmp/d.txt` で後から読める）。
4. login（USB keyboard で。p004 が動かなければ keyboard が効かない → その時点の画面を撮って終わり）。
5. `dmesg | grep -E 'ARM64 SMP|rp1|xhci|gem|rpi5-wifi|bcm43455|rpi5-bt'` を撮る（下の表の期待と照らす）。
6. `top`（CPU が 4 つ）、`df`・USB memory を挿して `dmesg | tail`。
7. `ifconfig en0`、`ping -c 3 <router>`（有線）。
8. `wifi scan`、`wifi join <SSID>`（鍵を入れる）、`ifconfig wlan0`、`ping -c 3 <router>`（無線。有線の cable を抜いて）。
9. `bt` で scan（Bluetooth）。
10. 余裕があれば `cmdline.txt` に `cpus=1` を足した起動も 1 回（R6 の切り分け）。

## 一括の試験で見る項目（p003 以降、実装の順に足す）

| Phase | 見る物 | 期待 |
| --- | --- | --- |
| p003 SMP | 起動の log の `ARM64 SMP` の行、login 後の `top` | `ARM64 SMP 4 CPUs (device tree 4, limit 4)`、`ARM64 SMP cpu 1 online (core 1)`〜`cpu 3 … (core 3)`、`top` の CPU が 4 つ。止まったら最後の行の写真。`cpus=1` を cmdline.txt に足した起動でも login できるか（R6 の切り分け） |
| p004 USB | 起動の log の `rp1:`・`rp1-usb:`・`xhci:` の行、USB keyboard で login、USB memory を挿して `dmesg` | `rp1: bus 1, peripherals at PCI 0 (410000 bytes), DMA offset 1000000000`、`rp1-usb: DWC3 … in host mode` が 2 行、`xhci: PCI controller, version=… ports=…` が 2 行。USB keyboard（どの口でも）で login できる。USB memory の disk の行。`rp1: PCIe link down … pciex4_reset=0` が出たら config.txt の問題。`rp1: inbound window N was …` の行があれば写真（firmware の値） |
| p004b 割り込み | 起動の log の `rp1: interrupts …`・`rp1: vector …`・`rp1-usb: polling …` の行、keyboard の打鍵の反応 | `rp1: interrupts through the MIP at fffffff000, lines 128-191, 61 vectors`、`rp1: vector 31 (edge) on line 160`、`rp1: vector 36 (edge) on line 161`、`rp1-usb: polling 2 controllers every 50 ms`。打鍵が遅れずに出る。`every 2 ms` なら割り込みが付いていない（その前の `rp1-usb: controller N interrupt not routed` の行を撮る） |
| p005 Ethernet | 起動の log の `gem:` の行、LAN の cable を挿して `ifconfig`・`ping` | `gem: revision …, queues …, MDIO divider 96`、`gem: PHY 1 id 600d84a…`、`gem: en0 ready MAC …`（基板の MAC）、cable で `gem: en0 link up 1000 Mbps full duplex`。DHCP の address が付き、`ping -c 3 <router>` が返る。`gem: Ethernet clock was off …`・`gem: no 64-bit DMA reported`・`gem: initialization failed at …` の行があれば写真 |
| p006 WiFi | 起動の log の `rpi5-wifi:`・`bcm43455:` の行、`ifconfig`、`wifi scan`・接続 | `rpi5-wifi: SDIO host at 1001100000, line 306, 200000000 Hz; WL_ON gio 28`、`rpi5-wifi: SDIO and WL_ON pins routed`、`rpi5-wifi: SDIO card: 3 functions …`、`rpi5-wifi: F1 enabled …`、firmware の版の行、`wlan0` が `ifconfig` に出る。scan で AP が見え、接続して address、`ping`。止まったら `rpi5-wifi:` の最後の行の写真 |
| p007 Bluetooth | 起動の log の `rpi5-bt:` の行、login 後に `bt` で scan | `rpi5-bt: UART at 107d50c000, irq 308, clock 96000000 Hz, speed up to 3000000 baud`、`rpi5-bt: UART and BT_ON pins routed`、`rpi5-bt: the line at 115200 baud: …`、`rpi5-bt: controller ready after about … ms`、`rpi5-bt: in step …`、bluetoothd が patch を当てた後に 3 Mbaud（`rpi5-bt: line at 3000000 baud`）。`bt` の scan で近くの device が出る。`CTS did not rise` や `too many resets` が出たら写真 |

## 2 回目の試験（2026-10-22、image `zedbsd-rpi5-493271454.img`）

1 回目（2026-10-22 ユーザー「Pi5のUATです。カーネルは起動しますが、USB, LAN, SDIOなどいろいろ使えないようです。」、写真 `build/review/rpi5/uat-20261022-1.webp`）を受けた版。

- 全部の log: 起動の後 1〜2 分おいてから電源を切り、SD card を PC に挿して、FAT（最初の区画）の `log/boot-NNNNNN.txt`（番号の最も大きい物）を持ち帰る。bootlog の service が kernel の log をそこに書く（Pi 5 でも boot の FAT を `/boot/boot0` に出すようにした）。画面の写真は従来どおり最後の画面だけでよい。
- `ARM64 SError …` の行（画面だけに出る。log の file には入らない）: 出たら写真。`in the kernel at pc …` の番地は `build/review/rpi5/zedbsd-rpi5-493271454-kernel.elf` で引く。1 回目の init の異常終了（signal 10、vector 47）はこれで、今は process を止めずに報告だけする。
- Wi-Fi: `rpi5-wifi: controller configuration … now …`（card の検出を「有り」に固定し SD の pin を選んだ）、`rpi5-wifi: host started: present …`・`host before CMD5: present …`（bit 16 が 1 なら card 有り）、`rpi5-wifi: WL_ON level 1, output` の後に `rpi5-wifi: SDIO card: …` が出るか。
- USB・LAN: `rp1: PCIe link up (status …), bus 1 device 0 is 00011de4`、`rp1: bus 1, MSI-X table at PCI …`、`rp1-usb: DWC3 …`、`xhci: …`、`gem: …` のどこで止まるか。`rp1: PCIe link down` なら config.txt の `pciex4_reset=0` が効いていない。
- Bluetooth は driver を外した（2026-10-22 ユーザー「BTはPi4でも動作していないため、いったんドライバを無効にしてください。」）。`rpi5-bt:` の行は出ない。

## 3 回目の試験（2026-10-22、2 回目への直し）

2 回目（ユーザー「まだだめですね」、log `build/review/rpi5/uat-20261022-2-boot-000001.txt`、写真 `build/review/rpi5/uat-20261022-2.webp`）を受けた版。手順は 2 回目と同じ（起動の後 1〜2 分おいて電源を切り、FAT の `log/boot-NNNNNN.txt` の最も大きい番号の物と、最後の画面の写真）。

- USB・LAN: firmware は link を残すが root port の bus 番号を 0 のまま渡していた。今は kernel が bus 1 を付ける。見る行: `rp1: root port: …`・`rp1: outbound window: …`（firmware の値）、`rp1: the root port had no bus behind it; numbered bus 1 …`、`rp1: PCIe link up (status …), bus 1 device 0 is 00011de4`（RP1 なら 00011de4）、`rp1: bus 1, MSI-X table at PCI …`、`rp1-usb: DWC3 …`、`xhci: …`、`gem: …`。`device 0 is ffffffff` なら bus 1 の設定空間に届いていない。`rp1: BAR N was unplaced …` や `rp1: outbound window was unset …` が出たら firmware がそこも残していない（kernel が設定した）。USB keyboard で login できるか、LAN の cable で `ifconfig`・`ping`。
- Wi-Fi: 原因は未特定で、今回は絞るための log。`rpi5-pinctrl: brcm,bcm2712d0-pinctrl before: …`・`after: …`（pin の mux と pull の register）、`rpi5-wifi: GPIO bank levels …, inputs …`（bit 30〜35 = SDIO の CLK・CMD・DAT0〜3 の pad の level）、`rpi5-wifi: command line low; present by pin selection 0-3: …`、`rpi4-sdio: command failed, interrupt status …`。
- SError: `rpi5: SError pending after <段>` の行が原因の段を名指す（その直後に `ARM64 SError N … in the kernel at pc …` も出る。pc は `build/review/rpi5/zedbsd-rpi5-<sha>-kernel.elf` で引ける）。名指しが無く `on return to user` だけなら、計った段の外（SD の読み書きなど）。

## 4 回目の試験（2026-10-22、image `zedbsd-rpi5-29ad4a404.img`）

3 回目（log `build/review/rpi5/uat-20261022-3-boot-000001.txt`、写真 `build/review/rpi5/uat-20261022-3.webp`）を受けた版。image は `build/review/rpi5/zedbsd-rpi5-29ad4a404.img`（sha256 3c3e469b1130bc387fe6a20450a593217c4eea6dd087ca8c45c22f808e58e076）、kernel の elf は同じ名前の `-kernel.elf`。手順は 2 回目と同じ（起動の後 1〜2 分おいて電源を切り、FAT の `log/boot-NNNNNN.txt` の最も大きい番号の物と、最後の画面の写真）。USB keyboard と LAN の cable を挿して起動する。

- USB・LAN（3 回目は RP1 の周辺の読みが全部 `deaddead`。firmware の outbound の窓が device tree と違う CPU 番地だった）: 期待の行は `rp1: outbound window 0 was PCI 80000000 MiB 3ff00000 high 0000001c; now PCI 0 at CPU 1f00000000 (fffffffc bytes)`、`rp1: outbound window 1 was … now PCI 400000000 at CPU 1c00000000 …`、`rp1: BAR 1 was at PCI 80000000; now at PCI 0 …`、`rp1: BAR 0 was at PCI 80410000; now at PCI 410000 …`、`rp1: bus 1, MSI-X table at PCI 410000, peripherals at PCI 0 …`、`rp1-usb: DWC3 <deaddead でない値> at 1f00200000 in host mode`（2 行）、`xhci: … caps len=… version=…`（deaddead でない）、`gem: revision …`。USB keyboard で login できるか、LAN で `ifconfig`・`ping`。まだ `deaddead` なら、その周りの `rp1:` の行を全部。
- Wi-Fi（3 回目は C1 の device tree が渡されたが chip は D0 で、pin を C0 の配置で書いていた）: 期待の行は `rpi5-pinctrl: brcm,bcm2712d0-pinctrl registers (the tree names brcm,bcm2712c0-pinctrl, register 0 is 00006677)`、`rpi5-pinctrl: brcm,bcm2712d0-pinctrl after: 00006677 00000011 11000000 00001111 15555699 28555655 0aaaa5aa 00000000 …`（GPIO 30〜35 が sd2、CMD・DAT が pull-up）、`rpi5: SError pending after rpi5-wifi: routing the pins` が出ない、`command line low` が出ない、`rpi5-wifi: SDIO card: …`・bcm43455 の版の行・`wlan0`。出れば `wifi scan`・接続・`ping`。
- SError: `rpi5: SError pending after …` と `ARM64 SError …` の行が出たら写真（段の名で原因が分かる）。

## 5 回目の試験（2026-10-22、image `zedbsd-rpi5-9d78dc950.img`）

4 回目（写真 `build/review/rpi5/uat-20261022-4-stall.webp`）は起動し、USB と LAN が動き（`kei@10.0.30.5` に ssh できた）、uptime 約 2380 s で cpu0 が止まった（stall watch: `spins with interrupts off`、`spin_lock_irqsave` ← `gem_transmit` ← `net_device_transmit`）。原因: GEM の link の確認（`gem_poll` → `update_link` → PHY の MDIO の読み）と `gem_close` の送信の停止待ちが、device の spinlock を割り込み禁止で持ったまま `kern_usleep_range` で CPU を譲っていた。譲った先の同じ CPU の thread が `gem_transmit` で同じ lock を待って割り込み禁止のまま回り続け、lock を持つ thread は二度と走らない。250 ms ごとの link の確認のたびに窓が開くので、RA の受信への応答などの送信が当たると止まる（RA が多いほど起きやすいが、RA の受信の path 自体は lock の外で stack に渡しており原因ではない）。直し: MDIO と送信停止の待ちを、Pi 4 の GENET と同じく単調 counter の上の busy-wait（譲らない、MDIO は 2 ms・送信停止は 10 ms の上限）にした。他の RP1 の driver（USB・GPIO・IRQ・pinctrl・Wi-Fi・BT）は lock を持ったまま眠る所が無いことを確かめた。

- image `build/review/rpi5/zedbsd-rpi5-9d78dc950.img`（sha256 381a2a05f8faa016b464f2e6e4233332d99e54d188af8dbed041f1a74195a8b8）、kernel の elf `-kernel.elf`（6a6555ad0896c48a96d8349372c6a04022de6ab0c911f8bedc950d38a23782f6）。
- kernel だけの差し替え: `build/review/rpi5/vmunix-9d78dc950`（sha256 c0679d339d96a694b98303558b029a5c07b37bab2569177fbd18031163a8092c、image の FAT の `vmunix` と同じ）。board では FAT（最初の区画）の根の `vmunix`（`config.txt` の `kernel=vmunix`）で、動いている system では `/boot/boot0/vmunix`。手順: `scp vmunix-9d78dc950 kei@10.0.30.5:` → board で root になり `cp /boot/boot0/vmunix /boot/boot0/vmunix.old && cp ~kei/vmunix-9d78dc950 /boot/boot0/vmunix && sync && cmp ~kei/vmunix-9d78dc950 /boot/boot0/vmunix && reboot`（FAT の空きは約 24 MB、vmunix は 7.2 MiB）。戻すときは SD card を PC に挿して `vmunix.old` を `vmunix` に。
- 見る所: LAN を挿したまま 1 時間以上（4 回目は約 40 分で止まった）、stall watch の赤い画面が出ないこと、ssh が続くこと。出たら写真（番地は上の elf で引く）。
