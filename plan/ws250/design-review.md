<!-- awesome-plan project=zedbsd record=ws250-design-review -->

# WS250 設計の敵対的レビュー（design.md 第 1 版の本文と 2026-10-29 の第 2 版の改訂）

日付: 2026-10-29（host の `date`）。担当: design-reviewer（P5 の依頼）。対象: [design.md](design.md)（本文 §0〜§11 と「第 2 版の改訂」R1〜R12・U1〜U5・Phase の表・Q250-1〜3）。

照合した物: worktree `p5-design`（HEAD ace88ccd6。`src/drivers/gpu`・`include/drivers/gpu` は design が言う main 61630bb0b と同じ、今の main 672104644 とも差が無いことを `git diff --stat` で確かめた）。
読んだ source: `src/drivers/gpu/nvrtx/{work.c,runtime.c,rpc.c,channel.c,gr.c,node.c,vk-node.c,objects.c,records.c,gsp.c}`、`nvrtx-{gpu,gsp,work,node}.h`、`vk/{vk-execute.c,vk-queue.c,vk-pipeline.c}`、`src/drivers/gpu/gpu.c`（close・監視・report）、`include/drivers/gpu/gpu.h`、`src/drivers/gpu/i915/reset.c`（isolate）、`src/kern/lock.c`（mutex）、`userland/desktop/libvulkan/context.c`、`userland/tests/vkreset/`、`config/kernel-options.list`、`plan/guardrail.md`。
公開の source の写し（信頼しない data として読むだけ）: open-gpu-kernel-modules 570.144（`version.mk` で版を確かめた）、Linux nouveau、Mesa。何も実行していない。

重大度: **blocker**（このまま実装すると目的を壊す・system が止まる）、**major**（実装の前に設計を直す要がある）、**minor**（実装の中で直せる・文書の整合）。各項目の「推測」は source で確かめられなかった事。

## 要点

- **blocker 2 件**: (B1) `stop_begin` が READY の channel を LOST にする案は、共通の層が **全ての正常な close** で `stop_begin` を呼ぶため、app が終わるたびに kill が走り、worker が寝ていれば 10 s 後に isolate が失敗して **device 全体の fault** になる。(B2) kill の RPC に短い timeout（1 s）を当てて `rpc_unanswered` を立てない案は、reply を function だけで照合する今の `nvrtx_rpc_wait` と組むと、遅れた STOP の reply を次の control の reply と取り違える。
- **major 13 件**: 案 A（session ごとの channel）の利点が p002〜p004 の範囲（worker 1 本・`node->lock` の中で GPU を待つ・WFI）では成り立たない、`isolate` が事実上呼ばれる時に必ず失敗する（作っても働かない）、kill を worker の「次の turn」に頼る（worker が寝ていると走らない）、`rpc_lock` が drain に取られ続ける（mutex に hand-off が無い）、chid の再利用と遅れた RC の event、U2・S1 の probe が GSP の ctxsw の timeout を起こせない、`vkhang` の loop が無限でない、kill の判定の基準が本文と R1 で食い違う、session の channel の VA の割り当てが未設計、`struct nvrtx_channel` の名前の衝突、実機が要る Phase の範囲の誤り、本文と改訂の矛盾の残り、channel を全ての open で作る費用。
- **確かめて正しかった主張**も多い（末尾の表）。R1・R4・R6 の header の引用と値、`rpc.c` の RC の event の offset、`VK_NODE_RUN_US`・`WORK_CHID`・`nvrtx_work_stop` の行など。

## Blocker

### B1. 正常な close のたびに kill が走り、device 全体の fault に落ちうる（§4.6 の `stop_begin` の行、§4.8）

- 根拠:
  - `gpu.c` 1530: final close は必ず `gpu_session_fail_locked(session, ENODEV, 0U)` → 1192〜1193 で `stop_state = GPU_STOP_REQUESTED`。
  - `gpu.c` 1558〜1565: その後 `commands->drain`（`runtime_drain`、pending が 0 になるまで待つ）→ `gpu_monitor_close`。
  - `gpu.c` 7546〜7560: REQUESTED の session は `native_submitted`（`gpu.c` 3373・5891・6090 で立つ。frame を 1 つでも出した session は全部）なら action 1 = `stop_begin` を呼ぶ（7602）。つまり **正常な close でも `stop_begin` は毎回呼ばれる**。
  - design §4.6:「`stop_begin` … 加えて `channel->state == READY` なら LOST にして worker に kill を頼む（…正常な close は `runtime_drain` の後に来るので run は無い。期限切れ（共通の層の 60 s）で来た時だけ効く）」。この括弧の前提は誤り。drain の後でも `stop_begin` は来る。
  - `stop_poll` は LOST・STOPPING の間 EAGAIN（§4.6）。kill は worker が「次の turn」で行う（§4.4・§5.1 の `runtime_worker` の行）が、worker は queue が空なら `runtime.c` 694 の `waitq_sleep(&node->available, …)` で寝ていて、kill を頼んでも起きない。
  - `gpu.c` 7569〜7572: stop の期限（`CONFIG_GPU_JOB_STOP_MS` 既定 10 s、choice は 1 s も可）が過ぎると action 3 → `isolate`（7627）。design の `isolate` は LOST なら失敗 → `fault` → `drv_gpu_report_error`（7633〜7640）。
