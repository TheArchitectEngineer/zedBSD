<!-- awesome-plan project=zedbsd record=ws238-p001 -->

# ws238-p001: amd64 の native の layout を ESP・FAT32・UFS の root・swap に

Status: in-progress（実装と host の確認まで。QEMU・5330 は未実施、T1 とユーザーに依頼する）
Disposition: normal
Parent: [WS238](../ws.md)
Queue: 2026-10-14 夜 Q1 → P1（ユーザーの指示は ws.md の原文。追加の /boot/boot0・/boot/efi も）
Branch: `agent/p1-ws238`（main 732dd30e0 から。WS080 p002 の `agent/p1` とは別にした: p002 は QEMU の回帰まで main に入れないため）

## 決めたこと

| 点 | 決定と理由 |
| --- | --- |
| partition の並び | 1 ESP 64 MiB（`EFI/BOOT/BOOTX64.EFI` だけ）、2 `zedBSD-boot` FAT32 256 MiB（GPT の Basic Data の type `EBD0A0A2-…`、volume label `ZEDBSD`、`vmunix`・`zedbsd.cfg`・`logo.ppm`）、3 `zedBSD-root` UFS、4 `zedBSD-swap`。大きさは `AMD64_NATIVE_BOOT_MIB ?= 256`（kernel の更新と log の余地。FAT32 の下限は約 33 MiB、64 MiB 未満は zedimage-host が拒む） |
| loader | **変えない**。`bootx64.c` は同じ disk の全ての FAT を、自分の volume を先にして順に開き、`/zedbsd.cfg` のある volume を選ぶ（ESP は無いので MISSING、FAT32 が MATCH）。kernel・logo は選んだ volume から読み、`zedbsd-config.c` が `boot0=UUID=<選んだ FAT の serial>` を足す。ESP と FAT32 の serial は違う値に（zedimage-host）。 |
| /boot/boot0 | kernel の slot 0（loader の boot0= の FAT）は今まで、overlay の image か swap file が `boot0:` を名指す時だけ `/boot/boot0` に出ていた（native の amd64 は private のまま）。**slot 0 は常に `/boot/boot0` に出す**（`src/kern/vfs.c`、RPi4 で BUG-288 からしていたことを全 platform に）。native の image では FAT32（ZEDBSD）が boot0 になる。 |
| /boot/efi | 新しい `vfs_publish_efi_system_partition()`: loader が報告した firmware の partition（`kern.boot.firmware_partition`、`PARTUUID=`）を引き、GPT の ESP の type（`partition_disk_is_efi_system`）なら `/boot/efi` に出す。**ESP が boot slot と同じ partition なら、その slot の mount を 2 か所目に bind する**（同じ partition の 2 回目の mount は BUG-065 で拒まれるので、1 つの FAT の状態を 2 つの名前で見せる。symlink より、`/boot/efi` が自分の mount point として `mount` に出て、片方が消えても他方が残る点で良い）。そうでなければ private に mount して bind（fstab の adopt を許す）。BIOS・MBR の起動は何もしない。失敗は log だけで起動は続く。 |
| 書き換えの道具 | menuconfig・settings・sysupdate は `zedbsd.cfg` を書かない（grep）。installer（zedinst）は 2026-10-07 に削除済みで、`docs/howto/boot-and-storage.md` の installer の節（ESP に vmunix・zedbsd.cfg）は残っている（今回は触らない、Q1 に報告）。`boot-test.sh` は native の image の中を見ない。 |

## 変えた file

- `tools/build/zedimage-host.c`（`disk_create_native`）、`platform/amd64/vmunix.mk`（`--fat-size-mib $(AMD64_NATIVE_BOOT_MIB)`、注記）、`platform/amd64/tools/check-amd64-native-image.py`（4 つの partition、FAT32 と label、ESP は `/EFI` だけ、boot の FAT の 3 file）
- `src/kern/vfs.c`（slot 0 の公開、`/boot/efi`、`/boot/NAME` の bind を `vfs_publish_boot_directory()` に切り出し）
- 文書: `docs/release/zedbsd-1.0.0-beta2-guide.md`（「The ZEDBSD partition」の節と dmesg の写し方（`sudo tee`、FAT は root の 0755）、`i915.gen9=off` の手順を ZEDBSD に）、`docs/reference/kernel-boot-parameters.md`、`docs/howto/build-from-source.md`、`docs/howto/boot-and-storage.md`
- 道具の追従: `plan/tools/ufs/root-crash.sh`（root の offset 65 → 321 MiB）、`plan/tools/hw5330/README.md`、`plan/ws075/demo/build-demo-image.sh` の注記

