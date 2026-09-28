<!-- awesome-plan project=zedbsd record=ws073p020 -->

# ws073-p020: 読んだ後に消した file の storage が cache の追い出しまで残る（BUG-075）、sync と終了の競合（BUG-082）

Status: cleared（2026-09-28。1 回目の試行は uncleared（wrap up、BUG-082 の hang が残った）。2 回目の試行で BUG-082 の hang を修正し、最後の kernel で BUG-075 を再試験）
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
- （1 回目の試行の時点で未実施）`inode_unnamed` を加えた kernel での bug075.sh の再試験は、2 回目の試行で BUG-082 の修正を含む最後の kernel で行った（下）。
- boot test: 最後の kernel（BUG-075 だけ）を full guest image に入れて `plan/tools/boot-test.sh` PASS。

## BUG-082（1 回目の試行の記録、2026-09-28 の wrap up の時点。wip.patch は 2 回目の試行で source に入れ、patch の file は削除）

- 再現: [tests/bug082.sh](../tests/bug082.sh)（[tests/sync-exit.sh](../tests/sync-exit.sh): 4 つの loop が短い process を 300 回ずつ、別の loop が sync）。
  修正前の kernel は 1 回目で `fatal: src/kern/vmspace.c:742: invalid shared VM reverse mapping`。
- patch の中身: revoke が終わりかけの space（tryref の失敗）を yield して待つ。copy-on-write の fault が予約した mapping（BUSY）は fault の
  wait queue で待つ（`vmspace.c:768` の 2 つ目の fatal も同じ競合で出た）。その fault は page を writeback が持つとき予約を返してから pin で
  待つ（`vm_object_page_pin_read_nowait`・`vm_object_page_pin_wait`）。revoke の最後の vmspace の参照は reaper に渡す（`vmspace_put_deferred`）。
- 結果: fatal は消えた。ただし 30 回中 27 回 PASS の後、28 回目で sync と cat が止まる（hang、guest は生きている）。原因は未特定。
- 再開の手順: `git apply plan/bugs/BUG-082-wip.patch`、guest の vmunix を build、`sh plan/ws073/tests/bug082.sh build/ws073-guest/vmunix 30`。
  hang で guest は残るので、`-g` の process.c・task.c の型で `all_processes` から sync・cat の thread の `resume_rsp` を読み、stack の
  return address を並べて待ち合いを特定する（phase007 の方法）。直った後に bug075.sh を再実行して p020 を閉じる。

## 2 回目の試行（2026-09-28、WS073 のサブエージェント）: BUG-082 の hang

### 解析（QEMU の gdbstub。serial・console の log は読んでいない）

- wip.patch を当てた kernel で `tests/bug082.sh` の 1 回目に hang（sync と cat が止まり、4 つの vCPU は全て `sched_idle`）。`-g` で compile した
  process.c・task.c・vm.c・vmspace.c の型で、`all_processes` の thread の `resume_rsp` から stack を並べ、`shared_objects` の page を調べた。
- 待ち合い 1（i_io_lock と fill の BUSY の逆順）: cat は `vm_object_read_coherent_useful` → `object_cache_read_missing` で読みの run の page を
  BUSY（WRITEBACK なし）で公開し、backend を読むため `file_io_begin`（`FILE_IO_VM_OBJECT`）→ `file_regular_io_lock` で `i_io_lock` を待つ。
  sync は `vm_object_sync_range_buffer` で同じ inode の `i_io_lock` を持ったまま、その BUSY の page を `page_waitq` で待つ。fault の fill
  （`vm_object_fault`）も同じ順（BUSY → i_io_lock）で、content と resize の prepare は i_io_lock を外して page を待つ設計。普通の sync だけが
  i_io_lock を持ったまま fill を待っていた。