- 起こる状況: desktop で app を 1 つ閉じる（vkquad が終わるだけでも）。queue が空なら 10 s 後に renderer が faulted になり、compositor を含む全 session が EIO。queue が空でなくても、正常な close のたびに STOP_CHANNEL（bImmediate）と（設計によっては）RESET_CHANNEL が GSP に飛び、「channel %u lost」の log と `drv_gpu_report_session_error` の誤報が出る。
- 直し方の案:
  1. `stop_begin` は今のまま `stopping` だけにする（READY を LOST にしない）。正常な close の channel は `stop_poll` が pending 0 で 0 を返し、`node_close` が `nvrtx_channel_close`（schedule off → FREE）で閉じる。run は同期で待つ作りなので、pending 0 の READY の channel に GPU の仕事は残っていない。
  2. kill が要るのは「run の期限」「RC の event」「`stop_begin` の時に run が走っている」（`session->running` のような印を `queue_lock` の下で持つ）の 3 つだけ、と明記する。
  3. kill の実行は M3 の形（必ず走る thread）にする。
  4. host 試験に「gpu.c の close の順（fail_locked(ENODEV) → drain → stop_begin → stop_poll → resource_destroy → close）を model にして、READY の session の close が kill を呼ばず 1 回の poll で FINISHED になる」を足す（§6.1 の 1〜5 ではこの誤りは見つからない）。

### B2. 短い timeout の kill の RPC が、後の RPC の reply を取り違えさせる（§4.3 の最後の段落、§9 の 6）

- 根拠:
  - `rpc.c` 219〜228: `nvrtx_rpc_wait` は reply を **function だけ**で照合し、sequence の違いは「shown, not refused」（220 行の comment）。control は全部 `NVRTX_CALL_CONTROL` なので function が同じ。
  - `rpc.c` 460〜490: `rpc_exchange` は timeout の時に `rpc_unanswered = 1` を立て、以後の call を全部断る。これが「遅れた reply を後の call が拾う」ことを防いでいる今の唯一の仕組み。
  - design §4.3:「kill の call には短い timeout（1 s）を使い、`rpc_unanswered` は立てない（kill の失敗は device の喪失ではない）」。
  - ogkm `ctrla06fgpfifo.h` 210〜214: bImmediate でも「If the preempt times out, we will RC the channel」。GSP の中の preempt の待ちと RC の処理の長さは公開の source に無い（firmware の中）ので、1 s を越える見込みは否定できない。
- 起こる状況: STOP_CHANNEL の reply が 1 s を越えて来る → nvrtx は STUCK と判定して次の control（`SCHEDULE` off、または別の thread の channel の open の promote・bind）を送る → その wait が **遅れた STOP の reply**（status 0 のことも ≠0 のこともある）を自分の reply として受ける → 後の call の status と reply の params（`gpu->reply` の中身）が 1 つずつずれる。open の成否の誤判定、promote の失敗の見逃し、さらに本物の reply を drain が「rpc: message … while waiting」として捨てる。GSP は 1 つずつ処理するので、短い timeout は待ち時間を縮めもしない。
- 直し方の案（どちらか）:
  1. kill にも `NVRTX_CALL_TIMEOUT_US`（5 s）と `rpc_unanswered` の今の意味を使う（GSP が 5 s 答えなければ device の fault で、段 3 と同じ）。§4.6 の「kill は 10 s より十分短い」の計算をこの値でやり直す（M3・minor m5）。
  2. 短い timeout を本当に使うなら、先に `nvrtx_rpc_wait` を sequence の厳密な照合にする（自分より古い sequence の reply は捨てる、新しい物は error）。これは全部の RPC の道の変更なので p002 の範囲と試験（遅れた reply の host 試験）に入れる。

## Major

### M1. 案 A（session ごとの channel）の比較が、p002〜p004 の作りでは成り立たない（§0 の 1・6、§4.1 の表、D1）

- 根拠: `runtime.c` 675〜744（worker 1 本）・752〜791（`runtime_execute` は `node->lock` の中で executor を走らせ、`vk-node.c` 291 の fence の待ちも lock の中）。`runtime_command`（`runtime.c` 218〜263）も同じ `node->lock` の中。よって **GPU の上で仕事を持つ channel は常に 1 つだけ**。§3.4 のとおり WFI では無限 loop の draw は切り替えられない。
- 起こる状況: 詰まった run の間、compositor の仕事は GPU の runlist ではなく software の queue で待つ。案 A でも kill は worker が `node->lock` を持って行う（§4.3・§5.1）ので、kill の間も全 session が止まる。§4.1 の表の B の欠点「取り消しの間 全 session が止まる」は A にも同じく当たる。A の GPU の側の利点（timeslice）は p005 a（session ごとの worker、lock の外の待ち）が入るまで 0。一方 A の費用は p002 の 7 LW、session ごとの 約 1 MiB の VRAM と 10 page の driver の page（M9）、open ごとの 6〜8 RPC と first fence の待ち（M13）、chid の管理（M5）、promote の別の VA（R8、実機でしか確かめられない）。
- 補足: executor は push buffer ごとに状態を全部書き直す（`vk-execute.c` 19〜25 の「A draw writes everything it uses」、`execute_room` の「The target is written again in the next push buffer」）ので、案 B の「共有の channel を作り直す」は executor から透過で、流し直しも §4.1 の表のとおり実質不要。誰の run が詰まったかも、run が 1 つしか無いので自明（RC の event もその run の間に来る）。
- 直し方の案: (a) 必須の範囲は案 B（共有の work channel を kill して作り直す）にし、案 A は p005 a と組にしてユーザーの判断にする、または (b) 案 A を残すなら「p002 は p005 a の下地で、p002〜p004 の範囲では compositor の待ちは減らない」と §0・§4.1・D1 に正しく書き、D1 をユーザーに聞き直す。どちらでも kill の手順（STOP・RESET・FREE の判定）、RC の道、`isolate` の論点は同じ。

