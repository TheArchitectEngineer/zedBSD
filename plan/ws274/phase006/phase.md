# ws274-p006 死んだ code と重複を除く（判断⑦）

- 親: [WS274](../ws.md)、Queue: q1302 / q1302-i01（P1、2026-10-19）
- Status: in-progress（実装と build・host 試験は済み、T1 の起動の回帰の結果待ち）
- 範囲: [design.md](../design.md) §4 の死んだ code・中身の無い weak の hook・重複。slab の allocator の統一と長い関数の分割は範囲外。振る舞いは変えない。hal.h は変えない。

## 確かめた事（参照が無い）

`git grep -nw <名前> -- . ':!plan/ws274'`（src・include・userland・試験・plan/tools を含む tree 全体）で、定義と公開の header の宣言のほかに参照が無い事を確かめた。plan/ws082/study.md・plan/ws213/phase007 の言及は文書の中の説明だけ（呼び出しではない）。

## 変更（c3999fff9）

| 物 | 今の場所 | 処置 |
| --- | --- | --- |
| vm_object_truncate_inode | vm/object-sync.c、vm-object.h | 定義と宣言を削除 |
| vm_object_retained_count | vm/object-cache.c、vm-object.h | 同上 |
| vm_object_discard_mount_check | vm/object-cache.c、vm-object.h | 同上 |
| vm_commit_can_shutdown_swap | vm/commit.c、vm-commit.h | 同上 |
| vm_private_page_vaddr | vm/vmspace-fault.c、vmspace.h | 同上 |
| vmspace_wire_range・vmspace_unwire_range・vmspace_unwire_range_locked | vm/vmspace-copy.c、vmspace.h | 同上（_locked は static の宣言も） |
| vmspace_map_anon | vm/vmspace-map.c、vmspace.h | 同上 |
| weak の hook vm_object_read_checkpoint | vm/object-content.c | extern と 2 つの呼び出しを削除 |
| weak の hook vmspace_unmap_retire_checkpoint・vmspace_object_revoke_checkpoint | vm/vmspace-unmap.c | extern・注記と 3 つの呼び出しを削除 |
| 重複 vmspace_fault_wake（reclaim.c の static）と vmspace_fault_wake_locked（vmspace-fault.c） | vm/reclaim.c | reclaim.c の複製を消し、vmspace-internal.h を include して vmspace_fault_wake_locked を呼ぶ。本文は同じ（guard の spinlock だけを取る）なので、注記を「guard の spinlock だけを取る」に直した |
| alloc_vm_page の届かない取り消しの枝 | vm/object-page.c | vm_private_page_alloc は成功の時だけ size を入れる（直前に memset で 0）ので、失敗で size≠0 の枝は届かない。削除 |
| 1 行の注記の重なり | — | 分割の後の tree には残っていなかった（`*.c` の連続する 1 行の注記を awk で探して 0 件） |

vmspace_pin_page_checkpoint は pin-host-test が使うので残した。

## 確かめ

- build: `make -j16 ZEDBSD_CONFIG=<config> BUILD=build/ws274-p006-<p> vmunix`（config/ci/config-amd64.mk（LTO）、config/ci/config-pcat.mk、plan/tools/ports/config-sun4u.mk）。3 つとも exit 0、`grep -c 'warning:'` 0。
- 比べる base は同じ worktree で HEAD~1 の src/kern/vm・include/kern に戻して `BUILD=build/ws274-p006-base-<p>` で build（exit 0）、戻した後に作業ツリーは clean。
- nm（`build/llvm/bin/llvm-nm vmunix`）: 大文字の型の symbol は 3 つとも base と diff 0（消えた関数は元から gc・LTO で vmunix に残っていなかった）。小文字の local で消えたのは `alloc_vm_page`（object-page.c の方が inline になった）と sun4u の `vmspace_fault_wake` だけ。amd64・pcat の他の差は static の局所変数の通し番号の 1 ずれ。sun4u の vm の object では、消した 8 つの `T` が base 8 → 0。
- host: `sh plan/ws213/tests/kd-host-test.sh` PASS、`sh plan/ws213/tests/pin-host-test.sh` PASS。
- 未実施: QEMU の起動（下の T1 の依頼）。

## T1 への依頼（WS274 p006）

worktree agent/p1 の c3999fff9（または Q1 が merge した main）で、振る舞いを変えない死んだ code の削除の後の起動の回帰。p001〜p005 の依頼とまとめて 1 つの QEMU で流してよい。

1. `plan/tools/boot-test.sh` の uefi-usb（amd64、8 GiB）と bios-ide（pcat）で login prompt。
2. `plan/tools/pc98-boot.py` で pc98 の起動。
3. `plan/tools/ports/` の sun4u（sparcv9、gcc で LTO 無し）の起動の回帰。

## 片付け（Q1 に依頼）

build/ws274-p006-{amd64,pcat,sun4u}、build/ws274-p006-base-{amd64,pcat,sun4u} と各 .log（worktree /home/awe/zedBSD-worktrees/p1 の中）。
