<!-- awesome-plan project=zedbsd record=ws274-p004 -->

# ws274-p004 S5 vmspace の群と vmspace-internal.h

- 親: [ws274](../ws.md)、設計: [design.md](../design.md) §1.2、§2 の vmspace-internal.h、§3 の S5
- Status: in-progress（q1300 / q1300-i01、P1、2026-10-19）。T1 の起動の回帰の結果を Q1 が判定するまで cleared にしない。
- 規約: p001〜p003 と同じ「配置の変更だけの例外」（[ws106-relocation.md](../../standards/ws106-relocation.md) の考え）。関数の本文・評価順・lock の順は変えない。

## 行った事（worktree `/home/awe/zedBSD-worktrees/p1`、agent/p1、commit f083264c5、親 aedc6c074 = p003 の S4）

vmspace.c（7481 行）を 8 つの file と device.c に分けた。vmspace.c は同じ名前のまま寿命と layout だけを残した。

| file | 中身 |
| --- | --- |
| vm/vmspace.c | 寿命と layout: layout_init・create・tryref・ref・put・put_deferred・set_reaper_notify・reap_pending・count・address_cap・destroy・generation_advance_locked。kernel_vmspace・vm_layout（global）と reap の変数・vm_layout_initialized・vmspace_live（static） |
| vm/vmspace-metadata.c | slab: vm_page_alloc/free_metadata・vm_private_page_free_metadata・alloc_vm_page（static のまま）・alloc_vm_metadata_page・slab の take/link/unlink・vm_private_page_alloc_metadata・private_page_alloc・attach_new。struct vm_page_slab・vm_private_page_slab と _Static_assert、slab の 4 変数（global） |
| vm/vmspace-fault.c | fault と問い合わせ: effective_prot・is_resident・vaddr・vmspace_fault・reclaim_page_map_reserve・private_page_pinned_resident・wait_fault_event・wait_faults_locked・allocate_page_frame・fill_file_page・prepare_cow_copy・fault_wake_locked・exec_cache_fault・fault_around(_page)_locked |
| vm/vmspace-copy.c | pin・copy・wire: user_range_valid・pin/unpin_user_pages・wire_range・check・unwire_range・copy_to/from と各 _locked・pin の helper・copy_backing |
| vm/vmspace-fork.c | vmspace_fork・fork_locked・fork_region・fork_batch_*・fork_share_page。struct vmspace_fork_entry |
| vm/vmspace-map.c | sync・map_anon*・map_file*・map_stack・find_region・shared_mapping_key・find_free_range(_bounded)・map_find・map_file(_shared)_find・map_exec_snapshot・map_region・prepare_region・discard_prepared_region・各 map の _locked・private_file_object |
| vm/vmspace-region.c | range_valid・overlaps・insert_region・find_region_locked・vmspace_find_page・region_page_index_*・find_free_range(_bounded)_locked・map_find_locked・map_file(_shared)_find_locked |
| vm/vmspace-unmap.c | object_page_revoke・brk・unmap・protect・limit（公開と _locked）・free_vm_page・detach の群・split_region(_prepared)・release_retired_regions・replace_prepared |
| vm/device.c | 判断④のとおり vmspace_map_device と vmspace_device_fault を末尾に足した |
| vm/vmspace-internal.h | 定数（36-66 行の define）、slab の 4 変数の extern、外に出した 30 関数の宣言、lock の順の注記 |

- 外に出した（static を外した）関数は 30（design.md の「約 30」）。変数は slab の 4 つ。
- **design.md と違えた所:**
  - `find_page` を `vmspace_find_page` に改名した（宣言と呼ぶ所の 8 箇所、本文は同じ）。S4 で object-page.c の find_page が global になっており、そのままでは amd64・pcat・sun4u の 3 つとも link で `duplicate symbol: find_page` になったため。判断 6-6（接頭辞は S8）の前倒しはこの 1 つだけ。
  - free_vm_page は metadata でなく unmap に置いた（使うのは brk だけで、detach の群と隣り合うため）。vmspace_user_range_valid は copy に置いた（pin と copy が使う）。
- weak extern は使う file だけに写した（unmap_retire・object_revoke は unmap、pin_page は copy）。forward 宣言は各 file に、その file の static の物だけ（元の順）。
- 各 file の include は元の vmspace.c の一覧を全部写した（増減の判断をしないため）。device.c は自分の include の後にその一覧と vmspace-internal.h を足した。
- 試験の追従: `plan/ws213/tests/pin-host-test.sh` の 36-47 行を、define は vmspace-internal.h から、宣言は vmspace-copy.c・vmspace-region.c の static と vmspace-internal.h から、関数は vmspace-copy.c・vmspace-region.c から取る形にした（find_page → vmspace_find_page）。`plan/ws274/tests/body-compare.py` に `--rename old=new`（古い側の識別子を置き換えて比べる）を足した。

## 確かめ（host）

- build: `make -j16 ZEDBSD_CONFIG=<config> BUILD=build/ws274-s5-<p> vmunix`、config/ci/config-amd64.mk・config/ci/config-pcat.mk・plan/tools/ports/config-sun4u.mk。3 つとも exit 0、log の `warning:`・`error:` 0。
- 照合: `python3 -I plan/ws274/tests/body-compare.py --rename find_page=vmspace_find_page aedc6c074 src/kern/vm/vmspace.c src/kern/vm/device.c -- src/kern/vm/vmspace.c src/kern/vm/vmspace-*.c src/kern/vm/device.c` → functions 138 names 138 differences 0、PASS（--rename 無しでは find_page を呼ぶ 10 関数だけが DIFF）。
- nm（`build/llvm/bin/llvm-nm`、大文字の型）: amd64 56・pcat 58（LTO、前段と同じ）。sun4u 1511（p003 の記録値）→ 1545（+34 = 30 関数＋4 変数）。外に出した 34 が全部 global にあることを comm で確かめた（欠け 0）。
- `sh plan/ws213/tests/pin-host-test.sh` PASS、`sh plan/ws213/tests/kd-host-test.sh` PASS。
- 未実施: 残りの platform の build（p007）、QEMU の起動（下の T1 の依頼）。

## T1 への依頼（WS274 p004）

worktree agent/p1 の f083264c5（または Q1 が merge した main）で、振る舞いを変えない vmspace の群の分割の後の起動の回帰。p001〜p003 の依頼とまとめて 1 つの QEMU で流してよい。

1. `plan/tools/boot-test.sh` の uefi-usb（amd64、8 GiB）と bios-ide（pcat）で login prompt。
2. `plan/tools/pc98-boot.py` で pc98 の起動。
3. `plan/tools/ports/` の sun4u（sparcv9、gcc で LTO 無し）の起動の回帰。
- 任意: `plan/tools/process/guest-vfork.sh`（fork の経路）。login 後の shell で数個の command（mmap・fork・exec の多い物）があれば fault・fork・map・unmap の経路の確かめになる。
- 合格: どれも login prompt（または各道具の PASS）。PNG を返す。

## 残り

- `plan/ws014/tests/run-venus-remote.py` が src/kern/vm/vmspace.c を参照する（WS014 は closed、master の一覧に無い）。AGENTS.md の追従の基準により直さず、削除を Q1 に頼む。
- 自分の build の出力 build/ws274-s5-{amd64,pcat,sun4u}/ と build/ws274-s5-*.log は Q1 が消してよい。
