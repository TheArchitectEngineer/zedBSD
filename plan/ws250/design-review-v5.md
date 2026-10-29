<!-- awesome-plan project=zedbsd record=ws250-design-review-v5 -->

# WS250 設計の敵対的レビュー（design.md 第 5 版）

日付: 2026-10-29（host の `date`）。担当: design-reviewer（P5 の依頼）。対象: [design.md](design.md) 第 5 版（commit 6c8609bee）。

照合した物: main `69c15ae94` の source（`git show 69c15ae94:…` で行を確かめた）。読んだ物: `src/drivers/gpu/nvrtx/{vk-node.c,runtime.c,work.c,rpc.c,node.c,objects.c,gr.c,nvrtx-node.h,nvrtx-gpu.h,nvrtx-work.h}`、
`vk/{vk-execute.c,vk-device.c,vk-memory.c,vk-object.c,nvrtx-vk.h}`、`display/{present.c,cursor.c,scanout.c}` の lock と RM の呼び出し、`src/drivers/gpu/gpu.c`（close・monitor・report）、
`include/drivers/gpu/gpu.h`、`src/drivers/gpu/i915/reset.c`、`src/kern/lock.c`・`src/kern/thread.c`・`include/kern/thread.h`、`plan/ws291/phase011/phase.md`、`plan/ws039/tests/vkhang/`、
`plan/tools/nvrtx/nvrtx-vk-host-test.sh`、`userland/desktop/libvulkan/wsi-swapchain.c`（present の fence）。公開の source は取得していない（F1〜F11 は知識で判断、推測と明記）。
注: review の間に worktree の HEAD が 72f15b880（p002 の `rpc_lock` の実装）へ進み、未 commit の変更もあった。それらの code は review の対象にしていない。

重大度: **blocker**（このまま作ると目的を壊す）、**major**（実装の前に設計を直す要がある）、**minor**（実装の中で直せる・文書の整合）。

## 要点

- **blocker 2 件**: (B1) 犯人の判定の表（4.4）は、被害者の期限が犯人の「走って 2 s」より先に来ると、被害者を LOST にする（行 3・5 と延長 3 回の上限）。S2 で起こりうり、S4（犯人 2 つ）では構造的に起こる。
  (B2) 始まりの印は「GR に載っている」を示さない。WFI の preemption は draw の境で channel を切り替えるので、mview（1 run 約 19 ms）の run は始まった後に犯人に GR を取られ、「始まって 2 s 以上終わらない run」になる。行 1 と reaper の見張りが mview（または compositor）を犯人として LOST にし、STOP する。
- **major 7 件**: 退避の channel（chid 4）の犯人が見張られず、`isolate` が 0 を返して GR が永久に止まったまま fault も出ない／channel の遅延の作成（2 s の first fence、`rpc_lock`、作り手の競争、session が channel を替えない不変条件）／取り消しの実行者が reaper と isolate の 2 つ／p005 の `node->lock` の手放しが vk 層の不変条件（destructor の中の待ち）を壊す／lane・reaper の thread の終わりを待つ API が無い（`thread_wait` は待たない）／p003 単独（p005 の前）で `node->lock` の下の待ちが延びて desktop が今より長く止まる／試験の計画が B1・B2 を見つけられない。
- UAPI・`gpu.h`・HAL・`vk/nvrtx-vk.h` の口を変えないことは確かめた（関数の形は変えない。ただし `nvrtx-vk.h` 20 行の「node の lock が全部を直列にする」という文の意味は p005 で変わる: M4）。HAL の変更の要は無い。

## Blocker

### B1. 被害者の期限が犯人の 2 s より先に来ると、被害者が LOST になる（4.4 の表の行 3・5、延長 3 回）

- 根拠:
  - 4.4 の行 3「他の channel に始まって 2 s 以上の run」だけが BLAME。行 5「犯人も取り消しも無い」は LOST。行 4 は「取り消しの途中か、終わって 2 s 未満」。
  - 被害者の期限は submit + 2 s（`vk-node.c` 37〜38・604〜607、4.4「期限 = submit から 2 s」）。犯人の「走った時間」は犯人の start stamp から（4.3）。start stamp は host の待たない release（`objects.c` 565〜581）で、前の run の WFI の fence（`objects.c` 518〜526）の後に出る。
