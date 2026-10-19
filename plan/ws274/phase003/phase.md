<!-- awesome-plan project=zedbsd record=ws274-p003 -->

# ws274-p003 S4 object の群（A〜G）と object-internal.h

- 親: [ws274](../ws.md)、設計: [design.md](../design.md) §1.1 の A〜G、§1.3、§2 の object-internal.h、§3 の S4
- Status: cleared（2026-10-19 Q1 の判定、T1-737・738・740: amd64 の uefi-usb・nvme、pcat の bios-ide、pc98、sun4u、riscv32・m68k-virt・ppc32・ppc64・mips32・mips64 の VFS の段、rpi4 の login が PASS。QEMU だけ）
- 規約: p001・p002 と同じ「配置の変更だけの例外」（[ws106-relocation.md](../../standards/ws106-relocation.md) の考え）。関数の本文・評価順・lock の順は変えない。

## 行った事（worktree `/home/awe/zedBSD-worktrees/p1`、agent/p1、commit 37ecab242、親 92fd627a2 = p002 の S3）

S4a・S4b に分けず 1 commit にした（機械的な分割で、各段の確かめが 1 回で済むため）。vm.c は `git mv` で object.c にし、残りの 6 file を新しく作った。vm.c は無くなった。

| file | 中身 |
| --- | --- |
| vm/object.c | A: get_shared・backing_busy・create_anonymous・ref・put、get_shared_internal、registry_lock・unlock、find_object_by_inode_locked、registry の wait・wake、operation_begin・end、next_*generation、can_destroy・unlink_object_locked・hash_index・destroy_object・retain_object、object_reference_locked・cache_retainable・inode_unnamed。変数 7 つ（global）と object_registry_lock（static） |
| vm/object-content.c | B: inode_io_wait・inode_resize_active・content_read_begin/end・cache_published・content_begin/prepare/commit/abort・read_coherent(_useful)・resize_*、object_initialize_eof_locked・object_wait_resize・content_release_pages_locked・content_finish・resize_finish |
| vm/object-fault.c | C: vm_object_fault・page_pin*・unpin・fault_release/resident・mapping_add/remove(_locked)・mark_dirty、page_pin_copy・range_has_wired_mapping |
| vm/object-sync.c | D: sync_range・sync_inode(_buffer)・truncate_inode・writeback_prepare/release・content_prepare_delayed・sync_mount_buffer、record_writeback_error・write_page_data・sync_range_internal/buffer・page_dirty_link/unlink/mark・sync_page_at_or_after・write_dirty_pages |
| vm/object-cache.c | E: count 類・cache_prepare/pin/unpin/reclaim_pinned/drain・discard_mount*・reclaim_clean・reclaim_one・cache_discard_inode と helper（cache_read_missing ほか） |
| vm/object-prefetch.c | F: prefetch_prepare/complete/abort・prefetch_consume/retire |
| vm/object-page.c | G: alloc_vm_page（static のまま）・find_page・unlink_page_locked・page_overlaps・dirty/busy の判定・clear_page_dirty_locked・free_object_page・memory_reserve・slab・descriptor・alloc_object_page・release_object_page_storage・AVL の索引。struct vm_page_slab・page_slabs・page_slab_lock（static） |
| vm/object-internal.h | 定数 PAGE_SIZE・VM_OBJECT_*、変数 7 の extern、外に出した 46 関数の宣言、lock の注記 |

- 外に出した（static を外した）関数は 46（design.md の「約 45」）: 一覧は object-internal.h。vmspace.c の static の find_page・alloc_vm_page とは、vmspace.c が object-internal.h を include しないので衝突しない（名前の接頭辞は判断 6-6 で S8）。
- 群の振り分けで design.md の行の範囲と違えた所: free_object_page を A でなく G に、object_wait_resize を B に。
- weak extern は file ごとに使う物だけを写した。forward 宣言は各 file に、その file の static の物だけ（元の順）。区切りの見出し `/* Object */`・`/* Reclaim */`・`/* Helpers */` は落とした。
- 注記だけの変更: reclaim-internal.h の冒頭（「vm.c」→「the object-*.c files」）。各 file の冒頭の注記は新しく書いた。
- Makefile は KERN_VM_SOURCES の vm.c の 1 行を 7 行にしただけ（main の P2 の swap の行と隣り合うので merge の時に注意）。

## 確かめ（host）

- build: `make -j16 ZEDBSD_CONFIG=<config> BUILD=build/ws274-s4-<p> vmunix`、config/ci/config-amd64.mk・config/ci/config-pcat.mk・plan/tools/ports/config-sun4u.mk。3 つとも exit 0、log の `warning:`・`error:` 0。
- 照合: `python3 -I plan/ws274/tests/body-compare.py 92fd627a2 src/kern/vm/vm.c -- src/kern/vm/object*.c` → functions 131 names 131 differences 0、PASS。
- nm（`build/llvm/bin/llvm-nm`、大文字の型）: amd64 56・pcat 58（LTO、p001・p002 と同じ）。sun4u 1458（p002）→ 1511（+53）。外に出した 46 関数＋7 変数が全部 global にあることを comm で確かめた（欠け 0）。p002 の 1458 は記録の値で、この Phase では元の版を作り直して数えていない。
- `sh plan/ws213/tests/kd-host-test.sh` PASS、`sh plan/ws213/tests/pin-host-test.sh` PASS。
- 未実施: 残りの platform の build（p007）、QEMU の起動（下の T1 の依頼）。

## T1 への依頼（WS274 p003）

worktree agent/p1 の 37ecab242（または Q1 が merge した main）で、振る舞いを変えない VM の object の群の分割の後の起動の回帰。p001・p002 の依頼とまとめて 1 つの QEMU で流してよい。

1. `plan/tools/boot-test.sh` の uefi-usb（amd64、8 GiB）と bios-ide（pcat）で login prompt。
2. `plan/tools/pc98-boot.py` で pc98 の起動。
3. `plan/tools/ports/` の sun4u（sparcv9、gcc で LTO 無し）の起動の回帰。
- 任意: `plan/tools/process/guest-vfork.sh`。file の読み書き・mmap の多い操作（login 後の shell で数個の command）があれば object の fault・sync・cache の経路の確かめになる。
- 合格: どれも login prompt（または各道具の PASS）。PNG を返す。

## 残り

- 自分の build の出力 build/ws274-s4-{amd64,pcat,sun4u}/ と build/ws274-s4-*.log は Q1 が消してよい。
