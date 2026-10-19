<!-- 案（2026-10-19、Q1 が読むだけの調べの担当に作らせた。main 44c3d666a を調べた）。まだユーザーの判断の前で、code は変えていない -->

# zedBSD の VM の code を src/kern/vm/ に分ける案

読むだけで、file は何も変えていません。`.internal/` も読んでいません。
- 調べた版: main 44c3d666a。途中で HEAD が 6bb830002 から進みましたが、VM の file には差がないことを `git diff` で確かめました。
- 行番号は awk で関数の定義の範囲を数えた値です。
- 他の branch・worktree に、main へ入っていない vm.c・vmspace.c・swap.c・vm-device.c・VM の header の変更はありません。今は衝突しない時期です。

**一言で:** 中身で 3 つの群に分かれます。object 群は static が強く絡んでいます（他の群から呼ばれる static 関数が約 45、共有する変数が 7）。匿名 page の群と vmspace の群は比較的きれいです（後者で約 30 関数）。まず「関数の本文を変えずに移す」段を 1 群ずつ進め、規約に合わせる段と死んだ code の整理はその後に別の段にします。

---

## 1. 今の構造

### 1.1 src/kern/vm.c（10350 行）

冒頭の注記（vm.c:8-17）に「元は別の翻訳単位だったが、状態を共有するので当面まとめてある」とあります。中の区切りは `Object` 469、`Commit` 3795、`Page` 4028、`Metadata` 4062、`Private` 4200、`Page` 4697、`Reclaim` 5038、`Helpers` 6044 です。static の helper は 6044 以降に全部まとめて置かれています。

| まとまり | 公開関数 | static helper | 行数 |
|---|---|---|---|
| A. object の登録と寿命（get_shared・backing_busy・create_anonymous・ref・put） | 476-805 | 6054-6440（get_shared_internal 188 行、registry_lock・unlock、operation_begin・end）、6525-6560（generation）、6819-6979（can_destroy・unlink・destroy・retain）、8520-8608 | 約 1000 |
| B. 内容と大きさの変更（inode_io_wait、content_read_begin・end、content_begin・prepare・commit・abort、read_coherent(_useful)、resize_*） | 812-1687 | 6564-6613、6983-7345（content_finish 163 行、resize_finish 143 行） | 約 1290 |
| C. object の fault・pin・mapping（vm_object_fault 228 行、page_pin*、unpin、fault_release・resident、mapping_add・remove、mark_dirty） | 1696-2337 | 7349-7452 | 約 750 |
| D. 同期と書き戻し（sync_range・sync_inode(_buffer)・truncate_inode、writeback_prepare・release、content_prepare_delayed 202 行、sync_mount_buffer） | 2343-2590、3093-3468 | 6701-6791、7456-7976（sync_range_buffer 413 行）、9062-9244（write_dirty_pages） | 約 1420 |
| E. object cache（count 類、cache_prepare・pin・unpin・reclaim_pinned・drain、discard_mount*、cache_discard_inode） | 2596-3087、8652-8696 | 8609-9058（cache_read_missing 279 行）。5070-5242 の vm_object_reclaim_clean・reclaim_one もここに入る | 約 1115 |
| F. 先読み（prefetch_prepare・complete・abort） | 3474-3792 | 9251-9321 | 約 390 |
| G. object の page の置き場（slab、descriptor、AVL の索引、find_page） | — | 6459-6521、6617-6697、6795-6815、7982-8516 | 約 700 |
| H. commit | 3806-4025 | — | 約 220 |
| I. metadata lock | 4069-4197 | 10282-10350 | 約 200 |
| J. private page（vm_private_page_*、vm_private_page_alloc 6445） | 4207-4694 | 10070-10169 | 約 600 |
| K. page の追跡・置き換え・共有（vm_page_note_in 4035、track・untrack・replace・share） | 4035-5035 | — | 約 360 |
| L. reclaim（queue、victim の選び方、swap-out、drain） | 5045-5415、5691-6041 | 9325-10066、10173-10272 | 約 1380 |
| M. page-out と OOM（wait_free、free_pages の見積もり、pageout_kick・worker、oom_kill） | 5425-5685 | — | 約 260 |

