<!-- awesome-plan project=zedbsd record=ws274-p002 -->

# ws274-p002 S3 匿名 page の群（J・K・L・M）と reclaim-internal.h

- 親: [ws274](../ws.md)、設計: [design.md](../design.md) §1.1 の J〜M、§2 の reclaim-internal.h、§3 の S3
- Status: cleared（2026-10-19 Q1 の判定、T1-737・738・740: amd64 の uefi-usb・nvme、pcat の bios-ide、pc98、sun4u、riscv32・m68k-virt・ppc32・ppc64・mips32・mips64 の VFS の段、rpi4 の login が PASS。QEMU だけ）
- 規約: p001 と同じ「配置の変更だけの例外」（[ws106-relocation.md](../../standards/ws106-relocation.md) の考え）。関数の本文・評価順・lock の順は変えない。

## 行った事（worktree `/home/awe/zedBSD-worktrees/p1`、agent/p1、commit 2e5d05d9a、親 4660a2fb5 = main 8c15a82e1 の merge）

| file | 中身 |
| --- | --- |
| vm/private-page.c | J: vm_private_page_init〜vm_private_page_in_owned、vm_private_page_alloc、private_page_advance_locked・wait_sequence(_interruptible)・vm_private_page_wait_idle_cancelable |
| vm/page-track.c | K: vm_page_note_in、vm_page_track・untrack・replace_private・share_private |
| vm/reclaim.c | L: vm_reclaim_init・one・frame・frame_private・note_fault・get_stats・drain_swap_source(_cancelable)・private_one と static helper（mapping_prot・queue・backing_*・swap_out・discard・reclaim_backing_owned など）。変数 page_queue・page_queue_tail・swapped_queue（static）、reclaim_lock、vm_reclaim_counters |
| vm/pageout.c | M: vm_reclaim_wait_free・vm_free_bytes_estimate・vm_free_pages(_exact)・vm_free_min_pages・pageout_kick・pageout_worker・vm_oom_kill。変数 pageout_lock・pageout_waitq・free_waitq・pageout_started・pageout_requested、free_snapshot_*（static） |
| vm/reclaim-internal.h | 外に出した物の宣言だけ。定数 PAGE_SIZE・VM_RECLAIM_SCAN_MAX・VM_FREE_*・VM_PAGE_TRACKED |

- 外に出した（static を外した）物: 関数 queue_insert・queue_remove・swapped_insert・private_page_advance_locked・vm_private_page_wait_idle_cancelable・reclaim_object_fault_reserve・vm_free_pages・vm_free_min_pages・pageout_kick・**vm_oom_kill**、変数 pageout_lock・pageout_waitq・free_waitq・**pageout_started・pageout_requested**・reclaim_lock。
  - design.md §2 との違い: vm_oom_kill（vm_reclaim_frame が呼ぶ）と pageout_started・pageout_requested・pageout_waitq（vm_reclaim_init が初期化し pageout.c も使う）を足した。page_queue・page_queue_tail・swapped_queue は reclaim.c だけが使うので static のまま。
- vm_object_reclaim_clean・vm_object_reclaim_one は design.md §2 どおり E（object-cache）として vm.c に残した。alloc_vm_page は object 側だけが使うので vm.c に残した。
- `stats` の macro は page-track.c・reclaim.c・pageout.c に置いた（vm.c からは外した）。weak extern は cache_memory_reclaim だけを reclaim.c に写した（vm.c にも残る）。VM_OBJECT_FAULT_RESERVE_PAGES・VM_OOM_WAIT_SECONDS・VM_PRIVATE_FRAME_ATTEMPTS は reclaim.c だけ。
- vm_reclaim_init の本文（初期化の順）は変えていない。
- 注記だけの変更: vm.c の冒頭の注記、VM_OBJECT_FAULT_WAITS の注記（「such waits」→「VM_FREE_WAIT_TICKS waits」）、header の変数の注記の「above/below」の指す先。区切りの見出し `/* Page */`・`/* Private */` は file が分かれたので落とした。
- Makefile は KERN_VM_SOURCES に 4 行を足しただけ。

## 確かめ（host）

- build: `make -j16 ZEDBSD_CONFIG=<config> BUILD=build/ws274-s3-<p> vmunix`、config/ci/config-amd64.mk・config/ci/config-pcat.mk・plan/tools/ports/config-sun4u.mk。3 つとも exit 0、warning 0。
- 照合: `python3 -I plan/ws274/tests/body-compare.py cd04d7e82 src/kern/vm/vm.c -- src/kern/vm/{vm,private-page,page-track,reclaim,pageout}.c` → functions 194 names 194 differences 0、PASS。
- nm（`build/llvm/bin/llvm-nm`、大文字の型）: amd64 56・pcat 58（LTO、p001 と同じ）。sun4u 1442 → 1458（+16）。増えた 16 は上の外に出した 10 関数と 6 変数で全部ある。旧 vm.c を同じ gcc の flag で compile した vm.o は 89、新しい 5 つの .o の合計は 105（= 89 + 16）。
- `sh plan/ws213/tests/kd-host-test.sh` PASS、`sh plan/ws213/tests/pin-host-test.sh` PASS。
- 未実施: 残りの platform の build（p007）、QEMU の起動（下の T1 の依頼）。

## T1 への依頼（WS274 p002）

worktree agent/p1 の 2e5d05d9a（または Q1 が merge した main）で、振る舞いを変えない VM の file の分割の後の起動の回帰。p001 の依頼（T1-736）とまとめて 1 つの QEMU で流してよい。

1. `plan/tools/boot-test.sh` の uefi-usb（amd64、8 GiB）と bios-ide（pcat）で login prompt。
2. `plan/tools/pc98-boot.py` で pc98 の起動。
3. `plan/tools/ports/` の sun4u（sparcv9、gcc で LTO 無し）の起動の回帰。
- 任意: `plan/tools/process/guest-vfork.sh`（fork の COW）。swap を使う試験があれば reclaim・page-out の経路の確かめになる。
- 合格: どれも login prompt（または各道具の PASS）。PNG を返す。

## 残り

- 自分の build の出力 build/ws274-s3-{amd64,pcat,sun4u}/ と build/ws274-s3-*.log は Q1 が消してよい。