### M2. 設計の `isolate` は、呼ばれる時にはほぼ必ず失敗する（§0 の 5、§4.6、§4.12 の段 2）

- 根拠: gpu.h 59〜77（isolate は「stop could not be confirmed」の context に対して呼ぶ）。`gpu.c` 7569〜7572・7623〜7630: `isolate` は stop の期限切れか `stop_begin`/`stop_poll` が EAGAIN 以外の error を返した時だけ。design の `stop_poll` は FREED で 0 を返す（その時点で FINISHED、7650〜7651）。よって isolate が呼ばれる時の状態は LOST・STOPPING・STUCK で、design の isolate（FREED か NONE の時だけ 0）は失敗する。成功するのは「最後の poll と期限の間に FREED になった」競争の時だけ。
- 起こる状況: ws.md の完了の条件「共通の層の `isolate`（その session だけ隔離）を実装」は形だけ満たし、働きは「isolate が無い」今と同じ（段 2 は必ず device の fault）。§4.6 の quarantine の list・§5.2 の「session isolated」の log・§6.1 の 5 の試験は実機でほぼ通らない道になる。
- 直し方の案: i915 の形（`src/drivers/gpu/i915/reset.c` 686〜716 の `i915_isolate` は、quarantine を立ててから engine の recover を自分で行い、成功で 0）に合わせ、isolate の成功の基準を「GPU がこの channel をもう走らせない証拠がある」にする。例: STOP_CHANNEL が 0、またはその chid の RC の event を受けた（R1・R2: RC は GSP が channel を runlist から外し engine を reset した後に送る）が、FREE が失敗・未了の時は、資源を返さずに 0（quarantine）。STOP も RC も無い時だけ失敗。さらに isolate の中で kill を同期で行う（monitor の thread で、`node->lock` → `rpc_lock`）案も M3 の解になる。基準は §4.12 の段 2 の議論（「warp が走り続けうる」）を R1・R2 の事実で更新して書く。

### M3. kill を「worker の次の turn」に頼っている（§4.4、§4.6、§5.1 の `runtime_worker` の行）

- 根拠: `runtime.c` 690〜695（worker は queue が空なら寝る）。検出は `nvrtx_vk_gpu_run` の中で、これは worker の frame からだけでなく `runtime_command`（app の ioctl の thread、`runtime.c` 255）からも呼ばれる。libvulkan は `GPU_CAP_NOTIFICATION` があれば `GPU_COMMAND_SUBMIT` を使う（`context.c` 857〜868）ので普段は worker だが、`GPU_COMMAND` を使う client（`context.c` 870〜876 の道や試験の道具）では ioctl の thread で検出される。RC の event も drain の thread で来る。
- 起こる状況: (1) ioctl の thread で期限を検出 → worker は寝たまま → LOST のまま → B1 と同じく 10 s 後に device の fault。(2) RC の event が run の外で来た（または run の後に遅れて来た）時も同じ。(3) S5（close の途中）は drain が pending 0 を待つので worker が frame を終えた後に kill が要るが、frame の完了（retire）と kill の順が §5.1 では曖昧（retire の後なら drain が先に返り、`stop_poll` が EAGAIN を繰り返す）。
- 直し方の案: kill を queue の entry（`RUNTIME_ENTRY_KILL`、session ごとに 1 つ、node の固定の slot）にして `runtime_enqueue` で worker を起こす、または kill 専用の thread を持つ、または M2 の同期の isolate で行う。どれでも「LOST にした所が必ず kill の実行者を起こす」を不変条件として §4.8 に書き、host 試験（queue が空の時の RC の event・ioctl の thread での期限）を足す。

### M4. `rpc_lock` を drain が取り続け、kill・open が待たされる（§4.5、§9 の 5）

- 根拠: `node.c` 863〜879: drain は `nvrtx_rpc_wait` を繰り返し、間に `nvrtx_present_refresh_sample` と `node_perf_report` を挟むだけ。design は turn（50 ms）の間 `rpc_lock` を持つので、持つ時間は実質 100%。`src/kern/lock.c` 359〜440: `mutex_unlock` は FREE にしてから待っている 1 つを起こすだけで hand-off しない。`mutex_lock` は先に `mutex_spin_take` で取る。drain は離した直後に（起こされた thread が走る前に）取り直せる。
- 起こる状況: kill・open・close の `rpc_lock` の待ちが 50 ms の 1 回で済まず、続けて負けうる（推測: scheduler の tick と CPU の数による）。kill が遅れると B1・M3 の 10 s に近づく。
- 直し方の案: drain は lock を持たずに `nvrtx_channel_peek` で「message があるか」を見て、ある時だけ短く lock を取る形にする。または「call を待つ者がいる」印（atomic の counter）を drain が見て turn を早く終え、lock の外で 1 tick 寝る。lock の順と rank（`LOCK_RANK_*`）も §4.10 に書く。