file の先頭（1-461 行）には macro、型、static な状態、weak extern、forward 宣言が置かれています。

**static な状態と使う所**（awk で関数ごとに数えた結果）:

| 変数（行） | 使う群 |
|---|---|
| shared_objects 123、object_hash 132、object_count 144、object_registry_generation 152、object_use_generation 160、cache_objects 168 | A・D・E |
| object_pages 173 | C・D・E・F・G |
| object_registry_lock 181 | A だけ（registry_lock・unlock を通す） |
| page_slabs・page_slab_lock 186-191 | G だけ |
| commit_stats・commit_lock・commit_initialized・commit_swap_seeded（199-207、299-307） | H だけ |
| metadata_lock・metadata_depth・metadata_perf・metadata_taken_*（211-219、312-332） | I だけ |
| page_queue・page_queue_tail・swapped_queue（224-242） | J・K・L |
| pageout_lock・pageout_waitq・free_waitq・pageout_started・pageout_requested・free_snapshot_*（247-281） | L と M |
| reclaim_lock 286、`#define stats vm_reclaim_counters` 106 | J・K・L・M（reclaim_lock は 16 関数で使う） |

- **lock:** rank は `include/kern/lock.h:28-34` に enum としてあります（VMSPACE 80、VM_RESIZE 85、VM_OBJECT 90、SWAP 95）。ただし src/kern/lock.c:58 は rank を記録するだけで、順を実行時に確かめていません。lock の順は注記（`include/kern/vm-lock.h:43-48`、metadata lock を他の VM の lock より先に取る）でしか守られていません。
- **registry_lock:** vm.c:6245-6272 で、割り込みを止めてから atomic で spin します。
- **群の間の呼び出し**（static のまま他の群から呼ばれる物）:
  - object 群（A〜G）の中で約 45 個。例: registry_lock・unlock は B〜F から 28 箇所、find_object_by_inode_locked は 12 箇所、destroy_object は 14 箇所、object_operation_end、find_page、alloc_object_page、object_page_index_insert・remove、free_object_page、object_page_dirty_mark、vm_object_sync_range_internal、object_wait_resize。
  - 匿名 page の群（J・K・L）の間で 6 個: queue_insert、queue_remove、swapped_insert、private_page_advance_locked、vm_private_page_wait_idle_cancelable、reclaim_object_fault_reserve（最後のものは L から C へ）。
  - H と I は他の群から static を呼ばれません。
- **weak extern（vm.c:340-356）:** 群ごとに使う物が違います。
  - registry と slab: hal_irq_disable・hal_irq_enable
  - D: writeback_*、io_pool_*、io_error_record
  - B・F: disk_cache_*、readahead_consumed
  - G・C・F: cache_memory_*
  - B・D: vmspace_object_page_revoke

### 1.2 src/kern/vmspace.c（7481 行）

中に区切りの見出しはありません。static は 3628 行以降にあります。

| まとまり | 範囲 | 行数 |
|---|---|---|
| 寿命（layout_init 312、create・tryref・ref 983-1041、put・put_deferred・reaper・count・address_cap 2339-2481、destroy 7008-7062、generation_advance 7113） | 左の通り | 約 315 |
| metadata の slab（vm_page_alloc・free_metadata、vm_private_page_free_metadata 369-564、slab 3631-3868、private_page_alloc・attach_new 3979-4014、free_vm_page 6005） | 左の通り | 約 490 |
| device の mapping（vmspace_map_device 570-664、vmspace_device_fault 7432-7481） | 左の通り | 約 145 |
| object の page の取り消し（vmspace_object_page_revoke 711-892、182 行） | 左の通り | 約 180 |
| 問い合わせ（effective_prot・is_resident・vaddr 899-977） | 左の通り | 約 80 |
| fault（**vmspace_fault 1052-1821、770 行**、wait・fill・cow 5189-5375、exec_cache_fault と fault-around 7123-7428） | 左の通り | 約 1260 |
| pin・copy・wire（1827-2240、check・unwire・copy_to・from 3058-3153、pin の helper と copy_backing 5379-5779） | 左の通り | 約 910 |
| fork（2490-2547、4016-4528） | 左の通り | 約 570 |
| map（sync 2250-2333、map_* 2553-3052、find 3159-3351、exec_snapshot 3588-3627、map_region・prepare・*_locked 4532-4970） | 左の通り | 約 1260 |
| region（range_valid・overlaps・insert 3872-3975、find_region・find_page・region の page の索引 4974-5185、find_free_range 5783-5994） | 左の通り | 約 530 |
| unmap・protect・brk・limit（3358-3584、detach・split・replace・brk・unmap・protect 6029-7004、limit 7066-7109） | 左の通り | 約 1430 |