- 起こる状況:
  1. S2: mview の run b が t_b に submit され、まだ始まらない（GR は mview 自身の前の run か keiland の run）。その後 vkhang の run a が t_a > t_b に始まって詰まる。t_b + 2 s に b の判定: 自分は未着手、a は走って 2 s − (t_a − t_b) < 2 s、取り消しは無い → 行 5 で **mview が LOST**。reaper の見張りは 250 ms ごとなので、a を見つけるのは t_a + 2 s 以後。
  2. S4（vkhang 2 つ）: A1 の取り消しが kill_end に終わる。scheduler が先に A2 を載せると A2 は kill_end + δ に始まる。被害者の次の期限 kill_end + 2 s で A2 は 2 s − δ → 行 5 で **mview が LOST**。δ > 0 なので「A2 が先に載る」たびに必ず起こる。
  3. 延長 3 回の上限: S4 で mview は「A1 の BLAME → A2 の待ち → 自分の run の始まり + 2 s」で 3 回を使い、もう 1 回要ると行 6 で LOST。
- 直し方の案:
  - 行 5 を「他の channel に **始まって終わらない run が 1 つでもあれば** EXTEND（その run の始まり + `RUN_US` + 取り消しの余裕）」にし、LOST は「他に始まった run も取り消しも無い」時だけにする。
  - 延長の上限を回数でなく「進みの無い延長」に当てる（新しい犯人を見つけた・取り消しが終わった・自分の run が始まった、のどれかが起きた延長は数えない）か、壁時計の上限（例: 取り消しの回数 ×（`RUN_US` + 取り消しの最悪）＋ `RUN_US`）にする。
  - host 試験は 4.4 の表そのものから期待値を作るので、この誤りを見つけられない（M7）。

### B2. 「始まった」は「GR に載っている」ではない: 途中で preempt された被害者を犯人にする（4.3、4.4 の行 1・2、4.5 の 1）

- 根拠:
  - F9 のとおり preemption は WFI で、draw の途中では切り替えないが、draw と draw の境では切り替える（timeslice の満了で他の channel が走れる時）。
  - mview（scene-5000）の 1 run は約 18.5〜19.5 ms（`plan/ws291/phase011/phase.md` 15 行）で、多数の draw を持つ。start stamp はその run の頭で出る（4.3）。
  - 4.4 行 1「自分の run が始まって 2 s 以上 → LOST」、4.5 の 1「READY で最も古い終わらない run が始まって 2 s 以上の channel を LOST」。
- 起こる状況: S2 で mview の run が始まり、timeslice の満了で draw の境で preempt され、vkhang が GR に載って詰まる。mview の run は「始まって終わらない」まま 2 s を越える。mview の期限の判定（行 1）か reaper の見張りが **mview を LOST にし STOP する**（vkhang も同じく LOST）。mview は完走しない。keiland の compose の run が同じ形で preempt されれば **compositor が DEVICE_LOST**。4.4 の BLAME も「始まって 2 s 以上の run を持つ channel」が 2 つ以上になり、どれを取り消すかが決まらない。
  timeslice の既定の長さ（推測: ms の桁）は未確認だが、19 ms の run は越える見込みが高い。