### M5. chid の再利用と遅れた RC の event、session の pointer の寿命（§4.4、§4.8、§5.1、S3）

- 根拠: §5.1 は chid を bitmap から配り、S3 は「chid が再利用される（`channel 16` が毎回）」を合格に含む。RC の event は非同期で、kill の STOP の後や FREE の後に来うる（R1・R2: STOP の timeout の後に GSP が RC する）。`gpu.c` 850〜876: `drv_gpu_report_session_error` は backend の pointer を鍵に `open_sessions` を探す（「private pointer an exact live-session key」）。
- 起こる状況: (1) vkhang の chid 16 を kill → FREE → close → 次の app が chid 16 を得る → 前の channel の遅れた RC の event が来る → handler が chid 16 → 新しい session を喪失にする（無実の app が DEVICE_LOST）。(2) handler が `queue_lock` の下で session を引いた後、lock を離してから report する形だと、その間に `node_close` が `kern_free(session)` し、同じ番地に新しい session が来ると別の session を失わせる（ABA）。
- 直し方の案: chid を順に回して直前に使った chid をすぐ再利用しない（S3 の合格の基準も「chid が 16〜47 を回る」に直す）、`channels[]` から外すのは FREED の後の close の時、event は `channel->state == READY` か `LOST`（kill 前）の時だけ効かせる、report は `queue_lock` の下で session を確かめたまま呼ぶ（`queue_lock` → `gpu_registry_lock` の順。gpu.h の「No callback runs under a core spinlock」から逆の順は無い）か、session に世代の番号を持たせる。
- 付記（minor）: `exceptType` 141（`ROBUST_CHANNEL_FAST_PATH_ERROR`、ogkm `nverror.h` 136）は kernel RM が `NV_RM_RPC_ECC_NOTIFIER_WRITE_ACK` で答える特別な event（ogkm `kernel_gsp.c` 538〜544）で、chid は session の物と限らない。§4.4 の「exceptType ごとの扱いは区別しない」から外し、log だけにする（ack を送らない時の GSP の振る舞いは未確認。RTX 2070 は ECC が無いので来ない見込み、推測）。

### M6. U2・S1 の probe は、今の作りでは GSP の ctxsw の timeout を起こせない（R4、U2、U3、Q250-1、§6.3 の S1）

- 根拠: R4 自身の条件「無限 loop の channel と compositor の channel が同じ runlist にあり timeslice の切り替えが要ると」。M1 のとおり、run は 1 つずつで、詰まった run の間に compositor の channel には GPU の仕事が無い（software の queue で待つ）。runlist に走れる channel が 1 つだけなら context switch の要求が出ず、ctxsw の timeout も起きない（推測: hardware の挙動の一般論。firmware は未確認）。
- 起こる状況: U2（取り消しの無い build で vkhang を 10 s）は「RC の event が来ない」を観測し、「GSP は自分で見張らない」と誤って結論しうる。U3（registry で短くする）も同じ理由で効果を測れない。Q250-1 の (b)「GSP の RC は back-up」は、p002〜p004 の作りでは back-up として働かない。
- 直し方の案: probe の build に「詰まった run の間に別の channel（driver の chid 4 の work channel か 2 つ目の session の channel）へ短い push（fence だけ）を `node->lock` の外から出す」道を入れ、その fence が進むか・RC の event が何秒で来るかを見る。U2 の記録には「他の channel に pending の仕事があったか」を必ず書く。Q250-1 の推しの理由もこの前提で書き直す。

### M7. `vkhang` の loop は無限でない（§6.3「試験の app `vkhang`」）

- 根拠: design の形「`while (x != 0) x = x * 3 + 1;`（uniform から来る値）」。32 bit の符号無しでは x_n = 3^n·x0 + (3^n − 1)/2 (mod 2^32) で、x = 0x55555555 の次が 0 になる。x0 = 1 なら 3^n·3 ≡ 1 (mod 2^33) となる n = 2^31 − 1 回で 0 に着く（3 の位数は 2^31、3^{-1} は 3 の生成する群に入る）。1 pixel あたり約 21 億回で、GPU の 1 回が数 cycle なら数秒〜十数秒で終わる（推測: nvrtx の compiler が出す命令数と clock による）。
- 起こる状況: S1・U2 の 10 s の観測の途中で shader が自然に終わり、RC の有無・GSP の振る舞いの結論を誤る。S2 は 2 s で切るので通るが、「無限」の試験になっていない。
- 直し方の案: 既にある `userland/tests/vkreset` の「hang.comp with an odd limit」の形（偶数ずつ増える counter を uniform の奇数と比べる、`for (uint i = 0; i != u.limit; i += 2)`）を fragment に移す。compiler が loop を消さないよう結果を色に使う。`vkreset` は compute で nvrtx は compute を拒む（`vk-pipeline.c` 756〜771）ので、新しい app は要るが、手本と出力の形を `vkreset` に揃え、§6.3 に参照を書く。

### M8. kill の「止まった」の判定が本文と R1 で食い違い、SCHEDULE の失敗で STUCK になる（§4.3 の 3b〜3d、§4.8、§5.1 の `nvrtx_channel_kill` の comment、R1、R2）

