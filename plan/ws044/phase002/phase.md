<!-- awesome-plan project=zedbsd record=ws044p002 -->

# ws044-p002: rpi4 の SD の boot partition を FAT32 に

Phase ID: `ws044-p002`
Parent: [WS044](../ws.md)
Status: **cleared**（2026-09-27、WS036/WS044 の subagent。QEMU まで。実機はユーザー）
Phase disposition: normal

## 目的

2026-09-24 ユーザー指示「rpi4ではbootパーティションはFAT32にしてください」。SD の第 1 partition（firmware・kernel・data・swap の file）を
FAT16 から FAT32 にする。

## 着手の判断（「kernel の安定化の後」）

ユーザーは「シリアル以外は先にカーネルを安定してからでいい」とした。安定化の基準は決まっていないので、次を仮の基準にして着手した
（可逆。`判断が要る点` に記録）: QEMU raspi4b で起動・login・SD の読み書き・動的 link の試験が通り、起動の間欠の fault（p012 で直した sched の競合）
が再現しないこと。2026-09-27 の main（WS053 の LTO、WS048 の merge の後）で満たす（下の検証）。実機の起動は ws044-p006〜p009 の修正の後、ユーザーの
確認を待っている。

## 変更

| 所在 | 変更 |
| --- | --- |
| `platform/arm64/tools/make-rpi4-hdd-image.py` | partition の型を `0x06`（FAT16）から `0x0C`（FAT32、LBA）に。`mformat -F`（FAT32）で、hidden sectors に partition の開始 LBA（2048）。大きさ（128 MiB）と配置は変えない |
| `platform/arm64/tools/check-rpi4-hdd-image.py` | 型 `0x0C` と FAT32 の BPB（固定 root 無し、16-bit の FAT の大きさ 0、32-bit の FAT の大きさ、`FAT32   `、署名）を確かめる |

kernel は変えていない。FAT の driver（`src/drivers/fs/fat.c`）は FAT12/16/32 を BPB で判別し、MBR は型を見ない（拡張 partition だけ除く）。

## 検証

| 試験 | 結果 |
| --- | --- |
| image の生成と検査（`make disk-image` の中の `check-rpi4-hdd-image.py`） | PASS。`minfo` で FAT32、cluster 2 sector、hidden 2048 |
| QEMU raspi4b の起動（`BOOT_MODE=raspi4b plan/tools/boot-test.sh`）、FAT32 の image | PASS（`build/boot-rpi4-fat32/login.png`、p026 の image `build/boot-rpi4-p026/login.png` も FAT32） |
| guest で FAT32 の boot partition を mount（`mount -t fat /dev/mmcblk0p1 /mnt/boot`、`plan/ws044/tests/rpi4-serial.sh`） | mount でき、長い名前（`bcm2711-rpi-4-b.dtb`、`LICENCE.broadcom`）も読める。`config.txt` を読める |
| 同じ guest での file の作成 | `Operation not supported`。FAT16 の旧 image でも同じ（この mount の経路は作成を持たない）ので回帰ではない |
| 実機（firmware が FAT32 から `start4.elf`・`config.txt`・`vmunix` を読んで起動） | **未実施**（ユーザー。QEMU は firmware を動かさない） |
