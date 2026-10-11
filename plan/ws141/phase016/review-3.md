# ws141-p016 review-3（design-reviewer、対象 v3 42e3b4f95）

結論: C1・H1・H5・L1〜L5 は解決、M1〜M5 は概ね解決。H2・H3・H4 は直し方に新しい誤りがあり未解決。このまま A1 を実装すると compositor が普通の操作で終わるか DEVICE_LOST。
実装の可否: A0 は N3 の直しを phase.md に入れてから開始可。A1 は N1・N2（予約の背圧で上限、slot の数、session ごとの上限の値）を書き足してから（小さな加筆で全体の再レビュー不要）。N5〜N9・軽微は実装と試験の中で直せる。N4 は実装の前に Q1 とユーザーの判断。

## 致命的
N1（H2 の直しの誤り）. execution の上限（session 4）を OUT_OF_HOST_MEMORY で返すと compositor が終わる。libvulkan は OOM を正しく扱う（sync.c:706-217 付近 vulkan_sync_job_finish）が、frame の vkQueueSubmit の失敗で compose_submit→kwl_compose_draw が EIO（compose.c:2660-2663、577-583）→ server->failed=1（display.c:208-210）。cursor の copy・present の失敗で plane を閉じる（cursor-plane.c:448-453、478-482）。compositor の同時の execution: frame 1・WSI の present 1・cursor plane は image 最大 4（compose.h:170）で copy と present 合わせて最大 8（WSI の present は毎回 execution、wsi-swapchain.c:2220）→ 4 を容易に超える（推定）。bytes 32MiB は全体だけで、1 submission 最大 8MiB（vulkan-queue.c:21）なので 1 session が使い切れる。
直し方: 上限は OOM でなく背圧に。session ごとの execution の上限を job_reserve/job_capacity の判定に入れ EAGAIN（libvulkan は RESERVE の EAGAIN で vulkan_sync_capacity_wait、queue.c:345-366、sync.c:636-145）。libvulkan は 1 transaction に 1 QueueSubmit と 1 marker なので marker の session 上限 ≤ execution の session 上限なら OOM を受けない。kernel の OOM は細工の stream への防御だけに残す。上限は compositor の最大の同時数（8 以上）を下回らない。bytes にも session ごとの上限。試験 (c) に「compositor が上限に当たっても終わらず plane も閉じない」。

## 重大
N2（H3 未解決）. 「全体 16」は slot 16（BCM2711_RENDER_REQUESTS 16U、うち RESERVATIONS 8U、render-worker.h:16-17）と両立しない。reservation_capacity は session を取らない（render-worker.c:836-865）。worker_submit も同じ 16 slot、空きが無ければ EAGAIN（render-worker.c:152-164）→ runtime_submit がそのまま返し（render-runtime.c:251-259）→ libvulkan は GPU_COMMAND_SUBMIT の EAGAIN を DEVICE_LOST（context.c:869-873→557-579）。session の数に上限なし（render-device.c:156）。PARKED の marker が全 slot を占めると他の app の command が DEVICE_LOST。client 9 個以上で「最低 2」は保証不能。
直し方: slot を「marker の上限＋command 用（8 以上）」に増やすか、PARKED の marker を slot から外し execution の heap の entry に持たせる。公平性は「session ごとの上限」で（job_reserve・job_capacity は session を受け取る）。compositor を守る枠が要るなら display の present を持つ session を kernel が識別する方法を書く。execution の完了でも drv_gpu_capacity_changed と retired の wake を明記。

