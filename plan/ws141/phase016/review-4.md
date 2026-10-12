# ws141-p016 review-4（design-reviewer、対象 v4 2a191ca8c）

判定: A0 は今すぐ始めてよい。A1 は C-1・H-1・H-2 を phase.md に書き足してから（数行の加筆、全体の再レビュー不要）。H-3 は A2 と受け入れ A の前に Q1・ユーザーの判断。M 以下は実装と試験の中で直せる。

## 致命的
C-1. executed・required が全体で 1 本のままで session ごとの列と矛盾。phase.md:41-42・:51 は v3 の全体の queue・sequence・executed++・required ≤ executed のまま、:141 も同じ、:131 は「PARKED の marker を execution の entry に」でどちらで判定するか未決。round-robin では全体の番号の順に終わらない（A の #5 が B の #3 より先）→ 全体の executed が 3 で B の marker（required=3）が未実行のまま完了 → vkResetFences が EBUSY→DEVICE_LOST、compositor は client の acquire fence を poll して（wayland/display.c:49-97）書き途中の buffer を合成。
直し方: sequence・executed・required を session ごと（session->submitted・executed）か entry 方式に一本化し :41-51・:141 を書き換え。drain・stop_poll・recovered も session 単位。試験 (q) に「他の session が先に進んでも自分の marker は自分の execution の完了まで完了しない」。

## 重大
H-1. session をまたぐ wait の仕組み（:124、A1、試験 (r)）は存在しない機能が前提。semaphore・fence は session の中だけ（vulkan-sync.c:131）、import の payload は ENOTSUP（:139-141）。libvulkan が出し入れするのは OPAQUE_FD の fence だけで semaphore の import は無い（external-fence.c:66,113）。fd は CPU で待たれる（compositor の poll display.c:49-97、GPU_DISPLAY_PRESENT_SYNC は ioctl の中で mutex なしで待つ gpu.c gpu_dependency_ioctl 5843〜）。WSI は factory の revision 2 以上なら early commit で acquire fence 付き（wsi-wayland.c:711-721,826-827、A0 の確認はこれで済み）。→ session をまたぐ順序は marker の CPU の待ちだけで保たれ、kernel の依存の記録・列の飛ばし・循環の期限は不要（dead code、偽の DEVICE_LOST と順序の崩れの危険）。
直し方: :118 の「wait の依存を満たしている」、:124 の機構、:126 の循環の検出、A1 の「session をまたぐ wait」、試験 (r) を削る。「session をまたぐ順序は marker の完了（C-1）だけに依る」を不変条件に。(r) は「early commit の client と compositor の poll で、compositor は client の marker の前に buffer を使わない」に。

H-2. 背圧（N1）が件数だけ。:129 は session の件数だけを RESERVE に。bytes は session 48MiB だが 1 submission 最大 8MiB（vulkan-queue.c:21）で 12×8=96MiB、正当な compositor が件数より先に bytes で当たる。全体（64・192MiB）は他 session の負荷でも超える、session の数に上限なし（render-device.c:156）。どちらも decoder の OOM → compose.c:2660-2663 → server->failed（N1 の再発）。全体を RESERVE の EAGAIN だけに移すと compositor が他 session の完了を待つ（受け入れ A に反する）。
直し方: (a) RESERVE で bytes も判定（保留中の bytes＋8MiB ≤ session の上限、または session の上限を件数×8MiB）(b) 全体の上限は RESERVE で判定し session ごとに最低の枠（例 2 件・16MiB）を保証、session の数にも上限 (c) 件数の減算は (3) の guard の下で (4) の finish_request より前に（でないと marker の完了直後の RESERVE は通っても decoder で OOM の競り）(d) 推測: kernel の大きな割り当ては hal_pmem_alloc の連続の物理 page（src/kern/entry.c:160-163）、全体 192MiB を 8MiB の連続で使うと 1GiB 未満の連続 RAM の V3D・display の buffer（share.c の 0x3fffffff の制限）と取り合う恐れ → M の実測でもっと小さく。

H-3. 受け入れ A は A1・A2 だけでは達成できない。compositor の main thread の client buffer の import（render_import render-device.c:541-546）と blob の作成（allocate_resource :719-750）は import_buffer の中で bcm2711_v3d_memory_map（:794）。v4 では map の ioctl は窓の終わりを待つ（:147）。App Home を開く時は import が続く。lazy map は「A2 の後で検討」（:148）だけで段の表（:170-176）に無い → 他 session の窓の長さだけ main loop が止まる。
直し方（Q1 が決める）: lazy map（または map の待ちを main thread から外す）を段 A3 として表に入れ受け入れ A の前提にする／受け入れ A を「import・blob の作成の時を除く」に限る／窓の長さに上限。

