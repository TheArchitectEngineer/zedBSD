<!-- awesome-plan project=zedbsd record=ws073p020 -->

# ws073-p020: 読んだ後に消した file の storage が cache の追い出しまで残る（BUG-075）、sync と終了の競合（BUG-082、途中）

Status: uncleared（2026-09-28、wrap up。BUG-075 の修正は commit、最後の kernel での再試験と BUG-082 は未了）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-075](../../bugs/BUG-075.md)、[BUG-082](../../bugs/BUG-082.md)

## 原因（BUG-075）

`read()` の page cache の object（cache_only、`src/kern/vm.c`）は同じ inode に自分の読みの handle を開いて持つ。unlink で inode が DEAD に
なっても、その handle が inode を保ち、FAT の orphan の chain・UFS の block は cache の object が追い出されるか unmount まで解放されない。
FAT だけでなく UFS・tmpfs も同じ（試験で確認: 読んだ 1 MiB の file の rm・sync の後も df の使用量が戻らない）。

## 修正（BUG-075、commit 済み）

- `vm_object_cache_discard_inode(inode)`（vm.c、`include/kern/vm-object.h`）: inode の cache だけの object が idle（registry の参照だけ、
  mapping・操作・待ち・dirty・busy・失敗・遷移なし、unmount 中の mount でない）なら registry から外して捨てる。idle の判定は
  `object_cache_idle_locked()` に切り出し、`object_cache_evict_one()` と共有。
- 呼ぶ所: 汎用の `inode_unlink_locked()`（inode.c、最後の名前を失う通常の file、fs の unlink の前）と FAT の `fat_release_orphan()`
  （unlink・rmdir・rename の置き換え）。どちらも weak 参照（VM の無い kernel）。
- `inode_unnamed()`: DEAD の inode には任意の cache を作らない（`vm_object_cache_prepare()`）、最後の mapping が離れる object を cache に
  入れない（`vm_object_put()`）。開いたまま消された file を fd で読んでも、close の後に storage が戻るため。link 数は FAT が保たないので DEAD だけを見る。

## 検証（QEMU、amd64 native の full guest、KVM。実機は未実施）

- build: guest の vmunix（`-Werror`、warning 0）。規約: `tests/style-diff.py`（vm.c・inode.c・fat.c・vm-object.h）0。
- [tests/bug075.sh](../tests/bug075.sh)（[tests/unlink-read.sh](../tests/unlink-read.sh)）、`inode_unnamed` の前の kernel: FAT32 の 512 B・4 KiB cluster で
  6 件とも PASS（読んだ file・読まない file の rm・sync で使用量が戻る、開いたまま消した file を fd で読める、close の後に戻る）、
  強制終了（unmount なし）の後の host の `fsck.fat -n` が 2 つとも clean。UFS・tmpfs は rm・sync で戻る、fd で読める は PASS、
  「close の後に戻る」は FAIL（fd の読みが新しい cache を作った）→ `inode_unnamed` を加えた。
- 未実施: `inode_unnamed` を加えた最後の kernel での bug075.sh の再試験（1 回目の UFS の試験は BUG-082 の panic、以後は BUG-082 の試験を優先）。
- boot test: 最後の kernel（BUG-075 だけ）を full guest image に入れて `plan/tools/boot-test.sh` PASS。

## BUG-082（未 commit、`plan/bugs/BUG-082-wip.patch`）

- 再現: [tests/bug082.sh](../tests/bug082.sh)（[tests/sync-exit.sh](../tests/sync-exit.sh): 4 つの loop が短い process を 300 回ずつ、別の loop が sync）。
  修正前の kernel は 1 回目で `fatal: src/kern/vmspace.c:742: invalid shared VM reverse mapping`。
- patch の中身: revoke が終わりかけの space（tryref の失敗）を yield して待つ。copy-on-write の fault が予約した mapping（BUSY）は fault の
  wait queue で待つ（`vmspace.c:768` の 2 つ目の fatal も同じ競合で出た）。その fault は page を writeback が持つとき予約を返してから pin で
  待つ（`vm_object_page_pin_read_nowait`・`vm_object_page_pin_wait`）。revoke の最後の vmspace の参照は reaper に渡す（`vmspace_put_deferred`）。
- 結果: fatal は消えた。ただし 30 回中 27 回 PASS の後、28 回目で sync と cat が止まる（hang、guest は生きている）。原因は未特定。
- 再開の手順: `git apply plan/bugs/BUG-082-wip.patch`、guest の vmunix を build、`sh plan/ws073/tests/bug082.sh build/ws073-guest/vmunix 30`。
  hang で guest は残るので、`-g` の process.c・task.c の型で `all_processes` から sync・cat の thread の `resume_rsp` を読み、stack の
  return address を並べて待ち合いを特定する（phase007 の方法）。直った後に bug075.sh を再実行して p020 を閉じる。
