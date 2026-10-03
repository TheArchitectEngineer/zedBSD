<!-- awesome-plan project=zedbsd record=ws073p009 -->

# ws073-p009: kernel が持つ boot の FAT を公開する — BOOT を /boot、ESP を /boot/esp

Status: cleared（2026-09-27）
Disposition: normal
Parent: [WS073](../ws.md)
Origin: ws073-p002 の判断が要る点（BUG-065 の修正で ESP の 2 度目の mount が EBUSY になった）

## ユーザーの判断（2026-09-27、原文）

「ESPは/boot/espにします。/bootはBOOTという名前のFATパーティションですね。UEFIのみのイメージでBOOTパーティションがない場合もあります。」

## 目的と受け入れ

kernel が private に持つ boot の FAT を 2 度目の mount を許さずに running system へ見せる: BOOT の FAT を `/boot`、ESP を `/boot/esp`。
BOOT の無い UEFI だけの image では `/boot` は root の directory で、ESP は `/boot/esp`。公開に失敗しても起動は続く。amd64・pcat・rpi4 の boot test を保つ。

## 各 platform の image の layout と結果

| image | kernel が持つ boot の FAT | 結果 |
| --- | --- | --- |
| amd64 native（UEFI だけ。ESP・UFS root・swap） | boot0 = ESP（loader の origin） | `/boot` は root の directory、ESP を `/boot/esp` |
| amd64 hybrid（ESP・BOOT の FAT に overlay の image） | boot0 = BOOT | BOOT を `/boot`。ESP は kernel が持たないので公開しない（普通に mount できる） |
| pcat（BIOS、BOOT の FAT に overlay の image） | boot0 = BOOT | BOOT を `/boot` |
| pc98（BIOS、BOOT の FAT） | boot0 = BOOT | pcat と同じ経路（下の確認） |
| rpi4（MBR の FAT32 `ZEDRPI4` と UFS root、parameter の無い legacy autoroot） | 持たない（legacy の UFS root は FAT を mount しない） | 何も公開しない。FAT は普通に mount できる |
| rpi4 の legacy ARM overlay（FAT の rootfs.img） | 起動の間ずっと private に持つ | BOOT として `/boot` |

## 設計

- ESP の判定は partition table の型: GPT の EFI System の型 GUID（C12A7328-F81F-11D2-BA4B-00A0C93EC93B）か MBR の型 0xEF。
  `PARTITION_EFI_SYSTEM`（`include/kern/partition.h`）を `gpt.c`・`mbr.c` が立て、`partition_disk_is_efi_system()`（`partition.c`）が引く。
  FAT の volume label（`BOOT`・`ESP`）は名前の目安で、判定には使わない（他の OS の ESP の label は様々）。ESP でない boot の FAT が BOOT。
- 公開は `vfs_publish_boot_filesystems()`（`src/kern/vfs.c`、runtime の filesystem の mount の後）: boot の slot の private な mount の最初の BOOT と
  最初の ESP（と legacy ARM overlay の boot の mount）を選び、`/boot` を root に作り（無ければ）、BOOT の root を `/boot` に bind、`/boot` の中に
  `esp` を作り（無ければ）ESP の root を `/boot/esp` に bind する。bind は private な mount への参照を持つので、kernel と namespace は 1 つの FAT の
  状態を共有する。2 度目の mount は従来どおり EBUSY（BUG-065）。`umount /boot/esp` は見せるのをやめるだけで kernel の mount は残る。
- `mount` の一覧: private な mount の bind の source を `(private)` でなく disk の `/dev/` の名前で出す（`mount_info_private_source()`、`mount.c`）。
- HAL は触れていない。

## 検証（QEMU。実機は未実施）

- build: amd64 guest（`plan/ws035/tests/config-amd64-guest.mk`）の vmunix warning 0。pcat・rpi4・amd64 hybrid の disk-image warning 0。
- amd64 native（main の測定用 image の vmunix だけを差し替え、SSH）: [tests/boot-publish.sh](../tests/boot-publish.sh) 11 件 PASS: `/dev/nvme0n1p1 on /boot/esp
  type fat (rw,bind)`、`/boot/esp/zedbsd.cfg` が見える、`/boot` 自体には何も mount されていない、`/boot/esp` での mkdir・rmdir、`mount -t auto
  /dev/nvme0n1p1` は EBUSY のまま、`umount /boot/esp` で消え、その後も partition は kernel が持つ（EBUSY）。
- boot test（画面の login）:
  - amd64 lean（native、UEFI、`plan/ws045/tests/config-amd64-base.mk`、warning 0）: PASS、画面に `vfs: ESP published at /boot/esp`
    （`build/ws073-img/boot-test-p009/login.png`）。
  - amd64 hybrid（`tests/config-amd64-hybrid.mk`）UEFI: PASS、画面に `vfs: boot filesystem published at /boot`（`build/ws073-hybrid/boot-test/login.png`）。
    BIOS（`QEMU=qemu-system-x86_64 BOOT_MODE=bios-ide`）: PASS（`boot-test-bios64/login.png`）。`qemu-system-i386`（bios-ide の既定）では
    amd64 の image は起動しない（p009 の前の kernel でも同じ。試験の側の選択の誤りで不具合ではない）。
  - pcat（`config/ci/config-pcat.mk`、BIOS/IDE）: PASS、画面に `vfs: boot filesystem published at /boot`（`build/ws073-pcat/boot-test-p009/login.png`）。
  - rpi4（`config/ci/config-rpi4.mk`、`BOOT_MODE=raspi4b`）: PASS（`build/ws073-rpi4/boot-test-p009/login.png`。legacy の UFS root で公開は無い）。
  - pc98（`config/ci/config-pc98.mk`、PC-98 の QEMU、`plan/tools/pc98-boot.py`）: login と `uname -a`。画面（text VRAM）に `vfs: boot filesystem published at /boot`
    （`build/ws073-pc98/boot-p009/login.txt`・`login.png`）。
- 規約: `tests/style-diff.py`（vfs.c・mount.c・partition.c・gpt.c・mbr.c・partition.h）0。

## 残り

- **FAT に file を作れない**（`echo > /boot/esp/x`・普通に mount した FAT でも `Operation not supported`）。FAT の driver が、mount が見せる mode（0755）
  と違う mode の作成を断る設計のため。p009 の前からある → [BUG-071](../../bugs/BUG-071.md)、ws073-p010。
- amd64 hybrid の ESP と rpi4 の UFS root の FAT は kernel が持たないので自動では見えない（普通に `mount -t auto /dev/... /boot/esp` などで mount できる）。
  自動で mount するかは必要になったら決める。
- 起動の後に kernel が持つ ESP を `/boot/esp` から外した後、再び見せる道具は無い（再起動で戻る）。
