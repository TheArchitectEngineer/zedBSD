<!-- awesome-plan project=zedbsd record=ws073p012 -->

# ws073-p012: BUG-072 — zedBSD が書いた FAT32 の metadata を仕様どおりに（`..`、FSInfo、日時）

Status: cleared（2026-09-27）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-072](../../bugs/BUG-072.md)（ws073-p010 の確認で見つけた。既存）

## 目的と受け入れ

zedBSD が FAT32 の volume に directory・file を作り、動かし、消した後に、host の `fsck.fat -n` が何も直すものを出さず、mtools が正しい日時を出す。
amd64 の ESP と rpi4 の firmware の FAT に zedBSD が書いた後も、それらが起動し、loader・firmware の file は変わらない。

## 再現（修正前、QEMU。p010 の kernel）

空の FAT32 に zedBSD が `mkdir dir`・`cp /etc/passwd`・`echo > dir/x.txt` をした後、host の `fsck.fat -n`: 「Invalid '..' entry in the second slot」、
「Free cluster summary wrong (129021 vs. really 129018)」。`mdir` の日時は全て `1980-00-00 0:00`（月・日 0 の不正な日付）。

## 原因

1. `fat_raw_initialize_directory()`・`fat_raw_update_dotdot()`（`src/drivers/fs/fat.c`）は `..` に親の最初の cluster を書く。親が root のとき FAT32 では
   root の cluster（2）になるが、仕様は 0。
2. FSInfo の空き cluster の数と次の空きの hint を一度も書いていなかった（mount で sector の番号を読むだけ）。
3. 新しい record の作成・更新・参照の日時を書いていなかった（`utimes` の setattr だけが書く）。
4. 確認の中で見つけた 4 つ目: `sync(2)`（`mount_sync_all()`、`src/kern/mount.c`）は bind を飛ばし、private な mount は namespace の list に無いので、
   kernel が持ち p009 で `/boot`・`/boot/esp` に見せた boot の FAT は sync されなかった（data の書き込みは FAT の driver が即時に durable にするが、
   FSInfo と開いた file の directory entry は sync を待つ）。

## 修正

- `fat_raw_dotdot_cluster()`: 親が root なら 0。directory の作成と rename の両方で使う。
- FSInfo: mount の state に空き数（`free_clusters`・`free_clusters_known`・`fsinfo_dirty`）。この mount が table を初めて変える前に table から数え
  （`fat_free_count_prepare()`。以前の driver が残した古い値は信じない）、`fat_table_transaction()` と `fat_raw_set_cluster_immediate()` が commit した
  変更で増減する（最初の copy の旧値で「空き→使用」「使用→空き」を数える）。`fat_sync_mount()` が `fat_fsinfo_write()` で FSInfo（signature を確かめて、
  空き数と allocation の hint）を書く。FAT32 だけ。数えられないときは FSInfo に触れない。
- 日時: `fat_raw_stamp_record()` が新しい record（FAT16 と FAT32 の作成、`.`・`..`）に作成・更新・参照の日時を、size の変わった file の flush に
  更新・参照の日時を書く（`clock_realtime` を FAT の date・time に。1980 年より前の clock は書かない）。
- `mount_sync_all()`: private な mount の bind（`/boot`・`/boot/esp`）は、その private な mount を sync する（`mount_sync_target()`、重複は 1 度）。
- HAL は触れていない。

## 検証（QEMU、KVM、4 GiB、4 vCPU、NVMe。実機は未実施）

- build: `plan/ws035/tests/config-amd64-guest.mk` の vmunix warning 0。
- [tests/fat-metadata.sh](../tests/fat-metadata.sh): guest で空の FAT32（2 台目の NVMe）に最上位と入れ子の directory、大小の file、消した file、
  directory の親の間の移動と root への移動 → host で `fsck.fat -n` が rc 0・何も出さない、`mdir` の全ての entry が当日の日付（修正前は 1980-00-00）。
- [tests/fat-create.sh](../tests/fat-create.sh)（p010 の回帰）: 22 件 PASS、その後の `fsck.fat -n` rc 0。
- amd64 の ESP: 測定用 image の guest で `/boot/esp` に directory・file・200 KiB の file・`EFI/` の下への移動・`NOTE.TXT` を書いて `sync`。その disk を
  [tests/fat-compare.py](../tests/fat-compare.py) で見ると `fsck.fat -n` が clean、`EFI/BOOT/BOOTX64.EFI`・`vmunix`・`zedbsd.cfg` は元と同じ。
  その disk の `plan/tools/boot-test.sh`（UEFI が書かれた ESP から起動）PASS（`build/ws073-p012-boot/login.png`）。sync の修正の前は同じ手順で
  「Free cluster summary wrong」（private な ESP が sync されなかった）。
- rpi4 の firmware の FAT（`ZEDRPI4`、MBR 0x0C）: rpi4 の image を amd64 guest の 2 台目の NVMe に付け、zedBSD が FAT に directory・file・300 KiB の file・
  `overlays/` の下への移動・`config.txt` への追記を書いた。`fat-compare.py`: `fsck.fat -n` clean、`start4.elf`・`fixup4.dat`・`bcm2711-rpi-4-b.dtb`・`vmunix`・
  `overlays/disable-bt.dtbo`・`data.img` は元と同じ、`config.txt` の追記が読める。その image の `BOOT_MODE=raspi4b` の boot test PASS
  （`build/ws073-rpi4/boot-test-p012/login.png`。QEMU の raspi4b は firmware を走らせず kernel を直に与えるので、firmware が書かれた FAT を読む確認は実機で。未実施）。
- boot test（p012 を含む image、warning 0）: lean amd64 native（`build/ws073-img/boot-test-p012/login.png`）・pcat（BIOS/IDE、FAT の overlay、
  `build/ws073-pcat/boot-test-p012/login.png`）・rpi4（raspi4b、`build/ws073-rpi4/boot-test-p012b/login.png`）全て PASS。
- 規約: `tests/style-diff.py`（fat.c・mount.c）0。