- 根拠: 本文 §4.3 の 3d と §4.8 の図・§5.1 の comment は「全部の status が 0 なら FREED」、3b は RESET を常に呼ぶ。R1 は「STOP が 0 か、失敗して RC が来たら FREE へ、FREE が 0 なら FREED」、R2 は「RESET は STOP が失敗し RC が来ない時だけ」。ogkm `ctrla06fgpfifo.h` 199〜205: STOP_CHANNEL は「disabling and unbinding the channel and removing it from runlist」。その後の `SCHEDULE`（bEnable=0、§4.3 の 3c）は外された channel に対する control で、`NV_ERR_INVALID_STATE` を返しうる（推測: GSP の中の扱いは未確認）。
- 起こる状況: 本文どおりに作ると、STOP が成功しても SCHEDULE off の失敗で STUCK → B1 の段 2 → device の fault。
- 直し方の案: 判定の本体を R1 に一本化し、「FREE の status（channel の free）が証拠。SCHEDULE off・object の free の失敗は log するが STUCK にしない」と書く。§4.3・§4.8・§5.1 の本文を R1・R2 の形に書き直す（M12）。STOP が 0 を返して RC も来る組み合わせ（header のとおり preempt の timeout の時は RC してから 0 を返しうる）も表に入れる。

### M9. session の channel の VA の割り当てが未設計（§4.1、§4.9、§5.1、§9 の 4、R8）

- 根拠: `work.c` 70〜75: ring・semaphore・fence の push は固定の `WORK_RING_VA` 等（`NVRTX_WORK_VA_DRIVER` から）、`work_tables`（784〜862）が起動の時に 1 回だけ map する。`WORK_DRIVER_PAGES` = 2 + 32 KiB/4 KiB = 10 page、DRIVER の区画は `NVRTX_WORK_VA_USER − NVRTX_WORK_VA_DRIVER` = 1 MiB = 256 page（`nvrtx-work.h` 39〜40）。own の context buffer は golden の VA に **privileged** で map されている（`work_tables` の `nvrtx_mmu_gr_map(…, buffer->read_only)` と comment「privileged at the golden context's addresses」）。`nvrtx_work_map_vram`（330 行〜）は「neither privileged nor read-only」。
- 起こる状況: §5.1 の「space は gpu の物、`nvrtx_work_map_vram / map_system / unmap unchanged`」のままでは、(1) session の ring 等を同じ固定の VA に置けない、(2) DRIVER の区画に 33 channel 分（330 page）は入らない、(3) own の context buffer を user の heap に普通の map で置くと privileged にならない（user の shader から読み書きできる。GSP がそれを許すかも未確認）、(4) §9 の 4 の「`NVRTX_WORK_VA_USER` の下に channel ごとの 2 MiB の区画」は USER の下には DRIVER の 1 MiB しか無く場所が無い。
- 直し方の案: p002 の設計に「channel ごとの VA の区画（ring・semaphore・fence の push・own の context buffer）を user の heap から 2 MiB 単位で取り、privileged の map の関数（`nvrtx_work_map_vram` の privileged 版）を足す、close で返す」を明記し、`work_tables` の範囲の検査（792〜800 行は golden の VA だけを見る）を session の区画にも広げる。M1 で案 B を採るならこの項目は不要。
- 付記: vaspace の共有は「他の app は動き続ける」の約束の範囲を狭める。shader が任意の VA に書けば、同じ space の他の session の blob・ring・semaphore を壊せる（今の 1 本の channel でも同じで回帰ではない）。§2 か §9 に「この WS は終わらない仕事の取り消しで、悪意のある・壊れた shader による他 session の memory の破壊は防がない（space の分割は別の課題）」と書く。

### M10. `struct nvrtx_channel` と `nvrtx_channel_*` は既にある（§5.1）

- 根拠: `nvrtx-gsp.h` 456 の `struct nvrtx_channel`（GSP の command・status の queue）、`nvrtx-gpu.h` 613 の `gpu->channel`、`channel.c` の `nvrtx_channel_start`・`stop`・`release`・`send`・`peek`・`receive`（`nvrtx-gsp.h` 892〜894）、`rpc.c` 144・204・214 がそれを使う。
- 起こる状況: §5.1 の型と関数をそのまま書くと再定義で build が通らない。名前を変えずに GSP の queue の側を改名すると、rpc.c・channel.c・gsp.c の広い変更になり WS291・WS242 の衝突の範囲が広がる。
- 直し方の案: 新しい物を `struct nvrtx_fifo`・`nvrtx_fifo_open/close/kill/submit/wait`（または `nvrtx_gr_channel_*`）にし、§5.1・§4 の本文・§5.2 の log の語をそれに合わせる。§5.1 の注「名前は実装の Phase で決める」だけでは、§4.8・§4.9・§4.10 の本文がこの名前を前提にしているので足りない。

### M11. 実機の要る Phase の範囲と依存（第 2 版の Phase の表、§7、R12、Q250-2）

- 根拠: 第 2 版の表は「実機」を p003 の依存にだけ書く。しかし §7 の p002 の受け入れ条件は「desktop の回帰（T1: … `channel 16: session channel` の行）」と run の時間の比較で、実機が要る。R8（別の VA の promote）は §9 の 4 のとおり「p002 の最初の実機の確認」でしか分からない。p003 の依存から §7 にあった「p004 の vkhang（S1 に要る）」が落ちている。R12 のとおり RTX 2070 は今どの試験の台にも挿さっていない。
- 起こる状況: p002 を host 試験だけで cleared にできず、実機が来るまで p002〜p004 の全部が止まる。または p002 を実機で確かめずに main に入れ、RTX の desktop の回帰（promote が通らない、open が落ちる）が実機の再開の時に初めて出る。p003 を始めた時に vkhang が無い。
- 直し方の案: 表を「実機なしで cleared にできる物（host 試験・build・QEMU の起動の回帰）」と「実機の関門」に分ける。例: p002a（channel の構造体の一般化と rpc_lock、driver の 1 本のまま、host 試験だけで cleared）、p002b（session の channel、実機の関門）。p003 の依存に p004 の app の部分を戻す。Q250-2 に「VFIO の passthrough なら GPU の FLR で S1 の破壊的な試験の後に host から戻せる（推測: WS305 の準備しだい）」を利点として足す。