- 直し方の案:
  1. 主の入力を「今 GR に載っている channel」にする: GR の current context（9 節 U4 の案、nouveau の 0x409b00 の FECS の current ctx）を CPU から読み、各 channel の instance block の VRAM の番地と照らす（nvrtx は instance を自分で置くので対応が分かる）。nvrtx は既に PGRAPH を CPU から読んでいる（`vk-node.c` 64〜66・647〜649）ので、読める見込み（推測）。
  2. 読めない時の代わり: 「始まって終わらない run のうち **一番遅く始まった** channel」を犯人にする（載っていない channel は次の run を始められず、詰まった channel は手放さないので、最後に始まった物が載っている物。U4 が成り立つ前提）。他の「始まって終わらない」channel は EXTEND。
  3. 見張りと行 1 は「犯人と判定された channel」だけに当て、取り消しは 1 つずつ、終わるたびに判定し直す。
  4. U4 が崩れる（PBDMA が GR と別に切り替わり、被害者の start stamp が出る）時は 2 の代わりが誤るので、1 を p003 の必須にするか、S1 の probe で U4 を先に確かめてから判定を作る順にする（7 節）。

## Major

### M1. 退避（chid 4 の共有）の session が犯人の時に誰も取り消さず、`isolate` が 0 を返して fault も出ない（4.1、4.4 の最後の行、4.8 の isolate）

- 根拠: 見張りと BLAME の対象は `channels[32]`（4.1・4.5）で chid 4 は入らない。退避の session の取り消しは「しない」（4.1）。4.8 の isolate は「channel が NONE・退避」なら 0。`gpu.c` 7705〜7716: isolate が 0 なら fault を呼ばない。今の nvrtx は isolate が NULL（`runtime.c` 105〜111）なので、期限の後は必ず device の fault で、少なくとも「壊れた」が見える。
- 起こる状況: 32 個の上限・open の失敗（M2）・`nvrtx.channels=0` で退避した session が詰まる → GR は永久に占められる（WFI）。session の channel の被害者は BLAME の相手が無く延長の後に LOST、新しい frame も全部 LOST、退避の session の close は stop の期限 → isolate 0 → QUARANTINED で終わる。device は error にならず、desktop は「動いているが何も描けない」まま reboot まで続く。`nvrtx.channels=0` の A/B は「今の振る舞い」と違う結果になる。
- 直し方の案: 退避の session に終わらない run がある時の isolate は EIO（今と同じ device の fault）にする。判定の候補に chid 4 を入れ、犯人が chid 4 なら被害者は延長せず今のまま LOST（または device の fault を早く出す）と表に書く。stop_poll の表（4.8）にも退避の session（chid 4 の上のその session の最後の run）を入れる（今は READY・NONE などだけ）。

### M2. channel の遅延の作成: first fence の 2 s、`rpc_lock`、2 つの作り手、session が channel を替えない不変条件（4.2 の open と「作る時」、7 節 p002）

- 根拠: 4.2 の手順 4「first submission と fence を 2 s 待つ」、失敗は退避。4.7「channel の open は `rpc_lock` で囲む」。p002 の時点の renderer は worker 1 本（`runtime.c` 695〜765）。frame は worker と `runtime_command`（ioctl の thread、`runtime.c` 237〜282）の 2 か所で走る。`nvrtx-vk.h` の ticket は fence と submit の時刻だけ（`vk-node.c` 393〜416）で channel を持たない。
- 起こる状況:
  1. 終わらない run が GR を占めている間に起動した app は、first fence が WFI のため 2 s 内に来ない → open が失敗 → **その session は一生退避**（取り消しの対象外、M1 の道）。
  2. fence の待ちを `rpc_lock` の中でするなら、その 2 s の間 reaper の STOP が待たされる。
  3. p002 では worker 1 本が open の RPC（6〜8 本）と first fence を行い、その間 全 session の frame が止まる（app の起動のたびに compositor の frame が遅れる）。
  4. p002 には `exec_lock` が無い（p005 で入る）ので、同じ session の worker と `runtime_command` の thread が同時に「channel が無い」を見て 2 つ作る（chid と VRAM の漏れ、`channels[]` の上書き）。
  5. 作成を frame ごとに試す実装にすると、同じ session の run が chid 4 と自分の channel に分かれ、`run_fence` をどちらの semaphore で見るかが分からなくなる（早い free → GPU の use-after-free）。