## 中
M-1. 優先の規則と受け入れの文言の食い違い、1 歩の長さに上限が無い。:112「他の session の pass の後ろに並ばない」と :119 の交互の実行。compositor の frame が k pass なら他の 1 歩を最大 k 回待つ。GPU は監視の上限（JOB_PROGRESS_WINDOWS×0.5s）まで、窓の前の CPU の変換・CPU だけの段（bcm2711_vulkan_buffer_copy_run、vulkan-native-job.c:172-184）も mutex の下で上限なし。decoder の FIFO は全 session で 1 本で、他の app の pipeline の作成（shader の compile、vulkan-pipeline-build.c:164-166）が mutex の下で走り compositor の QueueSubmit の decode はその後ろ。
直し方: 段 M で 1 歩の最大（GPU・窓の前・CPU の段）と decoder の opcode ごとの最大を測る。A・B の文言を「自分の仕事＋自分の歩の数×他の 1 歩の最大」に、「数十 ms」は実測の後に。

M-2. 表示の session の判定の記録が source に無い。struct bcm2711_buffer（buffer.h:27-61）に display の import の記録なし、display の import_image（display-device.c:1427-1451）は retain だけ。game mode は client の buffer を display に直接 import（libkeiland-backend-zedbsd/scanout-zedbsd.c:173-176、wayland/scanout.c）→ game・video の client が表示の session になる（content type を宣言して全画面にすれば誰でも）。印が sticky なら game mode の後も残る、表示の session が 2 つで compositor の保証は 2 歩。「書く execution」の判定は decode の時に書き込み先（attachment・copy/blit/clear の先・storage の descriptor）の列挙が要るが方法が無い。
直し方: buffer に display の import の数（buffer の guard の下）。印の寿命（execution ごとか session に残るか）と複数の表示の session の扱いを決める。game mode の client に優先を与えてよいかは Q1 の判断。未確認: 一般 process が display の node を開けるか。

M-3. slot（N2 の残り）。「16 のまま足りる」（:131）は未論証。RESERVED と QUEUED の marker は slot を使う（render-worker.c:557-573、supervised の上限 8）。A1 では executor が歩の間 mutex を持つので decoder は FIFO を減らせない。9 個以上の session が同時に command → worker_submit の EAGAIN（render-worker.c:159-163）→ runtime_submit（render-runtime.c:251-259）→ libvulkan の vulkan_kernel_error で DEVICE_LOST（context.c:557-579）。
直し方: runtime_submit を mutex の外で retired を待って再試行に、または session の数に上限。entry 化: 細工した stream は QueueSubmit なしに RESERVE と COMMIT を重ねて 1 entry に複数 marker → entry に list、node は RESERVE の時に用意（decoder で割り当てない）。session に未完了の execution が無ければ marker は decoder でその場で完了、と明記。:43（全体 16・session 2）と :131 の食い違いを消す。

M-4. mutex の待ちの数を知る手段が無い（N7）。kernel の mutex は待ちの数を出さない（include/kern/lock.h:78-83、waitq.h:22-27）。driver の中に数を持ち、controller の mutex を取る全部の所（render-device.c 9 か所、render-runtime.c 3 か所、render-worker.c 1 か所）を wrapper に変えると書く。kern/lock.c を変えるなら Q1 の判断。

M-5. A0 の点検の範囲が狭い（推測）。EBUSY と短い timeout だけでなく「submit の後に fence を待たずに結果を読む・資源を使い回す」誤りも。import.c:348-352 と shm.c は vkQueueWaitIdle で正しい。libglesv2・libegl・libmedia（queue 2 本 vkvideo-runtime.c:677,692）にも grep を広げる。具体の誤りは未発見。

## 軽微
L-1. stop は guard だけの文脈（render-worker.c:305-342）。表の「取り消し: 実行せず dispose」（:137）は「印を付けて起こし、executor が mutex の下で dispose」と明記（fault と同じ形）。
L-2. 受け入れ B は (B) の設計に依るが未設計（L5）。B の判定は (B) の版の review の後、と表にも。

## review-3 の判定
N1 部分的（H-2）。N2 部分的（M-3）。N3 解決（fence は SIGNALED で作られる cursor-plane.c:362）。N4 方針は解決（H-3・M-1 が残る）。N5 解決（遅延解放の view は space->views に残り quarantined で recover の条件 v3d-memory.c:274-278 を満たす）。N6 解決。N7 原理は解決（M-4）。N8 概ね解決（C-1・L-1）。N9 追加された（(r) は H-1 で差し替え）。L-a・L-b・L-c 解決。

## 問題が無いと確かめた点
歩への分割は可能（pass は 1 歩ごとに作成・実行・解放 vulkan-native-job.c:150-330、remaining も歩ごと、保存が要るのは event・region・計測）。libvulkan の RESERVE の EAGAIN は VK_NOT_READY を経て capacity の待ち（sync.c:636-645、queue.c:344-366）。1 transaction は QueueSubmit と marker が 1 つずつ、WSI の present も同じ（wsi-swapchain.c:2220）。cursor の image は 4 以下で compositor の同時の execution は 12 を超えない。gpu.c の完了に全体の順序の前提は見当たらない（推測）。HAL・UAPI の layout の変更不要。

## 確かめていないこと
一般 process が display の node を開けるか。hal_pmem_alloc の物理の範囲。V3D の MMU が無効な PTE を TLB に持つか（lazy map の可否）。1 歩の実際の長さ。実機・QEMU は未実施。
