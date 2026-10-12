# ws141-p016 (B) 第 5 版 review-B5（design-reviewer、2026-10-12）

Q1 から受けた要点の写し（原文は Q1 の scratchpad）。対応は [phase.md](phase.md) の「(B) 第 6 版」の表。

判定: B-u 条件付き実装可（lane・ticket・op・fast path の骨格は既存と合う。present_worker_get は queue で引く、present_drain・present_workers_stop は worker の list を全部、swapchain_notify_locked は broadcast、fence は lock なしで GPU_COMMAND_WAIT（sync.c:1418-1470）なので 2 本が同時に待てる。lock の循環なし）。実装の前に中 3（EBUSY）と重大 2（S2・S3 の測り方）を設計に。重大 1 は見込みを Q1・WS213 へ。

重大 1: S1 は B-l 無しでは閾値をほぼ確実に外す。kernel の output_present は controller->mutex を持ったまま flip_settle（display-flip.c:322-360）・flip_compose（display-device.c:862-940、:926）、output_cursor_set も同じ mutex（:1659）。libvulkan も primary の worker が PRESENT_SYNC の間 display_mutex（wsi-display.c:1079-1150、:1118）。→ (a) B-u で present_cursor_at を lease 専用の cursor の lock に（applied_ticket・x/y・chain の frame/sequence をこの lock で、plane->frame は atomic、lease が plane の参照を持つので display_mutex 不要）＝L1 の libvulkan の半分を B-u に (b) kernel 側（CURSOR_SET・MOVE を adoption の待ちの mutex から外す、flip は既に spinlock の guard）は「おそらく必要」として段に置き WS213 の見積もりに。

重大 2: S2 は止まりを検出できない（compositor は copy の fence が NOT_READY の間 acquire の前で戻る cursor-plane.c:499-509、止まりは呼び出しの前で call_ns→done_ns に出ない）。S3 の「depth ≤ image の数」は構造上常に満たす。→ S2 に「形が変わった present の call_ns から次に位置を反映した ioctl の done_ns まで」と「入力が続く間の done_ns の間隔」、S3 は ioctl の間隔と DROPPED の数、WS213 の gate の変更の前後どちらで測ったかを書く。

中 3: EBUSY の再試行は動かない（display_wait_native は kernel の output_wait が待たずに ETIMEDOUT display-device.c:948-980、libvulkan に呼ぶ所なし → 3 回が即尽き NATIVE_WINDOW_IN_USE が latch、plane が閉じる）→ 待ちは refresh_millihz からの nanosleep、3 回で latch せず新しい ticket が来るまで（または上限時間）再試行して DROPPED、native だけの job に印を付け worker の loop で vulkan_fences_wait（wsi-swapchain.c:2779）を飛ばす、lane に渡す前に job->info と indices の写し（:539-545）。

中 4: H-b の payload_mutex 案の穴（queue_attempt は分類 queue.c:513-528 から消費 :465-472 まで round trip を挟んで device->mutex を持ち snapshot を守る）→ 分類の時に「消費予約」の印、受理で確定、拒否で戻す。B-u の最初の実装には無関係。

中 5: 時刻の分解能 1ms（CLOCK_MONOTONIC は 1000Hz、clock.c:505-530、arch.h:72）→ 長さは cntvct_el0（kwl_cycles と同じ main.c:1165）、CLOCK_MONOTONIC は compositor の log と対にする絶対時刻だけ。

軽微: (u1)「SET・MOVE の ioctl だけ」は不成立（present_prepare→swapchain_current→display_validate_surface→vulkan_wsi_display_refresh が GPU_DISPLAY_QUERY を 2 回以上 wsi.c:967-、wsi-display-nodes.c:555、lock の取り合いは無い）→ fixture の期待値と §9 を直す。§9「fast path は lane に公開しない」と §3 の EBUSY の受け渡しの矛盾 → queue->mutex 不要と書き足す。command buffer の表が struct vulkan_image * を直に持つと破棄（wsi-swapchain.c:1184）の後の submit で use-after-free → handle で持ち受理で解決、か valid usage に頼ると明記。fast path の condition 5 に binary semaphore の temporary import が無いことも（queue_write_wait の分類 queue.c:513-528 と同じ関数）。

review-B4 判定: F1 libvulkan 側は解決（止まりの残りは WS213 の gate、重大 2）、H-a ほぼ（中 5、重大 2）、H-b 計測に回したのは可（中 4）、M-a 解決（EBUSY は中 3）、M-b・M-c・M-d・M-e・M-g 解決、M-f 正しい（L1 の後回しは重大 1）、軽微解決、evdev の時刻と CLOCK_MONOTONIC は同じ tick。

未決への意見: (1) G に上限を（G の p99 ≤ 16.7ms、S2・S3 の呼び出し→ioctl の p99 ≤ 33.4ms、上限は直接かける）(2) H-b の目安は p99 と最長、分解能を直してから。B-l の条件「S1 で display_ns − call_ns か done_ns − set_ns の p99 > 4ms」でよいがほぼ確実に起きる、libvulkan の半分は B-u に (3) 毎コマ cursor の buffer を commit する試験の client を plan/ws141/tests/ に置けば S2（形を交互）・S3 も実機で測れる。

未確認: 実機の時間（primary の present の controller->mutex の分布、G）、kernel が submit の外で group の image に書く経路、decoder が session ごとか、cntvct_el0 が EL0 から読めるか（kwl_cycles が使うので可と推測）。