- 直し方の案: (a) session の channel は最初の submit の前に 1 回だけ決め（作るか退避か）、以後替えない、を不変条件として 4.10 に書く。(b) 作成は session ごとの mutex の下（p002 で `exec_lock` を先に入れる）。(c) `rpc_lock` は RM の call の間だけ持ち、first fence の待ちの前に手放す。(d) first fence が来ない理由が「他の channel が GR を占めている」（判定の入力で分かる）なら退避にせず、後で試す（または first submission を最初の run と兼ねて fence を待たない）。(e) p002 の受け入れに「open の間の他 session の frame の遅れ」を測る項目を足す。

### M3. 取り消しの実行者が reaper と isolate の 2 つで、同じ channel を二重に取り消しうる（4.5、4.8 の isolate）

- 根拠: 4.5 の reaper は LOST の channel を KILLING にして STOP → 失敗なら `rpc_lock` を **手放して** RC を 500 ms 待つ → RESET → FREE。4.8 の isolate は「証拠が無ければ（READY・LOST・STUCK）その thread で同期の取り消し」。isolate を呼ぶのは `gpu_monitor_step`（`gpu.c` 7705〜7710）で、monitor の thread か close の thread（`gpu_monitor_close` が自分で step を回す、`gpu.c` 1587・7760〜7800）。KILLING の時の isolate の扱いが表に無い。
- 起こる状況: stop の期限（10 s）と reaper の取り消しが重なる（STOP 5 s ＋ 500 ms ＋ RESET 5 s の最悪は 10 s を越える）→ reaper が 500 ms の待ちで `rpc_lock` を離した間に isolate が LOST/KILLING の channel に STOP・FREE を送る → 二重の FREE（RM の handle の 2 回目は失敗で STOPPED になり資源を返さない、または状態を上書き）、reaper の後の FREE が既に消えた handle に行く。`nvrtx.kill=0`（S1）でも isolate は取り消してしまい、probe の観測を乱す。
- 直し方の案: 取り消しの持ち主を 1 つにする（state の READY/LOST → KILLING を `queue_lock` の下で CAS し、取った者だけが行う）。isolate は KILLING なら reaper の結果を waitq で有限に待ち、結果（FREED・STOPPED → 0、STUCK → EIO）を返す。`nvrtx.kill=0` の isolate は EIO（今の fault）。gpu.h の「isolate … ends every callback of that context with an error」に合わせ、isolate はその session の queue と park の entry を EIO で終えることも書く（i915 は `reset.c` 707〜709 で行う）。

### M4. p005 の「待つ間 `node->lock` を手放す」は vk 層の不変条件を壊す（4.9）

- 根拠: `nvrtx-vk.h` 20 行「The node's lock serializes everything here」、`vk-object.c` 16 行「The node's lock serializes every call」。待ちは destructor の中にある: `nvrtx_vk_object_release` が `object->destroy` を呼び（`vk-object.c` 189）、`memory_destroy` が `nvrtx_vk_memory_detach` → `nvrtx_vk_gpu_idle` → `vk_node_wait`（`vk-memory.c` 510、`vk-node.c` 153）。`nvrtx_vk_objects_close` は list を丸ごと外してから release を回す（`vk-object.c` 237〜260）。一方 `nvrtx_vk_node_blob_attach` は ioctl の thread から `node->lock` だけで `session->vk` の registry を引いて書く（`vk-node.c` 113〜119、`vk-memory.c` 107〜130）。`vk_node_wait` は completer からも `node->lock` 無しで呼ばれる（`vk-node.c` 459〜478）。
- 起こる状況: 4.9 は `exec_lock` で「自分の executor」を守るとするが、`exec_lock` を取らない `node->lock` の使い手（blob の attach、`node_close` の `nvrtx_vk_session_close`（`node.c` 503）、present の `shown` の参照）が、destructor の途中で止まっている session の `session->vk` に入る。例: vkFreeMemory の destroy の待ちの間に、同じ app の別 thread の blob の作成が registry を引く。今見える具体の壊れ方は限られる（unlink は待ちの前に済む）が、vk 層の 2191 行の executor のどこで待ちが起きるかの監査が設計に無い（推測を含む）。また `vk_node_wait` が無条件に `node->lock` を離すと、completer の道で持っていない mutex を離す。
- 直し方の案: `session->vk` に触る全ての道（blob の attach、session の close、present が executor の物を見る所）を `exec_lock` → `node->lock` の順にすると書く。lock を離す待ちは上の段（`execute_start`・`execute_drain`・`device_wait_idle`）だけにし、destructor の中の待ち（detach・`nvrtx_vk_gpu_free`）は離さない（または destructor を待ちの後へ遅らせる）。`vk_node_wait` を「lock を持つ版」と「持たない版」に分ける。`nvrtx-vk.h` の comment の意味の変更（関数の形は変えない）を p005 の範囲に書く。