**static な状態:**
- vm_page_slab_lock・vm_page_slabs・vm_private_page_slab_lock・vm_private_page_slabs（138-159）: slab の群と layout_init が使います。
- reap 系（164-206）: 寿命の群と layout_init が使います。
- vmspace_live 198: 寿命の群だけが使います。
- kernel_vmspace 127 と vm_layout 185 は既に global です。

**群の間で呼ばれる static は約 30 個です:**
- find_region_locked は 10 箇所、find_page は 8 箇所、vmspace_generation_advance_locked は 19 箇所。
- region_page_index_*、map_region、range_valid、overlaps。
- vmspace_wait_faults_locked、vmspace_fault_wake_locked、private_page_alloc・attach_new、vmspace_replace_prepared、prepare_cow_copy、vmspace_destroy。
- weak の試験の hook が 215-217 にあります。

### 1.3 名前の衝突（分割で効く所）

- **関数:** `alloc_vm_page`（vm.c:6459、vmspace.c:3631）と `find_page`（vm.c:6484、vmspace.c:4998）が両方の file で static です。
- **型:** `struct vm_page_slab` は vm.c:111 と vmspace.c:77 で中身の違う別の型です。
- **一般的すぎる名前:**
  - `queue_remove` は sched.c にも static であります。
  - `range_valid` は usb-cdc-ncm.c と libc に、`destroy_object` は bcm2711/vulkan-video.c にあります。どれも static なので、今は link で衝突しません。
- **macro:** `#define stats` は header に置けません（vm.c:106）。

### 1.4 その他の VM に近い file の判断

| file | 判断 | 根拠 |
|---|---|---|
| vm-device.c（262） | **VM に属する**。device の記憶域を mapping の寿命から独立に保つ物 | 呼ぶのは vmspace・syscall・audio・gpu |
| swap.c（3885） | **VM に属する**（裏打ちの記憶域） | slot store 169-1144、control 1153-1470、header の codec 1476-1673、source 1679-2830、static 2834-3885 |
| pmem.c（360） | 移さない | VM ではなく driver 向けの kern_* の shim。boot framebuffer（231）、rtc（308）、memstat も含む |
| uncached.c（79） | 移さない | DMA 用の uncached な窓。一部の arch だけで build する |
| tmpfs-pages.c/.h | 移さない | tmpfs の私的な radix tree。使うのは tmpfs.c だけ |
| cache.c | 移さない | buf と object の cache の予算。buf.c も使う。vm との繋がりは weak 1 つ（cache.c:40） |
| writeback.c・readahead.c | 移さない | I/O 層。vm_object_* の呼び出しは 1〜4 箇所 |
| backing-claim.c | 移さない | VFS・swap・loop の claim の登録。vm との繋がりは weak 1 つ（81） |
| heap.c | 移さない | kern_malloc の固定の heap |

### 1.5 header

- 公開の header: vm-object.h 462 行（12 file が include）、vmspace.h 619 行（12）、vm-reclaim.h 197 行（8）、vm-commit.h 57 行（6）、vm-lock.h 70 行（3）、vm-device.h 53 行（6）。
- VM の中（vm.c・vmspace.c・vm-device.c）からしか使われない公開関数が約 55 あります。例: vm_private_page_* の大半、vm_page_track・replace・share、vm_page_free_metadata、vmspace_object_page_revoke、vm_reclaim_one・frame。
- VM の header に static inline はありません。vm.c:4089 の `__attribute__((noinline)) vm_metadata_enter` は保つ必要があります（遅延の診断が戻り番地を使うため）。

---

## 2. 分割の案（src/kern/vm/）

大きさは関数の本文の行数に、include・宣言・注記の約 5〜10% を足した目安です。