N3（H4 の直しの誤り）. plane_present は先に vkAcquireNextImageKHR(...,0U,...) で image を取り plane->next を進め（cursor-plane.c:412-421）、fence の確認はその後（428）。present しないで return すると image が acquire 済みで残り、image の数（最大 4）だけ続くと以後 acquire が常に NOT_READY（414-415 で黙って return）→ plane は閉じず present もせず cursor が止まる。
直し方: 最後に submit した copy の fence を acquire の前に vkGetFenceStatus で見る（queue は 1 本の順なので signaled なら前の copy も完了）、NOT_READY なら acquire しない。または acquire 済みの image の番号を保持して次で使う。再試行は main loop の poll 上限 10ms（main.c:856）で足りる。試験 (e) に「NOT_READY が image の数を超えて続いた後に回復」。

N4. (A) だけでは cursor の 2fps・drag の 5fps は改善しない見込み。cursor plane の present は WSI の present の job を経て、queue ごとの present worker が fence を待ってから native present（wsi-swapchain.c:2776-2782）、present_early でも display は fence の後に flip（2824-2849）。executor は全体で 1 本の in-order。cursor の copy と位置の present は重い client の job の後ろ。compositor は frame を 1 つずつ（in_flight、compose.c:482）で drag は GPU の完了に縛られたまま。client の buffer の import は main thread で vkQueueWaitIdle（import.c:352、shm.c:391・881）、App Home を開く時は新しい buffer が来るので main loop はなお止まる。
直し方: 段 M で A の効果（main loop の非 blocking で何 ms 減るか）を先に見積もる。受け入れを「A: main loop と入力が止まらない」「B: cursor が動く」に分ける。A0 の点検に compositor の main thread の vkQueueWaitIdle（import、shm）と kwl_cursor_plane_close の vkDeviceWaitIdle（cursor-plane.c:117）。目的に効く案: executor を session ごとの queue にして pass の単位で round-robin（cross-session の順序は client の WSI の fence の待ちで保たれる、early commit の時は compositor 側の待ちが要るので要確認）— 範囲の拡大で Q1 とユーザーの判断事項。

N5. 遅延解放の list の view が fault の後に quarantine にならず reset が永久に EBUSY。bcm2711_v3d_memory_recover は全 view に references==0 && quarantined を要求（v3d-memory.c:274-278）、遅延解放は quarantine しない、fault の後は memory_ready が偽で一括の unmap もできない（224-226）、render_reset（render-device.c:645-690）は list を掃除しない。
直し方: fault の経路（bcm2711_render_worker_fault と同じ guard の下）で list の view を quarantined=true に移す、または render_reset で hardware reset の後に回収。試験 (g) に「窓の間の解放＋fault→reset の成功」。

## 中
N6. render_close・render_destroy の「mutex の前に窓の終わりを待つ」は不要で有害（C1 の段の「list に入れる者」と矛盾）。vkFreeMemory は vulkan_resource_destroy→GPU_RESOURCE_DESTROY で render_destroy を同期に呼ぶ（libvulkan memory.c:354、context.c:379-404）、compositor の import の解放（import.c:408、shm.c:944-953）も同じ → main thread が窓の長さ止まる。
直し方: 解放の側は待たずに list へ（窓は mutex を持つ executor しか開けない）。待つのは map の ioctl だけ。map の ioctl 自体も compositor の import（render_import→import_buffer の memory_map、render-device.c:794）で窓を待つので、GPU の VA の map を executor の「窓の前」まで遅らせる lazy map を検討（decoder と ioctl は VA 不要、推測）。

N7. mutex は handoff しない（mutex_unlock は FREE にして 1 つ起こすだけ、mutex_lock_interruptible は mutex_spin_take で横取り、src/kern/lock.c:311 付近と 362-387）。A1 では executor が溜まった execution を全部終えるまで decoder が進まない。CPU だけの execution（buffer_copy_run、barrier、skipped copy、vulkan-native-job.c:174-194、232-236）が続くと A2 でも同じ。逆に map の ioctl が絶えず来ると pte_waiters>0 が続き executor が窓を開けない（DoS）。
直し方: decoder と ioctl の待ちがあれば executor は mutex を外した後に一度眠って順番を渡す（waiting の数と sequence）。pte_waiters は「executor が待ち始める前から居た待ち手」だけを優先する世代番号と上限。段 M に mutex の待ちの最大（submit_lock_max）と executor の待ち。

