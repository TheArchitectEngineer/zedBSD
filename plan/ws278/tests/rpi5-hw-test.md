# WS278 Raspberry Pi 5 の実機の試験の手順

QEMU に raspi5 は無いので、Pi 5 の実機で確かめる（ユーザーの手元の機材、Q1 経由で依頼）。

## 用意

- Raspberry Pi 5（RAM の量と、基板の stepping C1/D0 が分かれば記録）、電源（5V 5A の USB-C 推奨）。
- micro SD card（8 GB 以上、中身は消える）。
- micro HDMI → HDMI の cable と monitor。Pi 5 の HDMI0（USB-C の電源の側の port）につなぐ。
- 任意: Raspberry Pi Debug Probe（3 pin の「UART」connector、115200 8N1）。serial の build（下）の時だけ使う。
- 任意（p004 以降）: USB keyboard。

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

## 一括の試験で見る項目（p003 以降、実装の順に足す）

| Phase | 見る物 | 期待 |
| --- | --- | --- |
| p003 SMP | 起動の log の `ARM64 SMP` の行、login 後の `top` | `ARM64 SMP 4 CPUs (device tree 4, limit 4)`、`ARM64 SMP cpu 1 online (core 1)`〜`cpu 3 … (core 3)`、`top` の CPU が 4 つ。止まったら最後の行の写真。`cpus=1` を cmdline.txt に足した起動でも login できるか（R6 の切り分け） |
| p004 USB | 起動の log の `rp1:`・`rp1-usb:`・`xhci:` の行、USB keyboard で login、USB memory を挿して `dmesg` | `rp1: bus 1, peripherals at PCI 0 (410000 bytes), DMA offset 1000000000`、`rp1-usb: DWC3 … in host mode` が 2 行、`xhci: PCI controller, version=… ports=…` が 2 行、`rp1-usb: serving 2 controllers every 2 ms`。USB keyboard（どの口でも）で login できる。USB memory の disk の行。`rp1: PCIe link down … pciex4_reset=0` が出たら config.txt の問題。`rp1: inbound window N was …` の行があれば写真（firmware の値） |
| p004b 割り込み | 起動の log の `rp1: interrupts …`・`rp1: vector …`・`rp1-usb: polling …` の行、keyboard の打鍵の反応 | `rp1: interrupts through the MIP at fffffff000, lines 128-191, 61 vectors`、`rp1: vector 31 (edge) on line 160`、`rp1: vector 36 (edge) on line 161`、`rp1-usb: polling 2 controllers every 50 ms`。打鍵が遅れずに出る。`every 2 ms` なら割り込みが付いていない（その前の `rp1-usb: controller N interrupt not routed` の行を撮る） |
| p005 Ethernet | 起動の log の `gem:` の行、LAN の cable を挿して `ifconfig`・`ping` | `gem: revision …, queues …, MDIO divider 96`、`gem: PHY 1 id 600d84a…`、`gem: en0 ready MAC …`（基板の MAC）、cable で `gem: en0 link up 1000 Mbps full duplex`。DHCP の address が付き、`ping -c 3 <router>` が返る。`gem: Ethernet clock was off …`・`gem: no 64-bit DMA reported`・`gem: initialization failed at …` の行があれば写真 |
| p006 WiFi | 起動の log の `rpi5-wifi:`・`bcm43455:` の行、`ifconfig`、`wifi scan`・接続 | `rpi5-wifi: SDIO host at 1001100000, line 306, 200000000 Hz; WL_ON gio 28`、`rpi5-wifi: SDIO and WL_ON pins routed`、`rpi5-wifi: SDIO card: 3 functions …`、`rpi5-wifi: F1 enabled …`、firmware の版の行、`wlan0` が `ifconfig` に出る。scan で AP が見え、接続して address、`ping`。止まったら `rpi5-wifi:` の最後の行の写真 |