| file | 中身 | 目安の行数 |
|---|---|---|
| object.c | A | 1100 |
| object-content.c | B | 1350 |
| object-fault.c | C | 800 |
| object-sync.c | D | 1500 |
| object-cache.c | E（object の reclaim の 2 関数を含む） | 1200 |
| object-prefetch.c | F | 420 |
| object-page.c | G（vm.c の struct vm_page_slab はこの file の中に留める） | 750 |
| commit.c | H | 250 |
| metadata-lock.c | I（vm-lock.h の実装） | 230 |
| private-page.c | J | 650 |
| page-track.c | K | 400 |
| reclaim.c | L | 1450 |
| pageout.c | M | 300 |
| vmspace.c | 寿命と layout | 380 |
| vmspace-metadata.c | slab と _Static_assert（300-306） | 530 |
| vmspace-fault.c | fault と問い合わせ | 1400 |
| vmspace-copy.c | pin・copy・wire | 960 |
| vmspace-fork.c | fork | 600 |
| vmspace-map.c | map と msync | 1320 |
| vmspace-region.c | region | 570 |
| vmspace-unmap.c | unmap・protect・brk・limit、revoke | 1650 |
| device.c | vm-device.c と、vmspace の device の 2 関数（判断 6-4） | 420 |
| swap.c ほか（判断 6-3） | swap.c（slot・manager・I/O）、swap-control.c、swap-header.c、swap-source.c | 1300 / 600 / 300 / 1700 |

内部の header は 3 つにする案を既定とします（判断 6-2）。

- **object-internal.h**
  - 外に出す変数: shared_objects・object_hash・object_count・object_registry_generation・object_use_generation・cache_objects・object_pages（7 個、VM_OBJECT_DATA の section を保つ）。
  - 定数: VM_OBJECT_* と PAGE_SIZE。
  - 関数: 1.1 に挙げた約 45 の helper の宣言。
  - 外に出さない物: page_slabs・page_slab_lock、object_registry_lock（それぞれ object-page.c と object.c の static のまま）。
- **reclaim-internal.h**
  - 変数: reclaim_lock、page_queue・page_queue_tail・swapped_queue、pageout_lock・free_waitq（vm_reclaim_frame と vm_reclaim_init が使う）。
  - 関数: queue_insert・queue_remove・swapped_insert、private_page_advance_locked、vm_private_page_wait_idle_cancelable、reclaim_object_fault_reserve、vm_free_pages・vm_free_min_pages・pageout_kick。
  - 定数: VM_PAGE_TRACKED（107）と VM_RECLAIM_*・VM_FREE_*。
  - `stats` の macro は header に置かず、使う file ごとに置きます。後の段で `vm_reclaim_counters` に書き換えます。
- **vmspace-internal.h**
  - 関数: 約 30 の helper。
  - 変数: slab の 4 変数。vmspace_layout_init を vmspace.c に置くと、slab の初期化のためにこれらが要ります。
  - 定数: VM_PAGE_SLAB_SLOTS ほか、40-60 行の定数。

**公開の header（include/kern/）は分割の段では変えません。** vm.c の外の 30 を超える file が include しているので、変えると影響が広がります。VM の中でしか使わない約 55 の宣言を内部の header へ移すのは、判断 6-5 の後の任意の段にします。

weak extern は、coding-style.md:139-143（「file の間で code を移す時にこの block を落とすな」）に従い、使う file ごとに必要な物だけを写します。

---

## 3. 手順（各段は build と試験で確かめられる単位）

**S0 準備**
- Makefile の KERN_NET_SOURCES（Makefile:771、805）と同じ形で `KERN_VM_SOURCES` と `KERN_VM_OBJS` を作ります。
- 11 の platform をこの変数に移します。source の一覧を持つ 9 つ:
  - amd64:440、arm64:72-73、m68k-virt:58-59、ppc32-mac99:57-58、ppc64-pseries:57-58
  - riscv32:70-71、riscv64:83-84、sparcv9:70、x68k:123-124