### M12. 第 2 版を「上に重ねる」形で、本文に矛盾が残っている（文書全体）

- 根拠と例:
  - §4.3 の 3b（RESET を常に呼ぶ）・3d（全部 0）・§4.8 の図・§5.1 の comment ↔ R1・R2（M8）。
  - §0 の 6・§4.7・§4.14・D6「TSG を作らない限り優先の口が無い」↔ R7（channel 単位の `SET_INTERLEAVE_LEVEL`）。
  - §0 の 7・§4.13・D7・§4.7・D3（WS241 の round の後）↔ R11（WS241 は canceled、WS291・WS242）。
  - §6.3「RTX 2070、10.0.30.5、方法 A」「T1 の lock」↔ R12。
  - §0 の 8・§7（必須 17〜22 LW）↔ 第 2 版の表（21 LW）。
  - §0 の 2・§1 の表の gpu.c の行（7028〜7271、5159〜5160、5486〜5488、7190〜7240）は今の tree では 7177（`gpu_session_fail_locked`）・7430（`gpu_monitor_step`）・7681（`gpu_monitor_close`）・5562・5890。§4.12 の `i915_isolate` 842〜874 行は今は 686〜716 行。§1 の `runtime.c` 573〜667 は 574〜670。
- 起こる状況: 実装の担当が本文を読んで R を見落とすと M8 の誤りをそのまま作る。phase.md の「結果」と ws.md の Phase の表・完了の条件も第 1 版のまま（`isolate` を実装、10.0.30.5）。
- 直し方の案: p001 を閉じる前に、R1〜R12 を本文の該当の節に溶かし込み、「第 2 版の改訂」の節は変更の一覧だけにする。行の番号を今の tree で付け直す。ws.md・phase.md の写しは P5 が同じ commit で直す（AGENTS.md §8 の本体と写し）。

### M13. 全ての open に channel を作る費用と失敗（§4.1 の案 A の「`node_open` で session の channel を作り」、§3.5、`NVRTX_SESSION_CHANNELS_MAX`）

- 根拠: `node.c` 427〜451 の `node_open` は今は RM を呼ばず lock も取らない。design では open ごとに arena・buffer・channel の allocate・promote・3D と compute の object・bind・schedule・first fence（`work.c` 1017〜1063 の 2 s までの待ち）を `node->lock` → `rpc_lock` の下で行う。/dev/gpu0 の open は Vulkan の app だけでなく、display の lease・capture（`keiland-shot`）・blob の共有だけの client もする（`node.c` 46〜51 の capability の一覧）。
- 起こる状況: (1) 33 個目の open が EBUSY になり、render しない client まで落ちる。(2) app の起動のたびに worker の frame が `node->lock` の待ちで数 ms〜数十 ms 止まり（first fence を含む）、compositor の frame が遅れる（WS241 で測った run の平均 0.16 ms に比べて大きい）。(3) fault の後（`node->faulted`）や `nvrtx_node_stop` の後（`node->broken`）の open・close が死んだ GSP に RPC を投げる（`rpc_unanswered` で短くはなるが、§4.8 に道が無い）。
- 直し方の案: channel は executor の最初の run（`nvrtx_vk_gpu_run` の初回）で遅延して作り、render しない session は NONE のまま（§4.6 の isolate の NONE の扱いと合う）。上限に達したら open ではなく submit を ENOSPC/EBUSY にする。faulted・broken の時は channel を作らず close も RM を呼ばない、を §4.8 に足す。

## Minor

