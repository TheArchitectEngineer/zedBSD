# ws141-p016 review-1（design-reviewer、対象 fc150c810）

結論: 前提が source と合わない。(A) は目的（vkQueueSubmit を GPU の実行から切り離す）を達しない。(B) の実機の受け入れは (A) に依存。実装の前に作り直し。

## 致命的
F1. vkQueueSubmit の遅さは mutex でなく decoder の同期往復。libvulkan の queue_attempt は device->mutex を取り（userland/desktop/libvulkan/queue.c:422）、vulkan_command_execute(..., VK_TRUE)（queue.c:460）で reply を待つ（context.c:750、GPU_COMMAND_SUBMIT の後 vulkan_decoder_wait context.c:847,872）。kernel は QueueSubmit の reply を run_submission の後＝GPU の実行と retire の後に書く（src/drivers/gpu/bcm2711/vulkan-queue.c:131）、通知は finish_request（render-worker.c:273）。mutex を外しても libvulkan は自分と FIFO の前の分の実行を待つ。全 Vulkan command が同じ FIFO。速くなるのは FIFO を通らない ioctl だけ。phase.md:72「libvulkan は変わらない」は誤り。compositor の cursor の present は main loop の plane_present（userland/desktop/wayland/cursor-plane.c:392-478）なので、frame の submit が戻らない間 (B) も効かない。
直し方: 先に計測（vkQueueSubmit を ioctl の戻りまでと decoder の待ちに分ける）。作り直し: QueueSubmit は prepare（検証）の後に reply を返し、GPU の実行は decoder の FIFO と別の executor へ。完了は job marker か fence、DEVICE_LOST は fence・sync の状態で。kernel と libvulkan の private wire の意味が変わるので両側の変更として計画。

## 重大
F2. 旧来の同期 GPU_COMMAND 経路（runtime_command render-runtime.c:117-178 → bcm2711_vulkan_dispatch → run_submission → native_job_execute vulkan-queue.c:543）でも mutex が外れる。窓の間に同じ session の runtime_submit が FIFO に入れ、worker が arena.used = 0（vulkan-stream.c:97）で巻き戻して旧来の側の queue_submission（vulkan-queue.c:112）・command buffer・sync を上書き。GPU_CAP_COMMAND は公開（render-runtime.c:60）で userspace から届く memory 破壊。phase.md:57 も不成立。
直し方: mutex を外すのを worker の thread からに限る（worker_step→execute_command から flag）、または旧来の経路で QueueSubmit を拒否、または session に「旧来の経路が実行中」の印を置き worker_submit・take_request が待つか EAGAIN。

F3. pass_run（vulkan-native-execute.c）は共有の物に触る: memory_written(output)（:109、v3d-memory.c:309 は device の mutex が前提）、output->synced_*（:115-119）、snapshot_filled（:371）、pass->target->layout（:130）、space->deferred・view->references を読む（:156）。窓を待たない render_write（render-device.c:432）と競合し generation・ring が壊れ、snapshot の再利用（native-storage.c:377）・部分写し（vulkan-native-image.c:600-610）が誤る。display の resource_write は別の mutex で generation++（display-device.c:580）、既存の競合で (B) が判定に使うので重くなる。
直し方: pass_run を「GPU の実行」（mutex なし）と「出力の公開」（mutex を取り直した後）に分ける、または render_write も窓を待つ。generation と ring は buffer の guard か atomic。「実行の区間で触る物」の一覧を直し、host 試験の対象に。

## 中
F4. mutex を持ったまま窓を待つと、memory_map（blob の作成 render-device.c:794・import）・memory_release（resource の破棄 :362・close）が 1 つ来ただけで全 ioctl が job の終わりまで止まる。kernel の mutex は handoff しない（src/kern/lock.c:311）ので worker が取り直せない時もある。
直し方: mutex を取る前に mutex なしで窓の終わりを待ち、取った後に再確認（立っていたら外して待ち直す）。PTE の待ち手の印で worker の次の窓を譲らせる、または PTE の解放を worker の解放の区間へ遅らせる。

