# zedBSD-firmware

Device firmware used by [zedBSD](https://github.com/awemorris/zedBSD), kept in
one place so that a zedBSD build fetches it from one host.

Every file here is an unmodified copy, byte for byte, of a file published by
its vendor or distributor at the commit named below, together with the license
text that the upstream distributes with it. Nothing here was rebuilt, patched,
or reformatted. The zedBSD build checks each file's size and SHA-256 against
the values in its own source tree, so the copies here are accepted only while
they are identical to the upstream files; when this repository cannot be
reached, the build fetches the same files from the upstream instead.

The firmware is not covered by zedBSD's license. Each directory's files are
under the license named for that directory, and each directory carries that
license's text and a `README.md` that says what the firmware is, its copyright
and where it came from. Read the license before you redistribute or use the files. In
particular, none of these licenses allows modification, and several forbid
reverse engineering or limit use to particular hardware.

## Directories

| Directory | zedBSD package / use | Upstream and commit | License (text in the directory) | Main conditions |
| --- | --- | --- | --- | --- |
| [`i915/`](i915/) | `i915-firmware`: Intel display DMC for Alder Lake-P and Tiger Lake | [linux-firmware](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git) tag `20260410`, commit `dc85ccedc9c973682fbcf4d628ca61174bcc3120` | `LICENSE.i915` (WHENCE: "Redistributable") | Redistribution in binary form without modification, with the copyright notice and disclaimer; no reverse engineering, decompilation or disassembly; patent license limited to use alone or with an OSI-approved open source OS |
| [`intelax211/`](intelax211/) | `intelax211-firmware`: Intel AX211 WiFi (`iwlwifi-so-a0-gf-a0-89.ucode`, PNVM) | linux-firmware, same commit | `LICENCE.iwlwifi_firmware` (WHENCE: "Redistributable") | Same terms as `i915/` |
| [`intelbt/`](intelbt/) | `intelbt-firmware`: Intel AX211 Bluetooth (Solar, `BT_Solar_REL82122_23.50.26053.82122`) | linux-firmware, same commit | `LICENCE.ibt_firmware` (WHENCE: "Redistributable") | Same terms as `i915/` |
| [`rtl8822b/`](rtl8822b/) | `rtl8822b-firmware`: Realtek RTL8822B WiFi (`rtw88/rtw8822b_fw.bin`, v30.20.0) | [endlessm/linux-firmware](https://github.com/endlessm/linux-firmware) commit `2f56219d20e4becccd718963fc3bcc671c543ce5` (the same bytes are in linux-firmware commit `458e40fdbb4dad5134ec230a42df21aea1b5baf8`) | `LICENCE.rtlwifi_firmware.txt` (WHENCE: "Redistributable") | Redistribution in binary form without modification, with the notice and disclaimer; no reverse engineering; patent license limited as above. `WHENCE` here is the endlessm tree's |
| [`brcmbt/`](brcmbt/) | `brcmbt-firmware`: Bluetooth patch of the Raspberry Pi 4's BCM43455 (`BCM4345C0.hcd`) | [RPi-Distro/bluez-firmware](https://github.com/RPi-Distro/bluez-firmware) commit `adb4d55c8f193ff16ece74dd72fd6e8801632103` (1.2-9+rpt4); `LICENCE.cypress` from linux-firmware commit `dc85cced…` | Cypress Wireless Connectivity Devices EULA (`LICENCE.cypress`; `copyright` is the tree's `debian/copyright`) | Reproduction and distribution in object code form only, solely for use with Cypress integrated circuits; no modification; no reverse engineering except where the law allows it |
| [`bcm43455/`](bcm43455/) | `bcm43455-firmware`: Raspberry Pi 4 WiFi (`cyfmac43455-sdio-minimal.bin` 7.45.241, the CLM blob, the Pi 4 NVRAM) | [RPi-Distro/firmware-nonfree](https://github.com/RPi-Distro/firmware-nonfree) commit `3bab0f823f5b53150b76aab77093adef6655b920` (1:20260519-1~bpo13+1+rpt1); `LICENCE.cypress` and `GPL-2` from linux-firmware commit `dc85cced…` | Cypress EULA (`LICENCE.cypress`; `copyright` is the tree's `debian/copyright`). The NVRAM values are also published in linux-firmware under GPL-2.0+ (`GPL-2`) | As `brcmbt/` for the firmware and the CLM blob |
| [`rpi4-boot/`](rpi4-boot/) | The Raspberry Pi 4 SD card's boot partition: GPU firmware, device tree, `disable-bt` overlay | [raspberrypi/firmware](https://github.com/raspberrypi/firmware) commit `3d301dd924bcd758a4c8cb19fe8531031f033f43` (`boot/`) | `start4.elf`, `fixup4.dat`: `LICENCE.broadcom`. `bcm2711-rpi-4-b.dtb`, `overlays/disable-bt.dtbo`: GPL-2.0 (`COPYING.linux`) | Broadcom: redistribution in binary form without modification, with the notice and disclaimer, and use only for developing for, running or using a Raspberry Pi device. Device tree: GPL-2.0; its source is `arch/arm/boot/dts/broadcom/bcm2711-rpi-4-b.dts` and `arch/arm/boot/dts/overlays/disable-bt-overlay.dts` of [raspberrypi/linux](https://github.com/raspberrypi/linux) commit `89586905b8603e545cce9089a81f5f35d65bc998` (the firmware commit's `extra/git_hash`) |

`WHENCE` in `i915/`, `intelax211/` and `intelbt/` is linux-firmware's index of
its files and their licenses at the commit above.

## Updating

A file here is never replaced in place. A new firmware version is added as a
new file (or in a new commit), and zedBSD's package names the new size and
SHA-256 and the new commit of this repository. `SHA256SUMS` lists every file.

## Files
