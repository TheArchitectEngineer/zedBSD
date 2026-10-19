# ws274-p007 最後の確かめと規約の全文（S9）

- 親: [WS274](../ws.md)、Queue: q1305 / q1305-i01（P1、2026-10-19）
- Status: cleared（2026-10-19 Q1 の判定、T1-737・738・740: amd64 の uefi-usb・nvme、pcat の bios-ide、pc98、sun4u、riscv32・m68k-virt・ppc32・ppc64・mips32・mips64 の VFS の段、rpi4 の login が PASS。QEMU だけ）
- 範囲: 13 の platform の vmunix の build（warning 0）、src/kern/vm/ の全 file を規約の全文に合わせる（書き方だけ、動作は変えない）。helper の接頭辞（判断⑥）は名前を変えず一覧だけ（下）。hal.h は変えない。

## 変更（42bf53226）

src/kern/vm/ の 23 file（+1411 −1190、ほぼ全部が関数の位置の移動）。

- 公開関数を static より前へ: object.c・object-cache.c・object-fault.c・object-page.c・object-sync.c・pageout.c・private-page.c・reclaim.c・vmspace.c・vmspace-fault.c・vmspace-map.c・vmspace-metadata.c。関数の単位（前の注記・頭・本文）ごとに、順を保って移した。
- 公開関数の前の 1 行の注記を複数行の形へ（文は変えない。80 桁を超える物は折り返した）。注記の無かった vm_object_writeback_release に注記を足した。
- include の重複を削った: device.c（6、`"kern/x.h"` と `<kern/x.h>` の重複と、間の空行）、swap.c・swap-control.c・swap-header.c・swap-source.c（各 3: kern/lock.h・kern/swap.h・kern/kmem.h）。使っていない include の削除は、全 platform での確かめの費用に見合わないので行っていない。
- 内部の header（object-internal.h・reclaim-internal.h・vmspace-internal.h）: 関数の宣言を定義の file ごとに分け、各群に「どの file の何の部分か」の注記。`struct x * f(` の空白と、object_mount_reserved の仮引数の名前を直した。
- file の頭の注記（著作権の次の説明）は 29 の .c の全部に既にあった。static の 1 行の注記と forward 宣言の欠けは 0（検査の script で確認）。

## helper の接頭辞（判断⑥、ユーザーに後で聞く）

内部の header で file の外に出ていて `vm_`・`vmspace_` の接頭辞の無い物（名前は変えていない）:

alloc_object_page clear_page_dirty_locked content_release_pages_locked destroy_object detach_vm_page find_object_by_inode_locked find_page find_region_locked free_object_page inode_unnamed insert_region map_region next_generation next_inode_content_generation_locked next_inode_resize_generation_locked object_cache_discard_prepared object_cache_evict_one object_cache_read_missing object_cache_retainable object_can_destroy object_has_busy_pages object_has_busy_pages_locked object_has_dirty_pages_locked object_hash_index object_initialize_eof_locked object_mount_reserved object_operation_begin object_operation_end object_page_dirty_link object_page_dirty_mark object_page_dirty_unlink object_page_index_insert object_page_index_remove object_prefetch_consume object_prefetch_retire object_record_writeback_error_locked object_wait_registry_transition object_wait_resize object_wake_registry_waiters overlaps page_overlaps pageout_kick prepare_cow_copy private_page_advance_locked private_page_alloc private_page_attach_new private_page_pinned_resident queue_insert queue_remove range_has_wired_mapping range_valid reclaim_object_fault_reserve region_page_index_free region_page_index_insert region_page_index_rebuild region_page_index_remove registry_lock registry_unlock release_detached_region_pages release_object_page_storage retain_object swapped_insert unlink_object_locked unlink_page_locked write_page_data

（65。特に一般的な名前は find_page・overlaps・queue_insert・queue_remove・range_valid・insert_region・map_region・destroy_object・retain_object・next_generation・registry_lock。）

## 確かめ

- build: `make -j16 ZEDBSD_CONFIG=<config> BUILD=build/ws274-p007-<base|after>-<p> vmunix`。config は config/ci/config-{amd64,pcat,pc98,rpi4,riscv64-virt,visionfive2,riscv32-virt,m68k-virt,ppc32-mac99,ppc64-pseries}.mk と plan/tools/ports/config-{x68k,sun4u,mips32-malta,mips64-malta}.mk の 14（13 の platform、riscv64 は virt と vf2）。base（main の 1edce7627 を merge した HEAD）と after（42bf53226）の両方で 14 とも exit 0、`grep -c 'warning:'` 0。
- 照合: `python3 -I plan/ws274/tests/body-compare.py HEAD~ src/kern/vm/*.c -- src/kern/vm/*.c` 相当（commit の前に HEAD と作業ツリーで）→ functions 424 names 423 differences 0、PASS。空行と注記の区切りを除いた行の多重集合も、変更の前と同じ（include の重複を除く前に確認）。
- nm（`build/llvm/bin/llvm-nm vmunix` の型と名前）: amd64・pcat・sun4u・x68k で base と after の差 0（local を含む）。
- host: `sh plan/ws213/tests/kd-host-test.sh` PASS、`sh plan/ws213/tests/pin-host-test.sh` PASS。
- 未実施: QEMU の起動（下の T1 の依頼）。

## T1 への依頼（WS274 p007、全 platform の起動の回帰）

worktree agent/p1 の 42bf53226（または Q1 が merge した main）で、振る舞いを変えない VM の分割（p001〜p007）の後の起動の回帰。p001〜p006 の依頼とまとめて 1 つの QEMU で流してよい。

1. `plan/tools/boot-test.sh` の uefi-usb（amd64、8 GiB）と bios-ide（pcat）で login prompt。
2. `plan/tools/pc98-boot.py` で pc98 の起動。
3. `plan/tools/ports/` の各 port（sun4u、m68k-virt、x68k、ppc32-mac99、ppc64-pseries、riscv32-virt、mips32-malta、mips64-malta）と riscv64-virt・rpi4（arm64）の起動の回帰（各 port の既定の手順）。

## 片付け（Q1 に依頼）

worktree /home/awe/zedBSD-worktrees/p1 の build/ws274-p007-base-* と build/ws274-p007-after-*（各 14）と各 .log。