### M5. lane と reaper の thread の「終わりを待つ」API が無い（4.9、4.10）

- 根拠: 4.9「close で drain の後に終わらせて終わりを待つ（`thread_exit`）」、4.10「reaper を止め（印と wake、終わりを待つ）」。`thread_wait`（`src/kern/thread.c` 564〜581）は zombie でなければ `EBUSY` を返すだけで待たない。今の renderer の thread は `detached = 1`（`runtime.c` 156・161）で、終わると自動で回収される（`thread.c` 549〜552）。
- 起こる状況: 「終わりの印を見て `kern_free(session)`」の形にすると、thread が印を立てた後に lane の waitq・spinlock・counter に触る間に session が free される（use-after-free）。close のたびに起こりうる。
- 直し方の案: thread の最後の lane への触り（終わりの印を `queue_lock` の下で立て waitq を起こす）を明記し、その後は lane に触らず `thread_exit`。close は印を waitq で待つ。lane の記憶を session と別に持ち、thread が最後に free する形でもよい。reaper も同じ。作成の失敗（`kthread_create`）時の扱いも 4.9 は ENOMEM、4.5 の reaper は未記載（無い時は isolate の同期の取り消しに頼る、と書く）。

### M6. p003 を p005 より先に入れると、`node->lock` の下の待ちが延びて desktop が今より長く止まる（7 節の順、4.4 の EXTEND）

- 根拠: p005 までは executor の待ち（slot の再利用 `vk-execute.c` 586、`execute_drain` 697、`device_wait_idle` `vk-device.c` 835、detach・free）は `node->lock` の中（`runtime.c` 948〜955）。p003 はこの待ちに EXTEND（取り消しの終わり + 2 s、最大 3 回）を入れる。flip・cursor は `node->lock` を取る（`display/present.c` 191・217・239 ほか、`display/cursor.c` 192・282・356）。
- 起こる状況: mview の executor が slot の再利用で被害者として EXTEND すると、その間 keiland の frame・flip・cursor の移動が全部止まる。今は submit から 2 s で EIO なので最長 2 s、p003 単独では 2 s ＋ 取り消し（通常 1 s 未満、最悪 10 s 超）＋ 2 s。S2 の合格「mview は止まらず完走、fps の落ち込みは 2〜3 s の 1 回」は p003 の時点では満たせないことがある。
- 直し方の案: p005 の「待ちを `node->lock` の外へ」（lane 全体ではなく executor の待ちの部分）を p003 の前か p003 に入れる、または p005 までは `node->lock` の下の待ちに EXTEND をしない（今の 2 s のまま、lane の completer の待ちだけ延ばす）と書く。p003 の実機の受け入れを「S2 は p005 の後に判定」と分ける。

### M7. 試験の計画が B1・B2・M1〜M3 を見つけられない（6.1）

- 根拠: 6.1 の host 試験は「4.4 の表の全行」を期待値にする。表そのものが B1・B2 の誤りを持つので、表どおりに作れば PASS する。実機の S2 は 1 回の偶然に頼る（B1 の δ、B2 の preempt の時機）。
- 直し方の案: `recover.c` に「GPU の scheduler の model」（timeslice、draw の境の preempt、WFI で詰まる draw、start stamp と fence の出る時刻）を host 試験に入れ、乱数の時機で「被害者が LOST にならない」「犯人だけが取り消される」を何千回も確かめる（S2・S4・mview の 19 ms の run・keiland の短い run）。M3 の二重の取り消し（期限と reaper の重なり）、M1 の chid 4 の犯人、M2 の作り手の競争も model の項目にする。

