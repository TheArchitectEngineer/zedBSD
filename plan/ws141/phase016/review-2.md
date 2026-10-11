# ws141-p016 review-2（design-reviewer、対象 v2 88bc7b40c）

結論: 方向（QueueSubmit を prepare の後に返し、実行は別の executor）は F1 への正しい答え。ただし C1・H1〜H5 のため、このまま A1・A2 を実装すると壊れる。v3 で直してから実装。

## 致命的
C1. 窓の間に decoder・旧来の経路・close の側で PTE が編集される（A2）。phase.md:59 は「PTE を編集する ioctl」だけを対象にしている。実際は decoder の vkFreeMemory／最後の参照を落とす vkDestroy* → release_memory → bcm2711_v3d_memory_release（vulkan-memory.c:340,354）。memory_release は ready の判定より前に参照を減らし quarantined=true（v3d-memory.c:216-226）、その後 memory_ready が job_busy を見て EIO（:445-449）。render_close は session_close を mutex の下で呼び、失敗で bcm2711_render_fail＝GPU 全体の障害（render-device.c:219,228-235）。旧来の runtime_command（render-runtime.c:146-169）も mutex を取る。窓は executor が mutex を外して GPU を回す間＝decoder が mutex を取って進む時間。compositor の client buffer の解放（import.c:410、shm.c:948）が EIO → view の quarantine → transport の失敗 → DEVICE_LOST。close が他 session の窓と重なると render_fail。さらに memory_ready は job_busy しか見ず、job_busy は 1 回の v3d_job_run の間だけ（v3d-job.c:278,299）なので、同じ pass の TFU と CL の隙間で PTE の編集が通る（phase.md:93 (4) は不成立）。
直し方: memory_ready を hardware.window で判定。窓の間に mutex を持つ者が参照 0 にした view は unmap せず遅延解放の list へ（references=0、quarantine しない）。窓が閉じた後に mutex を持つ者（executor の公開の後、または次の decoder の step）が一括で unmap・pages_sync。map は executor の準備の区間と ioctl だけ（decoder は map しないことを確認済み）。render_close・render_destroy にも「mutex の前に窓の終わりを待ち、取った後に再確認」を明記。mutex を持ち window==false を見た者がいる間は executor は新しい窓を開けない。試験に「窓の間の vkFreeMemory・vkDestroyImage・別 session の close」。

## 重大
H1. idle の保留（phase.md:40）は不要で実装できない。libvulkan は GPU_OP_QUEUE_WAIT_IDLE・DEVICE_WAIT_IDLE を送らない（使うのは host 試験と i915 だけ）。vkQueueWaitIdle・vkDeviceWaitIdle は空の submit＋private fence＋marker の待ち（libvulkan queue.c:203-236）。kernel の wait_idle は stream の中の 1 command（vulkan-device.c:468-509）、stream の状態は stack（vulkan-stream.c:52-84）で後ろに trailer などが続く。「transaction は 1 つずつ」は libvulkan の context->mutex（context.c:496）の振る舞いで kernel は保証しない。worker_submit は同じ session の複数 request を受ける（render-worker.c:152-186）。保留すると細工した client の後続 request が arena.used=0（vulkan-stream.c:97）で再開の前提を壊す（F2 と同種）。また A2 の後は wait_idle が job_busy を DEVICE_LOST とみなす（vulkan-device.c:500）ので窓の間に偽の DEVICE_LOST。
直し方: idle の保留を外す。WAIT_IDLE は ENOTSUP で断るか、job_busy を見ず「executed が最後の番号に達していれば SUCCESS、でなければ待たずに NOT_READY 相当」の即時判定、wire に未使用と明記。「request ごと保留」の機構は作らない。

H2. 保留できる execution の数に上限が無い。1 submission は CPU snapshot を最大 8 MiB（vulkan-queue.c:21、vulkan-prepared.c:16）、command・descriptor set の pending と sync の参照も保持。1 stream に QueueSubmit をいくつでも並べられる（vulkan-stream.c:96-103）。一般 process から kernel heap を使い切れる。
直し方: session ごとと全体の execution の数・bytes の上限。decoder は mutex を持って眠らないので、runtime_submit で EAGAIN＋capacity の通知で再試行、または reply に VK_ERROR_OUT_OF_HOST_MEMORY。試験に入れる。