- **m1. STOP_CHANNEL の param の大きさ**（§5.1）: 「1 byte bImmediate (4 with padding)」は誤り。`NVA06F_CTRL_STOP_CHANNEL_PARAMS` は `NvBool` 1 つで sizeof は 1（ogkm `ctrla06fgpfifo.h` 226〜228）。nvrtx の既存の `NVRTX_SCHEDULE_PARAMS_BYTES` は 2（`NvBool` 2 つ、`nvrtx-gsp.h` 689）で sizeof どおり。RM の control の dispatch が params の大きさを型の sizeof と照らすので、4 を送ると `NV_ERR_INVALID_PARAM_STRUCT` になる見込み（推測: 照合の code は手元の写しに無い）。RESET_CHANNEL の 16 byte（u32×3＋NvBool、末尾の padding 込み）は sizeof と一致する。§6.1 の 2 の layout の試験に「大きさが sizeof と一致」を入れる。
- **m2. `drv_gpu_report_session_error` を呼ぶ場所**（§4.3 の 2、§4.4）: drain の handler・kill の中の `nvrtx_rpc_wait` の callback から呼ぶ時の lock を明記する（M5 の ABA。`queue_lock` の中で呼ぶなら順は `queue_lock` → `gpu_registry_lock`）。`session->lost` の検査に `runtime_commit`（`runtime.c` 434〜461）も入れる（gpu.c が先に断る見込みだが、backend の側でも一貫させる）。
- **m3. config**（§4.2、D2）: `config/kernel-options.list` の platform の欄は nvrtx が amd64 だけなので `amd64`（他の GPU の項目は `*`）。`CONFIG_GPU_JOB_STOP_MS` は 1 s も選べるので、「kill の最悪の時間 < `CONFIG_GPU_JOB_STOP_MS` の最小」を不変条件として §4.6 に書くか、nvrtx が有効な時の choice の下限を設計に書く。
- **m4. kill の時間の見積もり**（§4.3、§4.6、§9 の 6・9）: 「call の timeout を 1 s にすれば 6 s」は B2 で使えない。5 s の timeout では、他 session の詰まった run の 2 s＋STOP の 5 s（timeout なら以後 `rpc_unanswered` で即失敗）＋R1 の RC の待ち 500 ms で 7.5 s 程度、10 s に近い。R1 の 500 ms の待ちの間に誰が message を読むか（kill の thread が `rpc_lock` を持ったまま `nvrtx_rpc_wait(NODE_DRAIN_NO_FUNCTION, …, 500 ms)` を回すのか、lock を離して drain に任せるのか）も未記載。RC の event は STOP の reply より前に同じ queue に来るので、kill 自身の wait の中で受けられる見込み（推測）。
- **m5. FREE を証拠にすることと既存の comment**（§4.3 の 4、§4.11）: `work.c` 296〜298 の `nvrtx_work_release` は「Only once the GPU no longer reads them: after the GSP is stopped」で、今の driver は FREE を信じていない。kernel RM は GSP の FREE の RPC の後に method buffer を返す（ogkm `kernel_channel.c` 1192〜1210、RPC の status を見ずに続ける）ので、design の「FREE の後に返す」は RM と同じ形で筋は通る。ただし RM の destructor は失敗を握りつぶす作りなので、「FREE が 0 ⇒ warp が止まった」は GSP の中の preempt の結果を保証しない（推測）。R1 の「STOP が 0 か RC を受けた」と組で証拠にする（M2・M8）。実装の時に `nvrtx_work_release` の comment も直す。
- **m6. user の ioctl の resource destroy**（§4.3 の 5、§4.9）: `gpu.c` 2624〜2715 の `gpu_destroy_ioctl` は session の stop の状態を見ずに `resource_destroy` を呼び、`node_resource_destroy` → `nvrtx_node_backing_put`（`node.c` 290〜312）は最後の ref で VRAM を返す。DEVICE_LOST を受けた app が kill の前に blob を destroy しても、executor の VkDeviceMemory が ref を持つ（`vk-node.c` 100〜126）ので VRAM は残る見込み（推測）。この不変条件（「channel が FREED になるまで、その session が GPU に書かせうる VRAM は executor の ref で残る」）を §4.9 に書き、host 試験で destroy → kill の順を確かめる。
- **m7. 出典の規則**（guardrail「MIT・GPL の出典のデータの引用」）: 本文と R の引用の形（版・path・関数か表の名前・行）は規則どおり。nouveau の引いた file はどれも MIT の許諾文（Red Hat、`nouveau_sched.c`・`nouveau_exec.c` は SPDX MIT）で、§10 の主張と合う。指摘は 3 つ: (1) `NVRTX_RC_FALLEN_OFF_THE_BUS` は `ROBUST_CHANNEL_GPU_HAS_FALLEN_OFF_THE_BUS` の symbol の字面に近いので独自の名前にする（例 `NVRTX_RC_BUS_GONE`）。`NVRTX_CTL_STOP_CHANNEL` 等は既存の `NVRTX_CTL_SCHEDULE` の流儀と同じだが、p006 の監査の対象に明記する。(2) U3 で registry の表（`records.c` 99〜103）に key を足すなら、今の block の「Values referenced: … nouveau r535 gsp.c」は nouveau の値の出典なので、ogkm `nvrm_registry.h` の値の block を別に付ける（file の先頭に書かない）。(3) §3.2 の `../ws037/temp/channel-mmu-display.md` と `plan/ws250/temp/ogkm-src/` は git に入らない temp で、文書の link が壊れる（`record-audit/links.py` が拾う見込み）。link を外して「commit しない調べ」と書く。
- **m8. 試験の計画**（§6）: (1) §6.1 の 1〜5 は B1・B2・M3・M4・M5 を見つけられない。足す物: gpu.c の close の順の model（B1）、遅れた reply（B2）、queue が空の時の RC と ioctl の thread での期限（M3）、chid の再利用と遅れた RC（M5）、`rpc_lock` の公平さ（M4、host の thread で）。(2) S1（取り消し無し）は破壊的で、全 session が順に 2 s で落ちて desktop が死ぬ見込み（M6 の別の channel の仕事を出すなら確実）。S1 の後は reboot か VFIO の reset が要る、と手順に書く。(3) S3 の「VRAM の allocator の残りが戻る」の読み方（どの log の行か）が無い。(4) S2 の合格は「desktop 継続」だが、M1 のとおり session ごとの channel が同時に GPU の上で動く場面を作っていないので、案 A の利点の確かめにならない。
- **m9. RC の event の chid の範囲**（§3.5、R6）: GSP-RM が自分の内部の channel に使う chid の範囲は公開の source では分からない（nouveau の r570 は `rsvd_chids = 1`、`nvkm/subdev/gsp/rm/r570/fifo.c` 212）。16〜47 は今の 1〜4・8 と同じく通る見込みだが、open の status の失敗の時の log に chid を出す（§5.2）。
- **m10. driver の chid 4 の work channel を残す理由**（§4.1、§5.1 の comment「4 for the driver's own」）: 案 A では executor の run は全部 session の channel に行き、chid 4 に仕事を出す者が無くなる（`work_first` の初回の push だけ）。残すなら用途（M6 の probe、将来の driver の内部の copy など）を書く、無ければ 1 MiB と RPC を省く。