- object の一覧を持つ 2 つ: pcat:54-59 と pc98:70-75（swap.o は pcat:150、pc98:119）。
- object の規則は全部 `mkdir -p $(dir $@)` で、`%` が subdirectory にも合うので、規則は変えずに済みます（Makefile:869、amd64:549）。
- 関数の本文の移動を照合する script を phase の tests/ に作ります。関数の名前ごとに元と新しい所の本文を比べ、`static` の有無だけを許します（WS106 の hash の照合と同じ考え）。

**S1 移動だけ**
- `git mv` で vm.c → vm/vm.c、vmspace.c → vm/vmspace.c、vm-device.c → vm/device.c とします（device.c の移動は判断 6-4 次第）。
- 変数・試験の path・docs/architecture/kernel-and-hal.md:16 を直します。

**S2 H と I を切り出す**
- static を外に出す必要がありません。
- 試験の path（kd-host-test.sh:43）を直します。

**S3 匿名 page の群（J・K・L・M）と reclaim-internal.h**
- vm_reclaim_init（5045-5064）の初期化の順は変えません。

**S4 object 群（A〜G）と object-internal.h**
- 一番大きな段です。必要なら S4a（G と F）と S4b（残り）に分けます。

**S5 vmspace の群と vmspace-internal.h**
- pin-host-test を追従させます。
- vmspace_device_fault を device.c へ移すかは判断 6-4 次第です。

**S6（任意）swap.c を 4 つに分ける。**

**S9 最後の確かめと記録。**

### 各段の危険

- **lock の順:** 実行時の検査がないので、関数の本文と呼ぶ順を変えなければ保たれます。照合の script で本文が同じことを確かめます。順の注記は内部の header にも書きます（中身を変えない文書だけの追加）。
- **static inline:** VM にはありません。新しく作らない、が既定です。
- **LTO:** amd64・arm64・pcat・pc98・riscv は既定が full LTO（Makefile:130）なので、file の間でも inline は効きます。m68k-virt・ppc32・ppc64・sparcv9・x68k は LTO の行がなく、gcc の物も含みます。そこでは registry_lock・find_page・page_index_* のような熱い小さな関数が inline されなくなり、少し遅くなりえます。振る舞いは変わりません。
- **gcc の -Wall -Wextra -Werror:** 分けた後に使わなくなった static や forward 宣言が残ると `-Wunused-function` で止まります。各段で gcc の platform を 1 つは build します。
- **名前の衝突:** 同じ名前で別の中身の物（alloc_vm_page・find_page・struct vm_page_slab）は、別々の内部 header に置くか file の中に留めれば衝突しません。外に出す一般名（find_page、queue_insert、overlaps、destroy_object など）は、移動の段では名前を変えず内部の header だけに宣言し、S8 で `vm_object_`・`vmspace_` の接頭辞を付ける案です（判断 6-6）。
- **coding style:** static を外に出すと「公開関数は static より前、複数行の注記」（coding-style.md:176-213）に反します。WS106 と同じ「配置変更だけの例外」をユーザーに承認してもらうのが既定です（判断 6-1）。
- **試験の link の list と、関数を名前で切り出す試験:**
  - `plan/ws213/tests/kd-host-test.sh`（master.md:1215 に登録）: 43 行で src/kern/vm.c から 3 関数を切り出します → path を metadata-lock.c に直します。
  - `plan/ws213/tests/pin-host-test.sh`（master.md:1216 に登録）: 36-47 行で vmspace.c から define を取り、`^static ...` の forward 宣言を grep し、15 関数を切り出します。find_region_locked・find_page・copy_backing などが外に出ると grep が外れます → 複数の file と内部の header から取るように直します。
  - 登録されていない、完了・closed の WS の試験は、追従の時の基準（AGENTS.md の 2026-10-06）に従い、直さずに削除を Q1 に頼みます:
    - plan/ws073/tests/region-index-host.sh（WS073 は completed）
    - plan/ws073/tests/swap-slot-host.sh（swap.c を移す場合）
    - plan/ws014/tests/run-venus-remote.py:49-51（vmspace.c と vm-device.c、WS014 は closed）
    - plan/ws014/tests/run-gpu-*.sh（vm-device.c）
  - vm-device.c を移すと、進行中の WS の試験も直す必要があります: plan/ws113/tests/host-display-control.sh:22、plan/ws214/tests/display-p003a-host.sh:15、plan/ws141/tests/host-display-cursor.sh:22。ws141 は別の session が専用に更新しているので、そちらに依頼します。