H3. 全体で 8 個の supervised marker（render-worker.h:17、reservation_capacity render-worker.c:832-860）が新しい行頭の詰まり。libvulkan は vkQueueSubmit ごとに 1 つ予約し、足りなければ capacity を待つ（queue.c:345-366、sync.c:620-628）。v2 では marker が完了まで保留されるので、重い job の間に compositor の frame・present・cursor・client の submit で 8 個が埋まり、どの vkQueueSubmit も戻らない。
直し方: M で marker の保留数を測る。session ごとの枠（compositor 分の確保）か、上限を H2 と分けて増やす。「marker の枠が埋まっても compositor が止まらない」を受け入れ条件と試験に。

H4. cursor plane が非同期化で閉じる（A3 の一覧の漏れ）。plane_present は前の copy の fence を 100 ms 上限で待ち、VK_SUCCESS 以外で「failed step=copy」→ kwl_cursor_plane_close（cursor-plane.c:428-454）。v2 では重い job の後ろで VK_TIMEOUT → plane が恒久的に閉じ、cursor が composed に落ちる。main loop も present ごとに最大 100 ms 止まる。
直し方: A3 の対象を bcm2711 の同期の意味でしか試されていない userland（少なくとも cursor-plane.c）に広げる。VK_TIMEOUT・NOT_READY は「今回は present しない（次で再試行）」、待ちは vkGetFenceStatus の非 blocking。(B) の fast path と整合。

H5. 公開の順序が無い。今の run_submission は予約（signals_reserved・waits_reserved・pending、fence の pending）を execution の始めに公開（vulkan-queue.c:526-537）、完了の公開は 559-581、release_submission（dispose → prepared_release で command->pending--・set->pending--、vulkan-prepared.c:94-108）はその後。libvulkan は marker の直後に vkResetFences（queue.c:842-873）、kernel は pending の fence を EBUSY（vulkan-sync.c:332-333）。command buffer の reset・begin、descriptor の更新・解放、pool の reset、pending 中の再 submit も EBUSY（vulkan-command-buffer.c:69,185,243、vulkan-descriptor-update.c:429、vulkan-descriptor-sets.c:294、vulkan-command-pool.c:210、vulkan-draw.c:48）、EBUSY は transport の失敗で DEVICE_LOST。
直し方: 順序を明記 — (1) 予約・pending・fence の pending の公開は decoder が enqueue の時に mutex の下で (2) executor は completed・signaled の公開と release_submission を mutex の下で終える (3) その後 guard の下で executed++ し保留の marker を集める (4) guard と mutex の外で finish_request。試験に「marker 完了の直後の vkResetFences・vkResetCommandBuffer・vkUpdateDescriptorSets が成功」。semaphore の pending を数にするなら publish_failure の sync->pending=false（vulkan-queue.c:640-653）も「この execution の分を減らす」に。

## 中
M1. 保留の marker の slot の状態機械が無い。finish_request は ACTIVE でなければ trap（render-worker.c:939-940）、worker_fault・worker_stop は QUEUED・RETAINED だけ（:324-334,455-459）、recovered は FREE だけ（:484-489）、bcm2711_render_fail は worker_step が mutex の外で呼ぶ（:289-293）。
直し方: 新しい状態（例 PARKED）と fault・stop・drain・recovered・claim_timeline・reservation_capacity での扱いの表。cancel された execution も executed を進め、保留の marker は faulted なら EIO。executor は失敗で mutex の外から bcm2711_render_fail。recovered に executor の queue と保留の list が空であることを加える。

M2. 「PTE の待ち手がいれば mutex を一度譲る」（phase.md:59）は保証にならない。kernel の mutex は handoff せず横取りできる（src/kern/lock.c:311 mutex_spin_take）。
直し方: guard の下で pte_waiters>0 なら窓を開かず、mutex を外して pte_waiters==0 まで眠る（waitq の sequence の型）。待ち手は編集の後に減らして起こす。decoder と executor の mutex の飢えも M で測る。

M3. fault の cancel を誰がどの lock で。render_fault→worker_fault は mutex を取らない文脈（render-device.c:632-640）、dispose は mutex が要る。
直し方: fault の経路は印を付けて起こすだけ、dispose は executor が mutex の下で。executor は各 execution の始めに faulted・stopping・uncertain を確かめる。