N8. PARKED の表に execution の側の状態と取り消しが無い。今の publish_failure は bcm2711_render_worker_fault で GPU 全体を fault（vulkan-queue.c:640）、stop の取り消しに使うと 1 client の終了で全 session が DEVICE_LOST。marker の request は decoder が受けた時点で取り消し済みとして完了（render-worker.c:226-229→296）するが、非同期では同じ session の前の execution が窓の中にあり得て「marker の完了 ⇒ 前の仕事は退役済み」が崩れ、外に出した fence の fd（sync.c vulkan_external_fence_prepare_locked）を見る他 process が書き途中の buffer を読み得る。
直し方: execution の状態（QUEUED・RUNNING・取り消し・完了）と stop・fault・close・recovery の表。stop の取り消しは session の中で（sync・fence を DEVICE_LOST、release_submission、worker_fault は呼ばない）。取り消された marker も required ≤ executed まで PARKED（error は ECANCELED）。fault の時は executor の sleep（pte_waiters の waitq）と窓の待ち手の両方を起こす。

N9. 試験に足す場面: (l) 窓の間の map の ioctl（GPU_BLOB_CREATE、RESOURCE_IMPORT、memory_map の EIO v3d-memory.c:125-127 が userland に届く render-device.c:732-737、796-799 → DEVICE_LOST context.c:575-579）(m) N5 の fault→reset (n) N1 の compositor と上限 (o) N3 の image の返却 (p) N7 の飢え。A0 の EBUSY の候補（推測）: compose.c:1958-1969 の spare_sets を kwl_compose_set_get の後に vkUpdateDescriptorSets、backdrop.c:174・456 が frame の in_flight の間に起きないか（非同期の後は EBUSY→DEVICE_LOST、vulkan-descriptor-update.c:429）。i915・Venus で動くことは証拠にならない。

## 軽微
L-a. wait_idle の VK_NOT_READY は vkQueueWaitIdle の正当な戻り値でない。libvulkan が使わない opcode なので EOPNOTSUPP で断る方がよい。plan/ws141/tests/vulkan-device-host-test.c が使うので A1 で直す。
L-b. pte_waiters は map の待ち手だけ数える（N6 で close・destroy が待たなければ問題なし）。
L-c. 「窓を開くのは mutex を持つ executor だけ」の不変条件を明記（C1 の安全の根拠）。

## review-2 の判定
C1 解決（残り N5・N6・N9 (l)）。H1 解決（残り L-a）。H2 未解決（N1）。H3 未解決（N2）。H4 部分的（N3・N4）。H5 解決（execution の完了で retired と capacity を起こすことを足す）。M1・M3 概ね解決（N8）。M2 原理は解決（N7）。M4 解決（buffer の guard は buffer.h:29 に既にある）。M5 概ね解決（N9）。L1〜L5 解決（L1 の点検は compositor の vkQueueWaitIdle にも広げる）。

## 問題が無いと確かめた点
marker は libvulkan の COMMIT（reply の後）で FIFO に入る（render-worker.c:589-629）ので required は自分の execution 以上。PARKED の slot は supervised のまま pending を持つので worker_stopped・recovered・find_reservation・job_cancel/commit（ESTALE）は矛盾しない。窓の中の validate_job は PTE を読むだけ。decoder は map しない（vulkan-memory.c:114-125）。perf は窓の中で書かれない（v3d-job.c:395-414）。HAL・UAPI の layout の変更不要、背圧は既存の GPU_JOB_RESERVE の EAGAIN で作れる。

## 確かめていないこと
compositor の同時の execution の実数。render node を一般の process が開けるか。V3D の MMU が無効な PTE を TLB に持つか（lazy map の可否）。compositor の EBUSY の候補の実際の順序。実機・QEMU は未実施。
