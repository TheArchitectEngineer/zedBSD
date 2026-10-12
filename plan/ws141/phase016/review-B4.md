# ws141-p016 (B) 第 4 版 review-B4（design-reviewer、2026-10-12）

対象: [phase.md](phase.md) の「(B) 第 4 版」。Q1 から受けた要点の写し（原文は Q1 の scratchpad）。対応は phase.md の「(B) 第 5 版」の表。

判定: B-u 条件付きで実装可。実装前に (1) F1 の扱い (2) ticket の lock と失敗 (3) chain->frame・sequence の競合 (4) 受け入れ B の測り方 を設計に足す。受け入れ B の判定は F1 と H-a の後。

F1 致命的: 像が変わった後は cursor が止まる。worker は queue ごとに 1 本の FIFO で前の job の fence を UINT64_MAX で待つ（wsi-swapchain.c:2747-2785）、present_native は primary の PRESENT_SYNC の間 display_mutex（wsi-display.c:1079-1150）、present の受け付けで PRESENTING（wsi-swapchain.c:1843）で present_finish まで acquire 不可（:2427-2431,:3064）、compositor は acquire した image ごとに新しい像を copy（cursor-plane.c:532-567）し copy の完了まで止める（:499-509）、commit ごとに version が進む（shm.c:320）→ set_cursor で 3 枚とも通常経路、各 present が primary の後ろ、約 3×（primary の GPU＋adoption）止まる。animated cursor は fast path に入らない。直し: (a) cursor の lease の通常経路の job を他の chain を待たない専用の worker に（ticket と併用）(b) WS213 の thread C の copied の gate を「acquire した image の fence だけ」に (c) 受け入れ B の場面に像の変化と上限（compositor の session の GPU 1 回分）。

H-a: 受け入れ B が測れない。閾値の食い違い（phase.md:111 は 100ms、目的と WS213 は 16.7ms）、at_us は戻った後（cursor-plane.c:616-623）で通常経路の ioctl は worker の中、経路が log で分からない、evdev の時刻（input.c:636）の clock 未確認 → libvulkan が present ごとに（経路、ticket、ioctl の CLOCK_MONOTONIC の時刻）を環境変数の log で出す、閾値・百分位・場面（位置だけ／像の変化／animated）を書く。

H-b: fast path の device->mutex の待ちに上限なし（queue_attempt queue.c:422-479、vkGetFenceStatus sync.c:187-224、worker の vulkan_fences_wait sync.c:1194、acquire sync.c:429、WS213 では R の submit とも）→ 測定の項目に。案: software の payload の消費・acquire の signal を vulkan_sync の atomic CAS か専用の小さな lock にして device->mutex を外す。

M-a ticket: next_ticket は lease の atomic fetch-add、通常経路で取る位置を 1 か所（present_reserve の後）。SET の失敗・SET の成功の後の MOVE の失敗の時の applied_ticket。

M-b: chain->frame++ と &chain->sequence（wsi-swapchain.c:2322-2351）を fast path と捨てられる古い job の present_native が同時に書く → fast path では触らないか、display_mutex の下か atomic。

M-c hook: vulkan_image から group の image への back pointer が無い（internal.h:216）、「表が溢れた（ExecuteCommands）」の扱い（secondary の表か、常に epoch）、api-commands.tsv は 3 列で列を足すには tools/maintain-commands.noct と生成物、copy の job の控えは queue_enqueue の再試行（queue.c:345-366）の受理された回で。

M-d WS213 との契約: zedBSD に queue_lock が無く C と R が同じ VkQueue を同時に使う → B-u の fast path が同時の Submit・Present に安全なことを明記、libvulkan の fixture に C の fast path・R の submit・worker の同時の場面。

M-e: import・shm の vkQueueWaitIdle は present_drain で worker を待つ（wsi-swapchain.c:617-629,2853-2900）＝WSI の問題、cursor が main loop にある間は止まる → 前提に「WS213 p004、または main loop の WaitIdle の扱い」。

M-f: acquire の display_image_available（wsi-display.c:1158-1170）も display_mutex を待つ、kernel の output_present は settle・adoption の間 controller の mutex（display-device.c:861-930）、cursor の image は holds を取らないので cursor の chain では省ける。WS213 ws.md:34 は L1 を前提、v4 は「計測の後で次の版」→ 揃える。

M-g 試験: 全部の image が PRESENTING の時の像の変化、fast path と worker の同時（frame・ticket）、SET の失敗と applied_ticket、ExecuteCommands と epoch。

軽微: phase.md:231「各 image は最初の 1 回だけ写す」未修正（M3）、phase.md:388 の段の表の B を B-k・B-u に。lock の順（fast path で device・display・swapchain の mutex を入れ子にしない、worker の copied_serial の更新は display_mutex の外）。FIFO の present mode で古い present を捨てるのは cursor に限る例外と明記。has_display が無い present の x,y の出所（:2329）。fast path の ioctl の直前に chain->error と device の error を見直す。UAPI の注記（gpu-display.h:237-250）に全部の cursor の image が list に名指された時の EBUSY（display-device.c:1724-1726）。vulkan-buffer-copy.c:289-290 の backing の解決の失敗で generation を進めない（推測）。

review-B3 判定: C1 一部、C2 一部（F1）、H1・H2・M1・M3・M5 解決、M2 ほぼ（M-c）、M4 計画だけ（M-g）、L1 計測に回した（M-f）、L2・L3 解決。

WS213 との関係: 期待が循環（WS213 は 60fps を B-u に、p016 は B を (b)・A3 に、main loop の止まりは WS213）→ 分担: p016 B は「main loop が止まらない時の入力→ioctl」、WS213 は端から端の 60fps。C と R の同時使用（M-d・H-b）、thread C の copied の gate（F1 (b) は WS213 6.2 にも）、L1 の位置付け（M-f）。

問題なし: 不変条件 (i)（ACQUIRED の image だけ wsi-swapchain.c:1769,1832）、kernel は SET の時点で写し cursor の present は holds を取らない、B-k の実装（display-device.c:1701-1705,1744,1708）、加算の 1 か所（queue.c:461-473、:2220、queue.c:218）、swapchain の破棄は pending_jobs を待つ（:1037）、lease は旧と新の chain で共有（wsi-display.c:64-79）、新しい lock の循環なし。

未確認: hover・drag で client が cursor を変えるか、evdev の時刻の clock、kernel が submit の外で group の image に書く経路、decoder が session ごとか全体か、実機の時間。

