# ws197-p007 設計 2.1a の X 節の確かめ（第 4 回、2026-10-11 design-reviewer、Q1 が要約して写した）

## 総評

blocker 0。review-3 の blocker（Stopped 26・27）と X 節の major 4 は形として解けた（X5 と core の受け渡しは usb.c:8114-8132・897-906・7118-7120 と合う、Stopped を EIO にしない、lock の順 event_lock → active_lock は 2602 と合う）。新しい major 4 は状態表の行と数文で直る仕様の抜けで、方式は変えない。**設計 2.1b として 4 つを足せば p007a1 に入ってよい（再 review 不要、Q1 が差分を確かめる）。** minor は p007a1 の中で直してよい。p007a0 の結果は開始の条件のまま（p007a1 の中身は依存しない）。

## 指摘

| 重さ | 箇所 | 問題 | 推しの直し |
| --- | --- | --- | --- |
| major | 状態表（持ち主の command の失敗） | Stop が 19 で返る、Error state で効かない、5 秒 timeout で `command_failed`（pci-xhci.c:1478-1480）以後全 EIO（1250-1263）の遷移が無い。STOPPING のままなら止まり、諦めて URB を返すと DMA の安全が崩れる | **RETAINED** の状態（FIFO と DMA を持ったまま、enqueue は EIO、dequeue・quiesce はエラー、外すのは `dma_quiesced` の後の `xhci_controller_drain_requests` だけ、今の `cancelling = 2` 4447-4463 に倣う）。19 なら context を読み直し `drv_xhci_cancel_action`・`drv_xhci_recovery_action`（lifecycle.h:113・148）でやり直し。各行に「失敗 → RETAINED」、host 試験に 19 と timeout |
| major | enqueue と X10（suspend の GATED） | GATED の間 enqueue は doorbell を鳴らさず（4215-4216）、wait_idle 失敗の `xhci_suspend_end`（7286-7307）も鳴らさない → 空の FIFO で Underrun の後に積んだ TD が動かず、FIFO が満ちて永久に止まる。bulk OUT にも同じ穴の見込み（推測、BUG-289） | wait_idle 失敗の道で `xhci_suspend_end` の前に `xhci_resume_endpoints`（7240）。isoch の FIFO が空でなければ GATED の前に EBUSY。表に「GATED の間も doorbell を鳴らす（HALTED の時だけ鳴らさない）」。QEMU ktest に「流している間の suspend の失敗で流れが続く」 |
| major | X6（isoch worker の生き死に） | 作る・止める場所（attach・detach（6365 以降は port_worker だけ）・`xhci_quiesce` 5475-5531・suspend/resume・`xhci_reinit_forget_devices`）、endpoint の探し方、`xhci_device_release`・`xhci_endpoint_disable`・`xhci_endpoint_reset`（3396 以降・3319-3390）との競りが無い。古い `d` の use-after-free（推測） | worker は `xhci_submission_enter` 相当の数を持つ（`operation_enter` は suspend の間眠るので使わない 3715-3725）。endpoint は毎回 active_lock の下で探し直し quiescing・slot_disabled・enabled を確かめる。stop は `dma_quiesced` と reinit の解放より前。endpoint_disable は NORMAL で FIFO が空の時だけ、endpoint_reset は isoch で EINVAL、drain_requests は isoch_state を見ずに外す（5291-5295 の trap に isoch の印を足さない） |
| major | X6・S8（優先度 4 の回り待ち） | `command_ex` は `sched_yield` で回って待つ（1231-1243）、sched_yield は同じ CPU で最高優先を選び直す（sched.c:624-637・1951-1970）→ 優先度 4 の worker が回る間、command を持つ優先度 8 の thread が走れない（CPU 1 つで逆転）。SCO の thread の NORMAL 待ちや `drv_usb_urb_drain`（usb.c:2593-2604）も同じ | 優先度 4 の thread は低い優先度の持ち物を sched_yield で回って待たない（sleep 1 tick 以上）。worker の command は command_busy を待つ間は既定に下げるか sleep。T1 の ktest に `-smp 1` |
| minor | X4（X3 の後の consumer） | X3 の後は TD k から後ろが残るので空ではない | X3 の後の consumer は context の TR Dequeue、producer に合わせるのは X5・X6 の dequeue_all の後だけ。試験は「空き = 256 − 1 − 残りの TD」 |
| minor | X9・X11（command の poller が claim した isoch の完了） | `command_ex` の poll（1379-1381）が claim した isoch の完了を drain するのは `xhci_irq` だけ、終わりの `xhci_completion_drain`（1495）は普通の list だけ、suspend の `completion_dispatch_busy`（6917-6920）と quiesce の待ちに isoch の list が無い | command の poll の中（event_unlock の後）と終わりでも isoch の list を drain、dispatch の印を別に持ち wait_idle と `xhci_device_quiesce` に足す |
| minor | 状態表の Stopped | 28（Stopped - Short Packet）が抜け | 26・27・28 を同じ行で |
| minor | X5 の dequeue(X) で X が FIFO に無い | 完了待ちの list に居ると EBUSY（4538-4543）で endpoint 全体の cancel が起きない | FIFO が空でなければ全体の cancel、X は EALREADY。usb.h に「EALREADY でも endpoint は空になる」 |
| minor | IRQ からの worker の起こし方 | lock の下で起こす形になっている（今は event_lock の外: 1418・3036） | 印だけ立て `kernel_notify_task` は event_unlock の後 |
| minor | enqueue（NORMAL で hardware が Halted） | HALT_PENDING を見逃して NORMAL に戻ると EBUSY が続き音が切れる | enqueue が Halted を見たら HALT_PENDING に移して worker を起こす |
| minor | `xhci_completion_finish` の log | COMPLETE でない時・residual が 0 でない時に毎回 kern_logf（2757-2787）、isoch で 1 秒に数百行 | isoch は log せず数える（S9 の counter） |
| minor | X12 | host 試験では major 2〜4 が見つからない | QEMU ktest に「流している間の suspend の失敗」「流している間の抜き差し」「`-smp 1`」 |