## Minor

- **m1. 行の番号**: 1 節の gpu.c「1530・1558〜1565・7546〜7640」は今の tree で 1552（`gpu_session_fail_locked(…, ENODEV, 0U)`）・1581（drain）・1587（`gpu_monitor_close`）・7509〜7740（`gpu_monitor_step`）。11 節の gpu.h「61〜88」は 62〜90。1 節の `nvrtx-work.h`「37〜40」は 38〜43。
- **m2. space の lock**: 今は `work->lock` 1 つが ring と heap・PTE を守る（`work.c` 374・445・501・554）。4.2 は `lock` を channel に移すと書くが、space（heap と PTE の書き込み）の lock の名前と、4.7 の lock の順の中の位置が無い。`nvrtx_work_map_vram` などは `work->ready`（driver の channel の印）を見る（`work.c` 377・448）ので、space の ready を分ける。
- **m3. 呼び出し元の一覧**: `runtime_park` は `nvrtx_work_passed(node->gpu, …)`（`runtime.c` 859）、`vk-node.c` は passed・wait_late・run_time を gpu 単位で呼ぶ（439・468・492・516・521・616）。全部を session の channel に替える一覧を p002 に書く。
- **m4. 判定の「自分の run」**: 待っている fence ではなく session の channel の「最も古い終わらない run」で判定する（自分の前の run が正当に長い時に待っている run は始まらない）。
- **m5. VA の区画の大きさ**: 2 MiB の固定でなく、own の buffer の `page_shift` の揃えを含めて計算し、越えたら退避（`work.c` 806〜817 の配置の形）。実機の own の buffer は 0xf5000 で、ring・semaphore・fences（40 KiB）と合わせて今は収まる。
- **m6. 公開の順**: open は `session->channel` と `channels[]` を `rpc_lock` を離した後に `queue_lock` の下で公開する。`rpc_lock` を持ったまま `node->lock` を取らない（display の control は `node->lock` → `rpc_lock` の順で呼ばれうる: 72f15b880 の `display_control`。推測: 呼ぶ文脈は未確認）。
- **m7. close の順**: `node_close`（`node.c` 486〜513）で、lane の終わり → executor の close（detach・free は channel の semaphore を読む）→ channel の close（semaphore・ring を返す）の順を 4.2 に書く。逆だと返した semaphore を読む。
- **m8. FREED と quarantine の規則の食い違い**: 4.2・4.10「FREED は close で資源を返す」と 4.8「quarantine の session の close は資源を返さない」。isolate の同期の取り消しで FREED になった session はどちらか、を決める。
- **m9. S1**: 手順の欄に mview の起動が無い（観測の欄にだけある。M6 の前の review の指摘の解決の主張と合わない）。`nvrtx.kill=0` では GR が詰まったまま（GSP の RC が来なければ）で reboot が要る、と手順に書く。
- **m10. STOP の時間**: `NVRTX_CALL_TIMEOUT_US`（5 s）を越えると `rpc_unanswered` で device 全体が止まる。GSP の中の preempt の待ちの長さは公開の source に無い（U1）。S2 の前に「STOP だけを送り時間を測る」probe を S1 に足す。
- **m11. 記録の写し**: ws.md は「必須 34 LW」（26 行）、「RTX 機（10.0.30.5）」「compute shader」（42 行）、Phase の表は p001 のまま。design の 29 LW と p002〜p007 に合わせる（本体と写しを同じ commit で）。
- **m12. RC の handler の文脈**: 4.6「呼ぶ thread はどちらも `rpc_lock` を持つ」は、`nvrtx_node_stop` の drain の停止の後の shutdown の RPC（`node.c` 231〜288）でも成り立つかを書く。handler は reaper を止めた後に呼ばれても安全（reaper の waitq を起こすだけ）と明記する。