- **古い依存の file:** Makefile:1273 が build/ の全部の .d を wildcard で include します。古い vm.d は何からも要求されないので害はありません。build/ は消しません。
- **docs:** docs/reference/kernel-boot-parameters.md:40-41（swap.c）も直します。userland/base/mkswap/swap-codec.c:9 は注記だけです。

---

## 4. 同時に直したい事（分割とは別の段、今回は案だけ）

**長すぎる関数（150 行を超える物）**
- vmspace.c: vmspace_fault 1052-1821（770 行）、vmspace_pin_user_pages 1863-2119（257）、vmspace_protect_locked 6775-7004（230）、vmspace_object_page_revoke 711-892（182）、vmspace_replace_prepared 6370-6543（174）。
- vm.c: vm_object_sync_range_buffer 7479-7891（413）、object_cache_read_missing 8780-9058（279）、vm_object_fault 1696-1923（228）、vm_object_content_prepare_delayed 3193-3394（202）、vm_object_get_shared_internal 6054-6241（188）、vm_object_content_finish 7037-7199（163）。
- vmspace_fault は 4 つに分かれます: 既にある private page の道 1186-1436、共有の object page の道 1514-1622、新しい private page の道 1623-1748、後片付け 1750-1814。

**重複**
- vm.c:9582-9592 の `vmspace_fault_wake` と vmspace.c:5365-5375 の `vmspace_fault_wake_locked` は同じ本文です。
- `alloc_vm_page` が 2 つあります（vm.c:6459-6480、vmspace.c:3631-3643）。vm.c の方の取り消しの枝は届きません。vm_private_page_alloc（6445-6456）は成功の時だけ size を入れ、その前に memset で 0 にしているからです。
- slab の allocator が 3 つあります: object の descriptor（vm.c:8018-8222）、vm_page（vmspace.c:369-491、3687-3750）、private page（vmspace.c:497-564、3754-3868）。
- vm.c:6442-6443 で、1 行の注記が 2 つ重なっています。

**死んだ code（定義と header の宣言のほかに参照がない。リポジトリ全体を grep で確認）**
- vm_object_truncate_inode（2523-2590）
- vm_object_retained_count（2632-2651）
- vm_object_discard_mount_check（2957-2968）
- vm_commit_can_shutdown_swap（4008-4025）
- vm_private_page_vaddr（vmspace.c:950-977）
- vmspace_wire_range（2126-2240、115 行）
- vmspace_unwire_range（3089-3105）と vmspace_unwire_range_locked（5574-5602）
- vmspace_map_anon（2553-2582）

**weak の試験の hook**
- 中身がどこにもない物: vm_object_read_checkpoint（vm.c:349）、vmspace_unmap_retire_checkpoint（vmspace.c:215）、vmspace_object_revoke_checkpoint（vmspace.c:217）。
- vmspace_pin_page_checkpoint は pin-host-test が使うので残します。

**公開しているが中でしか使わない物（static にできる）**
- vm_object_reclaim_one、vm_object_sync_inode_buffer、vm_page_note_in、vm_private_page_io_try_acquire、vm_private_page_operation_try_begin、vm_page_alloc_metadata、vm_page_effective_prot、vm_private_page_is_resident。

**その他**
- vm.c:340-356 の weak のうち writeback・io_pool・cache_memory は、kernel では常に link される相手です。weak にしている理由を確かめて整理します。

---

## 5. 見積もりと確かめ

| 段 | 内容 | LW |
|---|---|---|
| S0 | 準備（変数の導入、照合の script） | 3 |
| S1 | 移動と配線 | 2 |
| S2 | commit と metadata lock | 2 |
| S3 | 匿名 page の群 | 5 |
| S4 | object 群 | 10 |
| S5 | vmspace の群と pin 試験 | 8 |
| S9 | 最後の確かめと記録 | 3 |
| **中心の合計** | | **約 33 LW（約 8〜14 時間）** |
| 任意 S6 | swap.c を分ける | 4 |
| 任意 S7 | 公開の header を絞る | 4 |
| 任意 S8 | 規約の全文に合わせる（約 75 の関数の注記・順・接頭辞、試験の追従） | 8〜10 |
| **任意を含めた合計** | | **約 50 LW** |
| 別の段 | 死んだ code の削除 2、重複 3、長い関数 約 25（vmspace_fault 6〜8、sync_range_buffer 4、cache_read_missing 3、ほか 2 ずつ） | 約 30 |