## 確かめて正しかった主張（抜粋）

| 主張 | 確かめた所 |
| --- | --- |
| `VK_NODE_RUN_US` 2 s（`vk-node.c` 31・291）、`WORK_CHID` 4（`work.c` 81・936）、`nvrtx_work_stop`（`work.c` 224）、RC の event の枝（`rpc.c` 252）、`WORK_ENTRY_WAIT_US` 2 s（`work.c` 111） | worktree の source |
| nvrtx の recovery の ops は `isolate`・`reset` が NULL（`runtime.c` 93〜100） | 同上 |
| RC の event の offset（engine 0、chid 4、type 16、fault 28・32・36）は `rpc_rc_triggered_v17_02` の物 | `rpc.c` 88〜103 の block の出典 |
| STOP_CHANNEL の意味（disable・unbind・runlist から外す、preempt の失敗・timeout で RC）と番号 0xa06f0112 | ogkm `ctrla06fgpfifo.h` 199〜228 |
| STOP_CHANNEL は GSP client なら RPC で転送し、その後 `kchannelNotifyRc_HAL`（R1・§3.2 の表） | ogkm `kernel_channel.c` 1995〜2032 |
| RESET_CHANNEL の欄・reason の値・`bIsRcPending` を kernel RM が自分の記録で上書き | ogkm `ctrl906f.h` 106〜148、`kernel_channel.c` 3146〜3192 |
| `RmEngineContextSwitchTimeoutUs` 既定 0x003fffff（1.024 µs 単位、約 4.3 s、0 で無効）、`RmRobustChannels` は key が無い時に enable（header の DEFAULT は DISABLE だが `kernel_rc.c` 72〜83 が key の無い時に ENABLE にする）、`RmWatchDogTimeOut` 既定 7 | ogkm `nvrm_registry.h` 1365〜1408、`kernel_rc.c` 60〜200 |
| GSP client の chid の渡し方（INDEX_FIXED=FALSE、PAGE_FIXED=TRUE、chid%8・chid/8）と nvrtx の `nvrtx_channel_params` が同じ形 | ogkm `kernel_channel.c` 2833〜2850、`objects.c` 471〜510、nouveau `r535/fifo.c` 82〜131 |
| nouveau の GSP の道の RC は software の errored だけ（`r535_chan_stop` が空、`r535_fifo_rc_chid` → `nvkm_chan_error`） | nouveau `r535/fifo.c` 50〜58・345〜374 |
| `SET_INTERLEAVE_LEVEL` 0xa06f0109 は channel 単位（TSG なら全 channel） | ogkm `ctrla06fgpfifo.h` 110〜139 |
| `FIFO_DISABLE_CHANNELS` は GSP client で RPC に転送、`pRunlistPreemptEvent` は kernel client だけ | ogkm `kernel_fifo_ctrl.c` 713〜750 |
| RC の処理（Channel Teardown）は GSP-RM の中、kernel RM は journal と通知だけ（R2） | ogkm `kernel_gsp.c` 507〜640 |
| UAPI・`gpu.h`・HAL を変えない | 設計のどの節も gpu.h の宣言・契約を変えない（`isolate` の使い方は契約の範囲の中）。HAL の API の変更は不要（承認の要る差分は無い） |
| 共通の層の期限は supervised job だけで、frame には無い | `gpu.c` 5562・5890（reserve・commit の時だけ `job_deadline`） |

## 確かめられなかった事（推測のまま）

- GSP-RM 570.144 の firmware の中の preempt・RC・engine reset の手順と時間（公開の source に無い）。B2・M6・M8・m4・m5 の一部はこれに依る。
- RM の control の params の大きさの照合（m1）: 照合の code は手元の写し（`src/nvidia/src/kernel/rmapi/` の一部）に無い。
- `vkhang` の loop の実時間（M7）: nvrtx の compiler の命令数と clock による。
- `mutex` の取り直しで drain が勝ち続けるか（M4）: scheduler と CPU の数による。host の thread で測れる。
- 実機の試験は一つも行っていない（RTX 2070 は今挿さっていない、R12）。

## LW の見積もりについて

第 2 版の必須 21 LW（1 LW ≈ 15〜25 分で 5〜9 時間）には、B1・B2・M2・M3・M4・M5・M9 の直しと、それを見つける host 試験（m8）が入っていない。案 A のままなら p002 に +2〜3 LW（VA の区画・privileged の map・遅延の作成）、p003 に +3〜4 LW（kill の実行者・RPC の照合・chid の管理・isolate の基準）、host 試験に +2 LW の見込み。実機の待ちと U1〜U5 の build・reboot の往復（各数 LW）は別。案 B を採れば p002 は 2〜3 LW に縮む見込み（channel の構造体を増やさず、kill と作り直しの関数を足すだけ）。