## 確かめて正しかった主張

| 主張 | 確かめた所 |
| --- | --- |
| 非同期の submit・待ちの分離、`run_lost`、`VK_NODE_RUN_US` 2 s | `vk-node.c` 37〜38・379〜418・426〜451・580〜633 |
| 喪失した session の memory と run は残す（idle の失敗で return） | `vk-node.c` 152〜157・344〜349 |
| worker 1 本・completer 1 本、park の順に session をまたいで待つ | `runtime.c` 145〜174・695〜765・788〜835・843〜888 |
| recovery の ops は stop_begin・stop_poll・fault、reset・isolate は NULL | `runtime.c` 105〜111・593〜656 |
| work channel は chid 4、timed の start stamp は待たない host の release（timer つき）、fence は WFI の release、record 64 個 | `work.c` 64〜68・93・563〜620、`objects.c` 518〜526・565〜581 |
| reply は function だけで照合、CHANNEL_ERROR は log だけ、timeout で `rpc_unanswered` | `rpc.c` 237〜247・270〜282・501 |
| close の順（fail_locked(ENODEV) → drain → monitor_close → destroy → close）、期限で isolate、無い・失敗で fault | `gpu.c` 1552・1581・1587・1612、7705〜7716 |
| `drv_gpu_report_session_error` は backend の pointer で session を引き hard の fail を立てる（以後 monitor が stop_begin を呼ぶ） | `gpu.c` 872〜905・7256〜7271 |
| i915 の isolate は quarantine を立ててから engine を回復 | `i915/reset.c` 686〜716 |
| 今の stop_poll は GPU の run を見ない（drain の後でも run は走りうる）ので、4.8 の「READY で終わらない run は EAGAIN」は今の隙間を埋める | `runtime.c` 624〜656、`runtime.c` 731〜736（frame の completion は submit の後） |
| `vkhang` の hang.frag は 0 から（0 は偶数で 0 のまま）で本当に終わらない | `plan/ws039/tests/vkhang/hang.frag` |
| 共有の ring の暗黙の順に頼らず、present は producer の fence を待つ（session ごとの channel で画の順は壊れない見込み） | `wsi-swapchain.c` 3285〜3320（keiland の側の待ちは未確認） |
| RM の handle（0xf1f00010+n 等）は既存の handle と衝突しない | `nvrtx-gpu.h` 75〜124 |
| UAPI・`gpu.h`・HAL の変更は不要。isolate・report は既存の口。host 試験 `nvrtx-vk-host-test.sh` は `vk/` の file だけを build するので、`vk/` の関数の形を変えない限り通る | `gpu.h` 62〜90、`nvrtx-vk-host-test.sh` 16〜23 |
| mutex の rank は記録だけで順の検査は無い（lock の順は設計の約束だけで守る） | `kern/lock.h` 14〜46、`kern/lock.c` 53〜58・209〜217 |

## 確かめられなかった事（推測のまま）

- GSP の中の preempt・RC・engine の reset の手順と時間、STOP_CHANNEL の返る時間（B2・m10、U1・U2）。
- PBDMA が GR と別に他の channel へ切り替わるか（U4。B2 の代わりの規則の前提）、timeslice の既定の長さ（B2 の起こる率）。
- 0x409b00（FECS の current ctx）を GSP の下で CPU から読めるか（B2 の直し方 1）。
- keiland が client の buffer を読む前に fence を待つか（session ごとの channel で画の順が壊れないかの最後の確かめ）。
- 72f15b880 以後の p002 の code（`rpc_lock`、display の `display_control`）。

## LW について

必須 29 LW には B1・B2 の直し（current ctx の読み、scheduler の model の host 試験）で p003 に +3、M2・M3 で p002・p003 に +1〜2、M4・M5 の監査で p005 に +1〜2 の見込み（合わせて 34〜36 LW、±50%）。
M6 の直しで p005 の「待ちを lock の外へ」を p003 の前に出すなら、Phase の表の依存も「p003 は p005a に依存」に直す。