- 修正 1（`src/kern/vm.c`）: i_io_lock を持つ sync の pass は、BUSY で WRITEBACK の無い page（読みの fill の途中）を待たずに飛ばす。fill が公開する
  まで page は clean で mapping も無く、この pass が revoke・書き出すものは無い。i_io_lock を持たない resize の prepare は従来どおり待つ。
- 修正 1 の後、62 回目で別の hang: fork の子（`sh`）が CPU を使い続ける。`user_fault_probe` は同じ pid の fault が約 7 億回、`rtld_memset` の
  store で address 0xa38。子の register は page fault の frame（RFLAGS.RF=1、rip は memset の store、rcx=0xa38、rdi=0x10000c5c8）で、rax だけが
  0（fork の子の返り値）、stack の中身はその位置に合わない。fork が別の thread の #PF の frame を子へ写した。
- 待ち合い 2 の原因（`src/hal/amd64/task.c`）: macro `running_task` が `amd64_percpu_current()->running_task` の 2 回の load で、その間に preempt
  されて別の CPU へ移ると、前の CPU の task（他の thread）を読む。`hal_task_get_current()` はこの競合を 1 回の GS 相対の load で避けていたが、
  task.c の中の `hal_task_fork_current()`（`active_user_frame`・`tls`・fxsave の先）、signal の enter・return、exec の in-place などは 2 回の load のままだった。
- 修正 2（HAL の実装、hal.h は不変）: task.c の読みを全て `current_task`（`hal_task_get_current()`、1 回の `%gs:0xf8` の load）に、running task を
  切り替える 2 か所（初期 task と context switch、どちらも割り込み禁止）は field に直接書く。fork の読みが `movq %gs:0xf8` になったことを disassembly で確認。
- wip.patch（終わりかけの space を yield で待つ、BUSY の mapping を fault の wait queue で待つ、COW の fault は writeback の page を予約を返して pin で待つ、
  revoke の最後の vmspace の参照を reaper へ）はそのまま source に入れ、規約に合わせて 2 か所を直した（`waiting = pin() == 0` を if に、変数名 `unused`）。

### 検証（QEMU、amd64 native の full guest、KVM、4 vCPU、NVMe。実機は未実施）

- build: guest の vmunix（`-Werror`、warning 0、kernel include check・amd64 vmunix check PASS）。規約: `tests/style-diff.py`（vm.c・vmspace.c・
  vm-object.h・hal/amd64/task.c）0、diff の空白の検査 0。
- `tests/bug082.sh`（`sync-exit.sh 4 300` を繰り返す）: wip.patch だけ → 1 回目で hang。修正 1 → 61 回 PASS の後 62 回目で hang（fork の frame）。
  修正 1＋2（最後の kernel）→ **300/300 PASS**（fatal・hang なし、1 回 2〜6 秒）。
- `tests/bug075.sh`（最後の kernel）: 14 件全て PASS。FAT 512 B・4 KiB、UFS、tmpfs の「読んだ・読まない file の rm で戻る」「開いたまま消した file を
  読める」「close の後に戻る」と、強制終了の後の host の `fsck.fat -n` 2 つが clean。
- boot test: 最後の kernel を full guest image（`tests/kernel-image.sh`）に入れて `plan/tools/boot-test.sh` PASS（login prompt の画面）。

### 残り（別の bug の候補、main に ID を依頼）

- 同期の fault の signal（SIGSEGV 等）が block されていると、kernel は pending にするだけで同じ命令へ戻り、process は fault を永久に繰り返す
  （上の子は 7 億回）。POSIX では未定義だが、既定の動作で終わらせる（Linux と同じ）のが安全（`src/kern/user-probe.c` の `kernel_user_fault_handler`）。
  今回の hang の原因ではない（原因は fork の frame）が、症状を spin にした。未修正。
- `src/hal/amd64/space.c` の `AMD64_CURRENT_SPACE` も 2 回の load。使う所（TLB の shootdown の送り手）が preempt されうるかは未確認。
