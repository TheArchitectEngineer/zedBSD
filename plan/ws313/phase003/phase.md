<!-- awesome-plan project=zedbsd record=ws313-p003 -->

# ws313-p003: 媒体と夜の試験の手順

Status: in-progress（実装済み・実機未試験）
Queue: q1675-i01・q1675-i02（P7）
WS: [WS313](../ws.md)、設計 [design.md](../design.md) §2・§6・§7

## 実装（2026-10-29）

- `platform/arm64/odroid-c5.mk`: Hardkernel の DTS（`platform/arm64/odroid-c5/dts-sources.txt` の 31 file、pinned の raw URL、size と SHA-256）を
  `platform/arm64/tools/fetch-odroid-c5-dts.py` で `$(BUILD)/dts-sources/` に取り、cpp と dtc で `odroid-c5.dtb`（zedBSD の追加は `odroid-c5/odroid-c5.dts`: /chosen の simple-framebuffer）。
  `boot.scr`（`odroid-c5/boot.cmd` を `tools/make-uboot-script.py` で legacy image に、login= は config から）。`hdd-image.img`（`tools/make-odroid-c5-hdd-image.py`）。
- 試験: [tests/README.md](../tests/README.md)、`tests/c5-night-test.sh`、`tests/c5-serial.py`。

## 確かめ

- boot.scr: `mkimage -l` で header を読め、payload が `mkimage -T script` の物と一致。
- DTB: build でき、HAL の parser の host の試験が PASS（p002）。
- 2026-10-29（q1675-i02）: `make ZEDBSD_CONFIG=config/ci/config-odroid-c5.mk odroid-c5-fdt-host-test` が PASS（DTS を worktree の `build/odroid-c5/dts-sources/` に取り直し、
  `uart fe07a000 irq 201, 4 CPUs, 7 reserved, fb 7f800000`）。
- 2026-10-29: `make … sysroot-arm64` rc=0（worktree の `build/arm64/sysroot`）、`make ZEDBSD_CONFIG=config/ci/config-odroid-c5.mk BUILD=build/odroid-c5-image world` rc=0
  （zedBSD の source の warning 0。warning は openssl の deprecated と perl の locale だけ）、`build/odroid-c5-image/hdd-image.img` rc=0（bootloader 無し、456 MiB、
  FAT に boot.scr・vmunix・odroid-c5.dtb・data.img・swapfile、`ufs-root.img: canonical zedBSD UFS OK`）。kernel の LOAD は 0x80000〜0x8d6000 で、予約の 0x04e00000 より下。
  この image は周辺の driver が stub の時の source の物（夜の image は main で作り直す）。
- bootloader: ユーザーの判断は「取得して image に入れる」だが、公開の取得先が無く（dn.odroid.com は 403、github・Launchpad に無い）、今は `--bootloader-from`。URL はユーザーに確かめ中。
- bootloader の notice（2026-10-29、q1675-i03）: `platform/arm64/odroid-c5-bootloader.notice`。`--bootloader-from` の時、image の tool が FAT に `/BOOTLOADER.txt`（notice＋donor の名前＋写した区画（MBR の 440 byte と sector 1〜）の SHA-256）を置く（`platform/arm64/tools/make-odroid-c5-hdd-image.py --notice`、既定はこの notice）。
  調べ（GitHub の API の GET だけ）: Hardkernel の build の tree は github.com/hardkernel/u-boot の branch `odroidc5-v2023.01`（c99f3409d5、2026-02-04「Firmware update with Amlogic BSP 202508」）。Amlogic の BL2・BL30・BL31・BL32 と U-Boot は git.odroid.com の submodule（git.odroid.com は読んでいない）。Amlogic の blob の license の文は見つからず、再配布の条件は不明 → notice に「作った人の使用のため、条件が分かるまで渡さない」と書いた（推し）。U-Boot は GPL-2.0-or-later で、対応する source は submodule `bl33/v2023`。
  確かめ: 偽の donor（440 byte の code と 4 KiB の乱数）で tool を流し rc=0、FAT の `BOOTLOADER.txt` の末尾の SHA-256 が別に計算した値と一致。`--no-bootloader` では置かない（rc=0、FAT は 5 file）。
- CSF の firmware: package `mali-csf-firmware`（`userland/firmware/mali-csf/`）を作り keiland の config に入れた。license の同意はユーザーが build の時に答える。
- 実機: 未実施。