M4. 「出力の公開を後へ」の一覧の誤りと漏れ（phase.md:58）。space->deferred・view->references の読みは validate_pass の検証（vulkan-native-execute.c:155-157,171-178,217,233-267）で、窓の前に mutex の下で行うべき（後へ移すと検証なしで launch）。snapshot_filled は TFU の loop の中（:369-371）。display の resource_write の generation++ は別の mutex（display-device.c:580）で controller の mutex と直列でない（render_write は既に直列、render-device.c:426-432）。
直し方: 「窓の前に mutex の下（検証・CPU の clear）」「窓の中（v3d_job_run だけ）」「窓の後に mutex の下（公開）」の 3 区分で作り直す。buffer の guard の対象は display-device.c:580 とそれを読む側。

M5. 試験の計画の不足: (a) 窓の間の decoder の vkFreeMemory・vkDestroy* と別 session の close (b) TFU と CL の隙間 (c) execution の上限と backpressure (d) marker の枠が埋まった時の compositor (e) cursor plane の fence の timeout (f) marker の完了の直後の reset・update (g) fault で保留の marker が EIO で終わり render_fail が届く (h) checked stop（render_stop_poll）と保留の execution (i) 細工した stream（1 request に多数の QueueSubmit、同じ session の複数 request）。本物の thread の fixture では interleave が再現しないので、executor_step・worker_step を外から順に呼べる決定的な fixture に。実機で何を見るか（窓の間の解放、App Home 中の cursor）を書く。

## 軽微
L1. A3 の確認項目が事実と違う（phase.md:72）。libvulkan は既に「reply は完了ではない」前提: semaphore の software payload は受理で消費（queue.c:461-473）、WSI の present の readback は fence の待ちの後（wsi-swapchain.c:2772-2786）、WaitIdle は opcode を使わない。要るのは libvulkan の書き換えでなく確認の記録と、userland（H4、EBUSY の厳しさ）の点検。i915・Venus への影響は libvulkan を変えなければ無し。capset の STRICT_QUEUE（context.c:192-195）の意味も保てる。
L2. A1 だけでは executor が GPU の実行の間も mutex を持つので効果は A2 の後。計測を実装の前の Phase に、が同じ Phase の段 M に留まっている。userland の点検（H4）は A1 の前に要る。
L3. review-1 の「1 つの stream で submit の後の command が消された resource を見得る」が v2 に無い（非同期化で範囲が広がる）。phase.md:59 の memory_begin は ioctl ではなく executor だけが呼ぶ（vulkan-native-job.c:160,197,303）。
L4. executor の finish_request は mutex と guard の外で（drv_gpu_complete の再入）と明記。
L5. (B) の F8 は先送り。(B) の実装はその版がレビューを通るまで止める。

## review-1 の判定
F1 方針は解決、効果は H3・H4 次第。F2 ほぼ解決（旧来の経路も窓の間に mutex を取るので C1 の対象、phase.md:53 の「関わらない」は誤り）。F3 部分的（M4）。F4 ioctl は解決、decoder・旧来・close は C1、譲り方は M2。F5 解決。F6・F7 記述としては解決（(B) 未設計）。F8 未解決。F9 部分的（M5）。軽微: 16/8 の slot は H3 で意味を持つ、memory_release の順序 解決、窓の assert 解決、stream の中の destroy 未引き継ぎ、perf 解決。

## 問題が無いと確かめた点
arena から heap へ写す範囲（actions・syncs・queue・fence・device と prepared が持つ command buffer・descriptor の clone、batches は不要 vulkan-queue.c:540-568）。submit 後の vkFree・vkDestroy・vkResetCommandBuffer・再 submit の memory の安全は prepared の retain と pending で保たれる（vulkan-prepared.c:49-66,370-387、vulkan-draw.c:44-49）、ただし EBUSY→DEVICE_LOST（H5）。layout を書くのは executor の経路だけ（vulkan-barrier.c:174、vulkan-native-execute.c:130）。HAL・UAPI の layout の変更は不要。

## 確かめていないこと
GPU の実行中に無関係の VA の PTE を編集して TLB を flush しても安全か。render node を一般 process が開けるか（H2 の重大度）。App Home の 200 ms の内訳。実機・QEMU は未実施。