F5. 待ちの上限の根拠が違う。CONFIG_GPU_JOB_EXECUTION_MS は common の watchdog（gpu.c:56、60 秒）。bcm2711 の実上限は JOB_PROGRESS_WINDOWS(4)×0.5s（v3d-job.c:20-21）を job ごと（TFU の変換の数＋bin＋render）に重ねた値。render_fault→bcm2711_render_worker_fault（render-device.c:640、render-worker.c:415-445）、IRQ の fault、record_failure（v3d-hardware.c）は hardware.idle を起こさない。
直し方: 上限を実値で書く、fault の全経路で hardware.idle を wake_all、待ち手は faulted・!ready を先に見る（EIO 優先）。

F6. (B) の fast path が先の present を追い越す。compositor は image ごとに held[]（cursor-plane.c:419-455）。重い frame の間に S2 を image A に写す present（worker で GPU 待ち）→ S1 を持つ B の位置だけの present が即時 SET → 後から A の SET が来て S2 が残る。
直し方: 同じ lease・queue の present worker に未処理が無い時だけ fast path。他は worker が順に（software の待ちだけの job は GPU を通さず即時）。

F7. (B) の kernel の「同じ元なら写さない」の鍵が足りない。handle は session ごとに 1 から（display-device.c の next_resource）、compositor の再起動で同じ handle・offset・generation になり得る。幅・高さ・stride・format が無い。host_mapped（render-device.c:459・display-device.c:607）経由の書き込みは generation を進めない（buffer.h:40-45）。snapshot は host_mapped を信用しない（native-storage.c:357）。
直し方: 鍵を buffer->identity（buffer.c:101）＋offset＋幅・高さ・stride・format＋generation、cursor_reset（display-device.c:400,746,787）で消す、host_mapped は常に写す、generation は atomic に読む。

F8. (B) の libvulkan の範囲の過小見積もり。swapchain の image は OPTIMAL の普通の allocation（wsi-swapchain.c:1201-1251、export しない）。display への import には external memory の create info・export・大きさの一致（share.c:287）・placement・color attachment の usage の両立が要る。image の group は chain の間で共有され得る（wsi-swapchain.c:1252、oldSwapchain）。fast path は acquire への返却・present の fence と sequence・semaphore の software payload の消費（device->mutex の下、queue.c:466-475 と同じ）を自分で行う。
直し方: 列挙して設計に。p015（UIF）との関係も。

F9. 試験。vulkan-device-host-test.c は単一 thread の fixture（mutex_lock は assert で再帰・競合を止める :558-580、waitq_sleep は worker.retired だけ :531-555）。並行の検査は fixture の作り直しが要る。kernel の host 試験では F1・F2 を検出できない（F1 は libvulkan 側の計測、F2 は旧来の経路と worker の同時の場面が要る）。
直し方: 試験を計画に足し、F1 の計測を実装の前の Phase に。

## 軽微
- 「worker の queue は 8 個（BCM2711_RENDER_RESERVATIONS）」→ slot は 16（BCM2711_RENDER_REQUESTS）、うち 8 が marker 用（render-worker.h:16-17）。
- memory_release は参照の減算と quarantined = true を ready の判定より前に行う（v3d-memory.c:216-226）。待ちは unmap・flush の前、待った後の EIO は今と同じ quarantine と明記。
- 窓は単一の持ち主を assert（既に立っていたら trap）。
- 1 つの stream の中で submit の後の command が途中で消された resource を見得る（Vulkan の範囲内）ことを明記。
- pass_run の後の perf の加算（vulkan-native-job.c:266-284）は mutex を取り直した後に。

## 問題が無いと確かめた点
- deadlock: pass_run は hardware.guard 以外の lock を取らない。spin lock だけで窓を下ろしてから mutex を取る順で成り立つ。
- lost wakeup: waitq_sequence/waitq_sleep の型（render-worker.c:398-403）に倣えば起きない。
- close: drain が mutex の外で先、sessions の減算は後（render-device.c:207-216、gpu.c 1339→1351→1364）。
- reset: sessions が 0 が条件、EBUSY の追加は無害。
- finish_job の faulted で job_busy を下ろす（v3d-job.c:287-307）、永久待ちなし。
- suspend の op 無し。hal.h の変更不要。GPU_DISPLAY_CURSOR_SET は不変（ただし F1 の直しは private wire の意味を変える）。i915・Venus は surface->cursor で bcm2711 に限れる。

## 確かめていないこと
- GPU_COMMAND が gpu.c で ops->command に届くこと（推測）。200ms の内訳（未計測）。compositor の frame の submit が plane_present と同じ thread（pthread_create が無いことからの推測）。実機・QEMU は未実施。
