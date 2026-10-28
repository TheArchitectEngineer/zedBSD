<!-- awesome-plan project=zedbsd record=ws307-p005 -->

# ws307-p005 base の道具（mkswap の partition の mode・diskpart の batch・FAT の乱数の serial・4096 byte の sector）

- 親: [WS307](../ws.md)。仕様: [spec.md](../spec.md) §5.3・§5.9・§11.3。Queue: q1665 / q1665-i01（担当 P4、2026-10-29）。
- Status: cleared（2026-10-29 Q1 の判定: 4096 の NVMe・USB の FAT・swap・UFS と 512 の回帰が QEMU で PASS）
- 承認: 2026-10-29 ユーザー「P4にWS307」（Q1 の中継）。

## 目的

installer の helper（p009）が使う base の道具を足す: `mkswap` の partition の mode、`diskpart` の batch（add・delete と、型・PARTUUID・名前・attribute の変更を 1 回の確認で 1 回書く）、zedBSD 独自の「未割り当て」の型 GUID、`mkfs -t fat32` の乱数の serial、4096 byte の logical sector（mkfs の FAT32・mkswap・diskpart）。

## 結果（2026-10-29、P4）

- 「未割り当て」の型 GUID（U3、spec §5.3）: 乱数の v4 GUID `7EF6A3E2-F243-445D-B77A-C37CA9605418`（`python3 -c "import uuid; print(uuid.uuid4())"` で 1 回作った）。`include/uapi/gpt-type.h`（新規）に、ESP・zedBSD の UFS・swap・未割り当てと、他の OS の主な型（Basic Data・Microsoft Reserved・Windows Recovery・Linux・FreeBSD boot・APFS）の GUID を置いた。
- `diskpart`（`userland/base/diskpart/`）:
  - `diskpart batch DISK OPERATION...`（`add SLOT START COUNT TYPE PARTUUID NAME`・`delete SLOT`・`set SLOT TYPE PARTUUID NAME ATTRIBUTES`、GPT だけ、最大 32 の操作）。順に表に当て、`WRITE name:registration` の 1 回の確認で 1 回書く（`dp_write`）。add・delete と違い、書く前の reload（`BLKREREADPART`）の成功を確認の前と後の 2 回とも求める（導入先の partition が mount 中なら何も書かない、spec §4.4・§7.1）。結果の active な entry が 16 を越えれば ENOSPC。exit は add と同じ（0 成功、3 書いたが reload の失敗、2 command line、1 その他）。
  - 表の codec に `dp_modify`（1 つの entry の型・PARTUUID・名前・attribute を、slot と範囲を保って置き換える。他の entry と同じ PARTUUID・GPT に入らない名前は entry を元に戻して拒む）。
  - 人の読む `show` に `kind=`（型の名前、表に無い型は `-`）を足した。`--machine` の出力は変えていない。
- `mkswap /dev/PARTITION`（`userland/base/mkswap/swap-device.c`、新規）: canonical な名前、partition だけ（whole disk は EINVAL）、`BLKRESERVE`、`FORMAT name:registration` の確認、header の page を 1 回の page の書き込み（4096 byte の sector でも sector 単位）、`fsync`、kernel と同じ parser での読み直し。大きさは ZEDSWAP2 の 64 bit（file の mode の INT32_MAX を当てない）、page の倍数、slot が 32 bit に入る（kernel の `slot_count > UINT32_MAX` の拒みと同じ）。sector が 4096 の page を割り切らない device は EOPNOTSUPP。file の mode（`mkswap FILE`）は変えていない。
- `mkfs`（`userland/base/mkfs/block-command.c`）:
  - `mkfs -t fat32 DEVICE` の serial を乱数（`getentropy`、0 は引き直す）に。今の値（disk の登録番号）に頼る利用者は tree に無い（`git grep` で確かめた、loader・kernel は serial を `boot0=UUID=` の照合にだけ使う）ので、option にせず既定を乱数にした（spec §11.3 の「p005 で決める」）。
  - `mkfs -t fat32 DEVICE` は 4096 byte の logical sector の device も受け付ける（FAT32 の geometry は既に 4096 を扱う）。native UFS の device の mode は 512 だけのまま（下の「残り」）。
  - `--check-size` に `--sector-size=512|4096` を足した（FAT32 は両方、native UFS は 512 だけ）。出力の sector の欄はその値（今まで固定の 512）。
- UFS の inode の密度（spec §5.6）: native の 256 inode／cylinder group（約 62 MiB）で 1 GiB に約 4,200、8 GiB で約 33,000。unit の file の数は p003 の index で出るので、密度を変えるかは p003 で量ってから決める（mkfs は変えていない）。 → p003 の段 6（2026-10-29）で量った: 全 unit の entry 2,985 に対し、導入先の最小の root（約 1.5 GiB）で約 6,000 inode。密度は変えない（[phase003/phase.md](../phase003/phase.md)）。

## 確かめ（2026-10-29、P4、host）

- build: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws307 build/ws307/bin/diskpart build/ws307/bin/mkswap build/ws307/bin/mkfs` → rc 0、`error:`・`warning:` 0（`-Wall -Wextra -Werror`）。
- `plan/ws307/tests/host/diskpart-table-test.c`（host の cc、`userland/base/diskpart/table.c` と）: 512・4096 byte の sector の 256 MiB の memory の disk に ESP と他の OS の entry を書き、batch と同じ順で add 2 つ・set 2 つ（root・swap、PARTUUID の置き換え、attribute）・重なる PARTUUID と未使用の slot と長い名前の拒み・書いて読み直し・他の entry が byte で同じ・2 つの delete → `diskpart-table-test: PASS`。
- `plan/ws307/tests/host/mkswap-partition-test.c`（`swap-format.c`・`swap-codec.c` と）: 大きさの規則（2 page、1 page と端数は EINVAL、5 GiB は partition で可・file で EFBIG、slot の 32 bit の上限）、5 GiB の sparse の file に header の page を書いて kernel の parser で読む → `mkswap-partition-test: PASS`。
- `plan/ws307/tests/host/mkfs-fat32-test.c`（`fat32-format.c` と）: 512 MiB の ESP を 512・4096 byte の sector で書いて writer で verify → PASS。`/usr/sbin/fsck.fat -n` が両方を読んだ（`0 files, 1/1032412 clusters`・`0 files, 1/130784 clusters`）。
- 未実施: guest（QEMU）での `diskpart batch`・`mkswap /dev/…`・`mkfs -t fat32` の 4096 の device（p006 の kernel の FAT の 4096 の後でないと mount できない）。p009 の QEMU の試験で helper から使う時に確かめる。

## q1665-i04（2026-10-29、P4、worktree p4-ws307h）

- native UFS の fragment 4096 の profile: 実装した（commit bfa7afcdb、詳細と host の試験は [phase006/phase.md](../phase006/phase.md) の「q1665-i04 の結果」）。4096 の NVMe の guest で mkfs・mount・読み書きを確かめた。
- QEMU の短い確認: `sh plan/ws307/tests/p005-batch.sh build/ws307h-ssh/hdd-image.img` PASS（`build/ws307-test/p005-batch.uGro97`、512 と 4096 の NVMe で `diskpart batch`・native UFS・`mkswap /dev/PARTITION`）。`mkfs -t fat32` の 4096 の device は p006-4k で ok。

## 残り

- 無し（Phase の受け入れの判定は Q1。UFS の上の swap file の 4096 は p006 の再試験の結果待ち）。
