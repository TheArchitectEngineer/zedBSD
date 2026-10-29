# WS313 の実機の試験（ODROID-C5）

夜の実機の試験の手順（2026-10-29、ユーザーの指示「今日の夜、まとめてテストします。」）。code は実機なしで書いたので、全てが未試験。
判定に UART の対話（`c5-serial.py` が生きた line を読む）を使ってよいかはユーザーに確かめ中（AGENTS.md §9 の例外は今は ZYBO・RG350 など）。
認められなければ、UART は解析だけに使い、判定は HDMI の画面の写真（ユーザー）と、LAN が出た後の SSH にする。

## 準備（ユーザー）

- USB-UART の adapter（3.3 V、921600 baud を出せる物。Hardkernel の USB-UART kit、CP2102・FT232 など）を C5 の UART の connector（TX・RX・GND）につなぎ、centris の USB へ。pin の配置は wiki.odroid.com が読めず未確認（基板の silk か Hardkernel の資料で）。
- 12 V/2 A の電源（5.5/2.1 mm）。SwitchBot の plug を使うなら、その ID。
- microSD（8 GB 以上）、HDMI の display、USB の keyboard・mouse、LAN の cable（DHCP のある網）。
- Hardkernel の ODROID-C5 の公式の image（bootloader の区画を写すため。`ODROID_C5_BOOTLOADER_FROM` に path を渡す）。
- eMMC の module は外す（U-Boot は起動した媒体の boot.scr を先に読むが、eMMC から起動すると eMMC の OS が先に起動する）。

## 媒体

```sh
cd /home/awe/zedBSD-worktrees/p7-ws313
# text の console の image（GPU なし）
make ZEDBSD_CONFIG=config/ci/config-odroid-c5.mk ODROID_C5_BOOTLOADER_FROM=/path/to/hardkernel-odroid-c5.img disk-image
# desktop の image（Mali の driver つき）
make ZEDBSD_CONFIG=config/ci/config-odroid-c5-keiland.mk ODROID_C5_BOOTLOADER_FROM=/path/to/hardkernel-odroid-c5.img disk-image
# 書き込み（/dev/sdX は microSD。間違えると他の disk を壊す）
sudo dd if=build/odroid-c5/hdd-image.img of=/dev/sdX bs=4M conv=fsync status=progress
```

image の形: MBR、先頭 8 MiB は bootloader（Hardkernel の image から写した物）、part 1 FAT32（8 MiB から、`boot.scr`・`vmunix`・`odroid-c5.dtb`・`data.img`・`swapfile`）、part 2 UFS の root。

## 確かめる順と判定

`plan/ws313/tests/c5-night-test.sh [--device /dev/ttyUSBn] [--plug ID]` が順に待ち、最後に stage ごとの PASS・FAIL・SKIP を出す（最初の FAIL で止まる）。

| 順 | stage（UART の行） | 判定と、FAIL の時に見る所 |
| --- | --- | --- |
| 1 | `zedBSD: boot.scr on` | U-Boot が SD の boot.scr を読んだ。出なければ bootloader の区画（`--bootloader-from`）か boot の順。U-Boot の prompt（`odroidc5#`）で `printenv`・`fatls mmc 1:1` |
| 2 | `zedBSD: booti` | kernel と DTB を読めた。この後 U-Boot の `Starting kernel` が出るか |
| 3 | `ODROID-C5 ENTRY` | HAL の最初の行（UART は U-Boot の速さのまま）。出なければ kernel の置き場所（booti の移動）、`fdt_high` |
| 4 | `ODROID-C5 EL1 PASS`・`FDT PASS` | DTB を読めた。`FDT parse error: …` ならその名前 |
| 5 | `ODROID-C5 FRAMEBUFFER PASS` | HDMI に三色（左から赤・緑・青）の 3 秒の模様と白い枠。色の順が違えば format（a8r8g8b8）、何も出なければ U-Boot の `osd`・`vout` の命令（boot.cmd）。`framebuffer unavailable` は UART だけで進む（FAIL と記す） |
| 6 | `ARM64 MEMORY MAP PASS` | memory の量（2 GiB の見込み、DTS の linux,usable-memory） |
| 7 | `ARM64 IRQ READY`・`ARM64 SMP cpu 3 online` | GIC と PSCI の CPU_ON。止まれば `ARM64 SMP cpu N … did not come up` |
| 8 | `login: ` | SD の driver（`s905x5m-…` の行）と root の mount。止まれば SD の行 |
| 9 | login の後の命令 | `uname -a`、`df /`、`dmesg | grep s905x5m`、`lsusb`（keyboard・hub）、`ifconfig -a`（en0 の link と DHCP）、`dmesg | grep mali`（GPU_ID） |
| 10 | desktop | keiland の image で HDMI に Keiland が出るか（写真） |

固まった時は、UART の log を取り直さず、board の状態を写真と UART の最後の行で Q1 に送る（QEMU ではないので gdbstub は無い）。

## script

- `c5-night-test.sh`: 電源（SwitchBot の plug を名指した時）→ `c5-serial.py` → 要約。各 command に時間の上限。何も消さない。
- `c5-serial.py`: 921600 baud の生きた line を stage ごとに待つ（`--scale` で時間を伸ばす）。`--login USER:PASSWORD`（既定は image の kei:kei）と `--commands`。
