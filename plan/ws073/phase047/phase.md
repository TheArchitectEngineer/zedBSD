<!-- awesome-plan project=zedbsd record=ws073-p047 -->
# ws073-p047: BUG-052 — tmpfs の容量を物理 memory の半分に（file の data を page の index で持つ）

Status: in-progress（q651-i01、P1 generation12、2026-10-04。実装・build・host 試験済み、QEMU の試験を T1 に依頼）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-052](../../bugs/BUG-052.md)
Queue: q651（2026-10-04 user「P1、P2はBUG-143, 053, 052, 162, 103, 129, 033, 157, 139を修正します。直す順序は任せます。」）

## 範囲

tmpfs の byte の容量が mount ごとに 32 MiB 固定（node の部分は ws073-p013 で直した）。ticket の再開の条件（09-27）: 容量を物理 memory の半分に
すると、tmpfs の page（`kern_calloc` の 4112 byte = 固定 heap を越えると 2 page の large allocation）の解放が `kern_free` の large allocation の
list の線形の探索で 2 乗になる。page の list も整列した linked list で、1 page の lookup が O(file の page 数)。

## 設計（2026-10-04、P1 の技術判断）

- **file の data を物理 page で持ち、page の番号から radix tree で引く**（新しい `src/kern/tmpfs-pages.c`・`.h`）。tree の table も 1 page
  （amd64 で 512 entry、i386 で 1024 entry）。entry は下の table・data の page の物理 address に present の bit。lookup は tree の高さ（amd64 の
  64 bit の offset 全体で 6 段）の step。kernel heap を使わないので、大きい file が固定 heap を埋めることも `kern_free` の list を歩くことも無い。
  使う page は 4096 byte（以前は 4112 byte の record で、heap の外では 2 page を食っていた）。
- **data の frame は `vm_reclaim_frame_private()`（新設、`src/kern/vm.c`・`include/kern/vm-reclaim.h`）で取る**: fault 用の予備を残して取り、
  足りなければ disposable cache を返すか private（anonymous）の page を 1 つ swap out してから取る。**file の page の write-back はしない**
  （write の経路は inode の `i_io_lock` を持っており、tmpfs の file を mmap した object の write-back が同じ lock を要るため、`vm_reclaim_frame()` だと
  自分の lock で止まり得る）。眠らず、取れなければ ENOMEM。
- **write は 16 page の step ごとに**: inode の `i_lock` の下で足りない page を数え、lock を外して frame を取り（quota と commit に charge、0 で埋める）、
  lock の下で copy して index に入れ、使わなかった frame を返す。write は file の層の `i_io_lock` で直列なので、step の間に他の write は入らない。
  途中で truncate が page を消した時は次の step で数え直す。
- **容量の既定は物理 memory の半分**（下限 32 MiB、Linux の既定と同じ）。page は今まで通り commit の上限に charge する。
- 制限（残り）: tmpfs の data は swap されない kernel の page のまま（Linux の tmpfs は swap される）。swap で裏打ちする設計（VM の object の page に
  する）は大きいので、この Phase では行わない。半分の上限と commit の会計で、RAM の残り半分は他の用途に残る。

## 実装

| file | 変更 |
| --- | --- |
| `src/kern/tmpfs-pages.c`・`.h`（新） | page の index: `tmpfs_pages_init`・`lookup`・`insert`（高さを伸ばし、途中の table を取る）・`truncate`（ある番号以降の data の page と空になった table を解放、解放した data の page 数を返す） |
| `src/kern/tmpfs.c` | `struct tmpfs_page` の list を `struct tmpfs_pages` に。pread は lookup、write は上の step、truncate・reclaim は `tmpfs_pages_truncate`、stat の block は `pages.count`、容量は `tmpfs_default_bytes()` |
| `src/kern/vm.c`・`include/kern/vm-reclaim.h` | `vm_reclaim_frame_private()` |
| `platform/{amd64,arm64,sparcv9,x68k,pcat,pc98}/vmunix.mk` | `tmpfs-pages.c` を kernel に加えた |
| `plan/ws073/tests/tmpfs-many.sh` | 古い期待（data の file が 32 MiB の quota で止まる）を、20000 個（80 MiB）が全部入る、に直した |

HAL・UAPI は変えていない。

## 検証

- host: `sh plan/ws073/tests/tmpfs-pages-host.sh` → `tmpfs-pages-host: PASS`、`ASAN=1`（AddressSanitizer・UBSan）も PASS。密な 3000 page・疎な番号
  （511〜2^51−1）・hole・EEXIST・table の境界と途中での truncate・0 への truncate で全 page が返る（leak 0）・空にした後の再挿入。
- build: `make -j16 BUILD=build/p1-q651/amd64 build/p1-q651/amd64/vmunix`（既定の構成）→ exit 0、warning 0、`amd64 vmunix check: PASS`。
  i386（`config/ci/config-pcat.mk`）は `tmpfs.o`・`tmpfs-pages.o`・`vm.o` を -Werror で compile して 0 error。pcat の vmunix 全体は**この変更の前から**
  `src/kern/platform/pcat.c:439` の `drv_acpi_poweroff` の暗黙の宣言で失敗する（main の 9e228b6 で再現、Q1 に報告）。
- style: 新しい file は `plan/tools/style-check.py` で 0。変えた file の変えた行は `style-diff.py` で 5 件、全て critical section（`mutex_lock` と
  `mutex_unlock` の間の 1 文）の形で、coding-style §5 の例と同じ形（checker の誤検出として残した）。
- QEMU（T1 に依頼、未実施）: [bug052-tmpfs.sh](../tests/bug052-tmpfs.sh)（guest の root で: /tmp の容量 ≥ 1 GiB、512 MiB の file の書き込み・読み戻しの
  cksum・du、途中への truncate、rm が 5 秒未満で容量が戻る、1 GiB の疎な file）と [tmpfs-many.sh](../tests/tmpfs-many.sh)（BUG-029 の回帰）、boot test。
- 実機: 未実施。

## 残り

- T1 の結果で Q1 が判定する。
- tmpfs の data の swap での裏打ち（上の制限）。必要なら別の Phase。

## T1-048（2026-10-04、b65fd40 の既定の image、QEMU）と試験の直し

boot-test PASS、`bug052-tmpfs.sh` PASS（容量 4191060 KiB、512 MiB を 1 s で書き 0 s で消す）。`tmpfs-many.sh` は FAIL ×2（同じ）: 空 file 10000 ×3 は
PASS、1 byte の file が 14325 個で ENOSPC（`FAIL 20000 data files fit`）。14325 は ws073-p013 の node の上限（全 tmpfs で inode cache の 7/8、約 14300）で、
設計どおり。誤りは私が p047 で直した試験の期待（20000 個が入る）の方。→ `tmpfs-many.sh` を「旧 32 MiB の quota（約 8000 個）より多く入り（9000 超）、
止めたのは node の上限で byte は 100% でない、他の file は開ける」に直した。kernel は不変。再試験を T1 に依頼。