## 確認

| 確認 | 結果 |
| --- | --- |
| `make build/zedimage-host`（`cc -Wall -Wextra -Werror`） | warning 0 |
| `make -j16 build/amd64/vmunix` | warning 0 |
| 合成の入力（乱数の vmunix・BOOTX64.EFI・logo、8 MiB の root、4 MiB の swap）で `zedimage-host disk --layout native --fat-size-mib 256` → `check-amd64-native-image.py` | OK。`fdisk -l`: 1 EFI System 64M、2 Microsoft basic data 256M、3 FreeBSD UFS、4 FreeBSD swap。BPB: ESP は label `ESP`、boot は label `ZEDBSD`・FAT32・hidden 133120、serial は別。`fsck.fat -n` が両方とも誤り 0 |
| 本物の image の build（CI の config） | **未実施**（P は disk-image を流さない。T1 が作る） |
| QEMU・5330 | **未実施**（下の手順） |

## T1 への試験の手順（QEMU、UEFI）

image: `agent/p1-ws238` の commit の tree で `config/ci/config-amd64.mk` の image（`ZEDBSD_VARIANT=native`）。build の中で `check-amd64-native-image.py` が通ること（通らなければ image ができない）。

1. **起動**: `plan/tools/boot-test.sh IMAGE`（OVMF、UEFI の USB）で login prompt。PNG。合格: login prompt（loader が FAT32 から zedbsd.cfg・vmunix・logo を読めた）。
2. **boot0 と efi**（guest で、SSH か serial）: `mount` に `/boot/boot0`（FAT）と `/boot/efi`（FAT）がある。`ls /boot/boot0` が `vmunix zedbsd.cfg logo.ppm`、`ls /boot/efi` が `EFI`。`sysctl kern.boot.firmware_partition` と `kern.boot.config_partition` が違う PARTUUID。kernel の log に `boot0 (BOOT) published at /boot/boot0` と `ESP published at /boot/efi`。
3. **FAT32 への書き込み**: `dmesg | sudo tee /boot/boot0/dmesg.txt > /dev/null; sync`（root でなら `dmesg > /boot/boot0/dmesg.txt; sync`）、guest を止め、host で image の partition 2 を `mtype -i IMAGE@@$((133120*512)) ::/dmesg.txt | head` で読める、`dd` で partition 2 を切り出して `fsck.fat -n` が誤り 0（Windows 相当の確かめ）。`minfo` で label `ZEDBSD`。
4. **ESP の中**: host で `mdir -i IMAGE@@1048576 ::/` が `EFI` だけ。
5. **回帰**: smoke の AAT（`tests/suites/smoke.suite`）。合格: 既存の基準。
6. （任意）ESP が boot0 の場合の確かめ: hybrid か uefi の layout ではなく、ESP に zedbsd.cfg を置いた古い native の image（main 732dd30e0 以前の image）を新しい kernel で起動できれば、`/boot/boot0` と `/boot/efi` が同じ FAT（`ls` が同じ、片方で作った file が他方に見える）。古い image が無ければ省いてよい。

## 5330 の素の起動（ユーザー）

1. T1 の image（または release の image）を USB に書き、5330 を F12 で USB の UEFI の entry から起動。合格: Kei の logo → desktop（今までと同じ）。
2. Terminal で `dmesg | sudo tee /boot/boot0/dmesg.txt > /dev/null; sync`、Shut Down。
3. USB を Windows（か macOS）の機械に挿し、`ZEDBSD` の drive が自動で出て `dmesg.txt` が開ける。`zedbsd.cfg` も見える。
4. （任意）`zedbsd.cfg` に `i915.gen9=off` 等を Windows で足して起動し、反映される。

## Resume point

T1 の QEMU の結果と 5330 の結果を待つ。FAIL なら: loader が FAT32 を見つけない → loader の画面の `A64 CFG` の行と OVMF の `map -r`（UEFI shell）で FAT32 の SimpleFileSystem の有無を見る。
