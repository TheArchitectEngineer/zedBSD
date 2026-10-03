<!-- awesome-plan project=zedbsd record=ws073p015 -->

# ws073-p015: boot の slot を /boot/boot0〜3 に自動で mount する（最終の layout。p009・p014 の公開を置き換える）

Status: cleared（2026-09-27）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-073](../../bugs/BUG-073.md)（この Phase の試験で発見し、この Phase で修正）

## ユーザーの判断（2026-09-27、原文）

「amd64 UEFIでも、/boot/boot0みたいなマウントは自動でやりましょう。ESPはfstabです。スワップだけでもマウントします。rootfs.imgは読み込み専用なので、書き込みできなくても、読み込めていいと思います。」

## 意味（main の整理）

- 全ての platform（amd64 UEFI を含む）で、`bootN:` の file（overlay-root、overlay-data、または swapN だけでも）が参照する boot の slot を
  `/boot/boot0`〜`/boot/boot3` に自動で read-write に mount する。
- 直接の selector（`rootpart=`、UUID・PARTUUID）でだけ使う slot は mount しない。
- `/boot` はただの directory。ESP は fstab だけで `/boot/esp`。
- 使用中の file（rootfs.img・data.img・swapfile）は読めるが、書き込み・truncate・削除・改名は backing-claim の registry が断る。
- p009 の `/boot`・`/boot/esp` の公開と p014 の「amd64 は公開しない」を置き換える（p014 の fstab の ESP の adoption はそのまま使う）。

## 設計

- `vfs_publish_boot_filesystems(parameters)`（`src/kern/vfs.c`）: `overlay-root`・`overlay-data`・`swap0`〜`swap3` の値を
  `kern_boot_source_reference_parse()` で読み、`bootN:` が名指す slot に印を付ける。自分の private な mount を持つ全ての slot を
  `mount_private_allow_adoption()`（p014）で fstab に adopt させ、印のある slot だけを `vfs_publish_boot_slot()` で `/boot/bootN` に bind する
  （`/boot` と `/boot/bootN` は無ければ root に作る。失敗は log だけで起動は続く）。platform ごとの分岐（p014 の `#if defined(HAL_ARCH_AMD64)`）は無くした。
- 起動の後の swap の追加: `swapon bootN:PATH` が成功すると、その slot も「boot の file が使う slot」になるので `/boot/bootN` に出す。
  swap control の resolver に任意の `source_added` を足し（`include/kern/swap-control.h`）、`kern_swap_control_add()` が成功の後に
  control の単一の操作の中で呼ぶ（`src/kern/swap.c`）。vfs は `vfs_swap_source_added()` で同じ publication を行う。
- 各 slot は 1 度だけ出す（`vfs_boot_slot_published[]`）。`umount /boot/bootN` は見せるのをやめるだけで、kernel の mount は残る（再起動で戻る）。
- log: `vfs: boot0 (BOOT) published at /boot/boot0`（ESP なら `(ESP)`。partition の型で判定、p009 の `partition_disk_is_efi_system()`）。
- legacy ARM overlay（rpi4、slot の外の boot の FAT）: その FAT を boot0 として `/boot/boot0` に出す（下の可逆な既定）。
- `mount.c` の説明の comment を新しい layout に合わせた。HAL は触れていない。

## 可逆な既定（判断が要る点として記録）

1. **ESP 自身が `bootN:` で参照される slot のとき**: `/boot/bootN` に出し（他の slot と同じ扱い）、fstab の `/boot/esp` の行は p014 の adoption で
   同じ FAT の状態の別の view になる。ESP だけを特別に隠す案に戻すなら `vfs_publish_boot_filesystems()` で `partition_disk_is_efi_system()` の slot を飛ばす。
2. **root に promote された slot**（`rootpart=` が boot の FAT を root にする）: 自分の private な mount を持たないので出さない（root の中に root を bind しない）。
3. **legacy ARM overlay の boot の FAT**: `/boot/boot0` に出す（以前は `/boot`）。rpi4 の試験は amd64 だけの方針で未実施。
4. **起動の後の `swapon bootN:PATH`**: 成功した時点でその slot を出す。失敗した swapon では出さない。

## BUG-073（この Phase で発見・修正）

- 症状: native の ESP（FAT32、512 B cluster）で `swapon boot0:swapfile` の後、同じ FAT への file の作成が EBUSY になり、以後その FAT の
  ls・cat・mkdir・swapoff が再起動まで EBUSY（FAT の engine の 1 sector の cache が書けずに残り、後の全ての操作がそれを先に書こうとする）。
  ws073-p014 の 300 秒の timeout もこれと見られる。
- 原因: buffer cache の line（4 KiB）が 4 KiB 未満の cluster を複数持ち、claim された file（swap・loop）の block と隣の file の block が
  同じ line に入る。line の書き戻し（`writeback_line()`、`src/kern/buf.c`）は line 全体を書くので、registry が「他の claim の範囲」として断る。