**確かめ**（guardrail.md:62: 意味を変えない refactor は build（warning 0）と最後の boot test だけ）
- **各段:**
  - `make -j16` で warning 0 の build を 3 つ: amd64（LTO）、gcc で LTO なしの sparcv9 か x68k、object の一覧を持つ pcat。
  - 照合の script で、全部の関数の本文が同じであること。
  - vmunix の `nm` を比べ、増えた global が外に出した物だけであること。
  - 関係する host の試験: `sh plan/ws213/tests/kd-host-test.sh`、`pin-host-test.sh`（どちらも最後が PASS）。
- **最後:**
  - 11 の platform を全部 build します。
  - T1 に頼む起動の確かめ（1 つの QEMU でまとめて流す）:
    - `plan/tools/boot-test.sh` の uefi-usb（amd64、8 GiB）と bios-ide（pcat）
    - `plan/tools/pc98-boot.py`
    - `plan/tools/ports/` の riscv32・m68k-virt・ppc32・ppc64・sun4u の起動の回帰
  - 任意: `plan/tools/process/guest-vfork.sh`（fork の COW）、LTO なしの build の速さを比べる `plan/tools/kbench`。
- **swap の圧力を確かめる道具がありません。** swap の圧力や reclaim の回帰を見る試験は master.md の Tools に登録されていません。長い関数を直す段では、そうした試験を別に作る必要があります。

---

## 6. ユーザーに聞く判断（既定に ★）

1. **規約の扱い:**
   - ★(a) 移動の段に WS106 と同じ「配置変更だけの例外」を承認してもらう。static を外すことと配置以外は変えず、規約は S8 で全文に合わせる。
   - (b) 各段で全文に合わせる（約 +40% の手間）。
2. **内部の header:**
   - ★(a) object-internal.h・reclaim-internal.h・vmspace-internal.h の 3 つ。
   - (b) 1 つの vm-internal.h（同じ名前の物を先に変える必要がある）。
3. **swap.c:**
   - ★(a) vm/ に移して 4 つに分ける（S6）。
   - (b) 移すだけ。
   - (c) 残す。
4. **vm-device.c:**
   - ★(a) vm/device.c に移す。WS113・WS214 の試験は Q1 が、WS141 の試験はその session が直す。
   - (b) WS141・WS214 が終わるまで残す。
5. **公開の header:**
   - ★(a) 今回は変えない。
   - (b) VM の中だけで使う約 55 の宣言を内部の header へ移す（S7）。
6. **外に出す helper の名前:**
   - ★(a) 移動の段は今の名前で、S8 で接頭辞を付ける。
   - (b) 移す時に付ける（試験の名前の list も同時に直す）。
7. **4 章の別の段:**
   - ★(a) 分割の後に、死んだ code と重複（約 5 LW）だけ続けて行う。長い関数は swap の圧力の試験を用意してから別の WS で行う。
   - (b) 全部を後で決める。
8. **pmem.c・uncached.c・cache.c・tmpfs-pages の配置:**
   - ★(a) 移さない（1.4 の根拠）。
   - (b) pmem.c の kern_pmem_* の部分だけ vm/ へ分ける。

### Critical Files for Implementation
- /home/awe/zedBSD-claude1/src/kern/vm.c
- /home/awe/zedBSD-claude1/src/kern/vmspace.c
- /home/awe/zedBSD-claude1/Makefile（KERN_NET_SOURCES の手本 771-805）と /home/awe/zedBSD-claude1/platform/amd64/vmunix.mk（と他の 10 の vmunix.mk）
- /home/awe/zedBSD-claude1/plan/ws213/tests/pin-host-test.sh と /home/awe/zedBSD-claude1/plan/ws213/tests/kd-host-test.sh
- /home/awe/zedBSD-claude1/plan/coding-style.md（2〜3 節）と /home/awe/zedBSD-claude1/plan/standards/ws106-relocation.md（例外の手本）