- 修正: `backing_claim_find_extent_owner()`（`src/kern/backing-claim.c`、header `include/kern/backing-claim.h`）で書き手の claim 以外の claim が
  line に触れるか調べ、触れるときは `writeback_line_runs()` で claim の外の block の連なりだけを、連なりごとに filesystem の guard を取って書く。
  claim の block は所有者が device に直接書くので、line の写しは書かない。書いた後その line は VALID を外し、次の読み手が読み直す。
  registry は書く全ての block を従来どおり判定するので、claim の file の block を他者が書けないという保護は弱めていない。触れないときは従来の
  `writeback_line_whole()`（同じ処理を関数に分けただけ）。

## 検証（QEMU、KVM、NVMe、amd64 だけ。実機は未実施）

- build: `plan/ws035/tests/config-amd64-guest.mk` の vmunix（`-Werror`）、lean native（`plan/ws045/tests/config-amd64-base.mk`）と hybrid
  （[tests/config-amd64-hybrid-serial.mk](../tests/config-amd64-hybrid-serial.mk)、serial の mirror）の disk-image。
- 規約: `tests/style-diff.py`（vfs.c・swap.c・mount.c・buf.c・backing-claim.c・swap-control.h・backing-claim.h）変えた行の finding 0。
- native（[tests/p015-native.sh](../tests/p015-native.sh): main の測定用 image の vmunix を差し替え、2 つ目の NVMe に 512 B・2 KiB・4 KiB cluster の
  FAT32 を 3 つ）:
  - [tests/boot-slots.sh](../tests/boot-slots.sh) `native` 26 件 PASS: 起動時に `/boot` は mount でない directory、`/boot/esp`・`/boot/boot0`〜`3` に何も無い
    （native は `rootpart=`・raw の swap で `bootN:` が無い）。ESP を `mount -t msdosfs` で `/boot/esp` に（adoption）、16 MiB の file・mkswap・
    `swapon boot0:swapfile` で `/boot/boot0` が現れ、`/boot/esp` と同じ FAT。swapfile は読めるが、追記・その場の書き込み・truncate・rm・mv・他の file の
    mv での上書きが EBUSY（`/boot/esp` 経由でも）、大きさは変わらない。隣の file は書ける。swapoff の後は rm できる。
  - [tests/fat-claim.sh](../tests/fat-claim.sh) を 3 つの FAT で、それぞれ全 PASS（failures 0）: 有効な swap file の隣で file の作成・書き込み（1 MiB）・
    読み戻し・mkdir・rename・ls・rm・sync、swap file 自身の保護（上と同じ 5 種の拒否）、swapoff、rm、umount。
  - UFS（buffer cache は共有）: root で `dir-grow.sh`（LONG=300 SHORT=600 MOVE=100 GONE=300）の make と verify が `VERIFY-OK`、削除。
  - host の `fsck.fat -n`: 3 つの FAT とも clean。
- hybrid（[tests/p015-hybrid.sh](../tests/p015-hybrid.sh): lean の hybrid image を serial で操作、BOOT は 2 KiB cluster）: `boot-slots.sh hybrid` 36 件 PASS:
  `/boot/boot0` が rw で出て、`/boot/boot1`〜`3`・`/boot/esp` は無い。rootfs.img・data.img・swapfile はそれぞれ最初の page が読め、追記・その場の書き込み・
  truncate・rm・mv・上書きの mv が EBUSY で大きさは不変。BOOT に新しい file を作って消せる。ESP（kernel は持たない）は `mount -t msdosfs` で普通に
  `/boot/esp` に。停止の後の host の `fsck.fat -n` で BOOT は clean。
- boot test（`plan/tools/boot-test.sh`、UEFI・NVMe）: lean native PASS（`build/ws073-p015/boot-native/login.png`）、hybrid PASS
  （`build/ws073-p015/boot-hybrid/login.png`、画面に `vfs: boot0 (BOOT) published at /boot/boot0`）。
- 未実施: pcat・pc98・rpi4（試験は amd64 だけの方針）、実機。

## わかったこと・残り

- ESP（512 B cluster）への 16 MiB の `dd` に約 30 秒（BUG-073 の ticket に別の性能の問題として記録。未調査）。
- 試験の途中で別の bug を 2 つ見つけた（どちらも claim と無関係で、この Phase の変更の前からある）:
  - [BUG-074](../../bugs/BUG-074.md): FAT で `rm -r DIR` が 1 つおきに entry を飛ばし "Directory not empty"（readdir の位置）。次の Phase で扱う。
    `fat-claim.sh` は file を名指しで消して避けている。
  - [BUG-075](../../bugs/BUG-075.md): FAT で読んだ後に消した file の cluster が rm・sync の後も on-disk で解放されず、強制終了の後の fsck が回収する。
    `boot-slots.sh`・`p015-hybrid.sh` は読んだ file を消さない形で避けている。
- 残る制限: swap の prepare の `buf_invalidate(DISCARD)` は claim の extent に一部だけ重なる line も捨てる。prepare の直前に mount を sync するので
  通常は dirty でないが、その間に隣の file が書かれると失われうる（未確認。BUG-073 の ticket に記録）。
