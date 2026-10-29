<!-- awesome-plan project=zedbsd record=ws250-design -->

# WS250 設計: nvrtx の終わらない GPU の仕事の取り消しと復帰（system は動き続ける）

第 6 版（2026-10-29 P5、q1676-i01。第 5 版を design-reviewer の [design-review-v5.md](design-review-v5.md)（blocker 2・major 7・minor 12）で直した。対応は 12 節）。第 5 版はWS291 p011（P2）が main に入れた非同期の submit（`vk-node.c` の submit・wait の分離、
`runtime.c` の completer、`work.c` の `nvrtx_work_passed`）の上に作る形に、第 1 版〜第 4 版（P4・P5、2026-10-15・10-29）を一本に書き直した。
第 1〜4 版の本文・改訂の表（R1〜R12・T1〜T13・W1〜W5）と design-reviewer の指摘（[design-review.md](design-review.md) の B1・B2・M1〜M13）は
この版の該当の節に溶かした（対応は 12 節）。前の版の全文は git の履歴にある。code は書かない。読んだ code は main `69c15ae94`。

## ユーザーの決定（原文）

- 2026-10-15「その場合のキャンセルと復帰を実装した方がよくない？device lostでプログラムがそれ以上動けなくなるのはOK。でもシステムとしては動き続けてほしいな。」
- 2026-10-27「すぐ取り組むものの一つです。」
- 2026-10-29（クリック、Q1 の中継）: Q250-1「nvrtx の 2 s で STOP (推し)」、Q250-4「案 A を必須に」、Q242-4「RDNA と差し替え (推し)」（WS250 の実機も同じ）、Q250-5「含める (推し)」。
- 2026-10-29（Q1 の中継）「作業の手が空いてしまったら、テストなしで実装だけ進める戦略で、残っているWSのphaseをまんべんなく進めましょう。」

解釈: 主の検出は nvrtx の 2 s の期限で、GPU の上の取り消しは STOP_CHANNEL。GSP 自身の RC は同じ session の喪失として受ける back-up。
session ごとの channel（案 A）と session ごとの worker を必須に含める。実機は RTX 2070 を RDNA と差し替えた素の起動（10.0.10.3、
`plan/tools/gpu-perf/machines/rtx.conf`）。今は実装だけを進め、試験（実機）は後で Q1 が割り当てる。

## 0. 要約（この版が決めたこと）

| # | 決めたこと | 節 |
| --- | --- | --- |
| 1 | **session ごとの channel**（`struct nvrtx_work_channel`）を session の最初の frame で遅延して作る。vaspace（page table）は今の 1 本を共有し、channel ごとに user の heap から 2 MiB の VA の区画を取って ring・semaphore・fence の push・own の context buffer を置く。作れなければ（RM の拒否・32 個の上限）その session は今の共有の work channel（chid 4）を使う（今と同じ振る舞いへの退避） | 4.1、4.2 |
| 2 | **犯人の手がかり**: 主は GR に今載っている context（FECS の current context の register と channel の instance block の照合、4.3）。読めない時の代わりに、全ての submit の segment の前に置く待たない release（start stamp）で「始まったが終わらない run」とその始まりの時刻を見て、**最後に始まった channel** を犯人にする（WFI の preempt で GR を取った者が最後に始まる） | 4.3 |
| 3 | **判定は reaper だけ**: 待つ側（executor・completer）は期限（submit か最後の取り消しの終わりから 2 s）で reaper を起こし、自分の channel が READY の間は待ち続ける（上限は同じ起点（submit か最後の取り消しの終わり）から 8 s、取り消しの間は数えない。越えたら自分を喪失。2026-10-29 実機の S4 で直した）。reaper が「期限を過ぎた run がある」時に犯人を 1 つ選んで取り消す。判定は hardware に触らない関数（`recover.c`、host 試験に scheduler の model） | 4.4 |
| 4 | **回復の thread**（node に 1 本、`reaper`、node の寿命の間）: 250 ms ごとの見張りと、取り消しの唯一の実行者。取り消しは STOP_CHANNEL（bImmediate=1）→ 失敗なら RC の event を 500 ms 待つ → 来なければ RESET_CHANNEL → 証拠があれば SCHEDULE off・object の FREE・channel の FREE。RPC の timeout は今の 5 s と `rpc_unanswered` のまま。共有の channel（退避）の犯人は共有の channel ごと取り消し、共有の session を全部喪失にする | 4.5 |
| 5 | **RC の event**（`NVRTX_EVENT_CHANNEL_ERROR`）を chid で channel に結ぶ: その channel の session を喪失にし（`run_lost`）、rc の印を立て、reaper を起こす。handler は queue_lock だけで動く。type 79（bus から落ちた）は device の fault | 4.6 |
| 6 | **`rpc_lock`**: runtime の RM の call（channel の open・close・kill）と drain の turn を直列にする mutex。drain は 20 ms の turn ごとに手放し、待つ者がいれば 1 tick 寝て譲る | 4.7 |
| 7 | **共通の層の ops**: `stop_begin` は今のまま（kill を頼まない）、`stop_poll` は channel に終わらない run があるか取り消しの途中なら EAGAIN、`isolate` は今の state だけで答える（FREED・STOPPED・NONE・終わらない run の無い channel は quarantine して 0、他は EIO で device の fault。取り消しはしない: 実行者は reaper だけ）、`reset` は作らない | 4.8 |
| 8 | **session ごとの lane**（worker と completer の対、Q250-5）: channel の番号ごとに 1 組の thread を node の寿命の間持ち（最初に使う時に作り、終わらせない: thread の終わりを待つ API が無い）、その番号の channel を持つ session の frame・marker を扱う。共有の session は今の node の worker・completer。executor の待ちのうち slot の再利用と CPU の copy の前の待ちだけ `node->lock` を手放す（destructor の中の idle は手放さない）。session の executor は `exec_lock` で守る | 4.9 |
| 9 | UAPI・`include/drivers/gpu/gpu.h`・HAL・libvulkan・`vk/nvrtx-vk.h` の口は変えない（host 試験 `nvrtx-vk-host-test.sh` の stub はそのまま通る） | 5 |
| 10 | Phase: p002 channel（8 LW）、p003 検出と取り消し（12 LW）、p004 実機の scenario の script（2 LW）、p005 lane（10 LW）、p006 reset（選択肢、作らない）、p007 規約の全文と出典の監査（2 LW）。必須 34 LW | 7 |

## 1. 今の振る舞い（main 69c15ae94 の code の事実）

| 事実 | 所在 |
| --- | --- |
| executor は push buffer を `nvrtx_vk_gpu_submit` で work channel に出して待たない。session は最新の run の fence（`run_fence`）と submit の時刻（`run_us`）を持つ。待ちは `vk_node_wait`: submit から `VK_NODE_RUN_US`（2 s）まで、越えたら `run_lost` を立て EIO。以後その session の submit・待ちは即 EIO | `vk-node.c` 37〜38・372〜418・573〜633 行 |
| executor が `node->lock` の中で GPU を待つ所: slot の再利用（最も古い slot の run）、CPU の copy の前（`execute_drain`）、memory の detach と run の free（`nvrtx_vk_gpu_idle`）、vkQueueWaitIdle・vkDeviceWaitIdle | `vk-node.c` 131〜166・324〜370・552〜571 行、`vk/vk-execute.c`（slot 3 個）、`vk/vk-device.c` |
| renderer は node に worker 1 本と completer 1 本。frame は worker が `node->lock` の中で executor に渡す。marker は session の最新の run が済んでいなければ park し、completer が **park の順に**（session をまたいで）`node->lock` の外で待って completion する | `runtime.c` 8〜27・144〜174・694〜765・787〜888 行 |
| work channel は GPU に 1 本（chid 4、graphics runlist、ring 512 entry、fence は channel の通し番号、0 を飛ばす）。全 session の run がこの ring に順に並ぶ。timed（`nvrtx.vk.perf=1`）の時だけ segment の前後に stamp（start は待たない release、GPU の timer つき）を置き、semaphore の page の 64 個の record（fence の下位 6 bit）に書く | `work.c` 8〜31・48〜68・520〜626・634〜683 行、`objects.c` 518〜585 行 |
| vaspace は driver の 1 本（`NVRTX_HANDLE_WORK_SPACE`、3.5 GiB、table は work の arena の中）。context buffer は golden context の VA に privileged、driver の page（ring・semaphore・fence）は `NVRTX_WORK_VA_DRIVER`、残りを user の run に heap で配る。run の map は table の PTE を PRAMIN で書いて TLB を落とす | `work.c` 870〜950・1157〜1185 行、`nvrtx-work.h` 37〜40 行 |
| own の context buffer（約 1 MiB、実機 `own buffers f5000 bytes`）は work の arena の中、promote は golden の VA（`gr->virtual_address`）と channel の物理を書く | `work.c` 784〜829 行、`gr.c` 545〜610 行 |
| 共通の層の ops: `stop_begin`（`stopping`）、`stop_poll`（pending が 0 か）、`fault`。`reset`・`isolate` は NULL | `runtime.c` 104〜111・587〜686 行 |
| GSP の channel error の event（0x1004）は `nvrtx_rpc_wait` が log に出して捨てる。node の drain の thread が 1 s の turn で message を読み続ける。runtime に RM の control を送る道は無く、drain が GSP の唯一の読み手 | `rpc.c` 248〜275 行、`node.c` 60〜70・893〜935 行 |
| `nvrtx_rpc_wait` は reply を function だけで照合する（sequence の違いは log だけ）。timeout は `rpc_unanswered` を立て、以後の call を全部断る（遅れた reply を後の call が拾わない唯一の仕組み） | `rpc.c` 219〜228・466〜505 行 |
| 実機（WS291 p011、2026-10-29、10.0.10.3 の RTX 2070）: vkhang は 2 s で DEVICE_LOST、その後の vkhang --loops も lost、mview は描かず desktop の shot も取れない（共有の work channel に終わらない run が残るため） | [ws291-p011](../ws291/phase011/phase.md) の「実機」 |
| 試験の app `vkhang`（3x+1 を 0 から: 0 は動かない点で終わらない）と `measure.sh hang`（rtx の machine の conf もある）は既にある | `plan/ws039/tests/vkhang/`、`plan/tools/gpu-perf/measure.sh` 44〜46・340〜460 行、`machines/rtx.conf` |
| 共通の層は **全ての final close** で `gpu_session_fail_locked(ENODEV)` → drain → 期限の監視の中で `stop_begin` → `stop_poll` を呼ぶ。stop の期限（`CONFIG_GPU_JOB_STOP_MS` 既定 10 s）を過ぎると `isolate`、無いか失敗なら device の fault | `gpu.c` 1530・1558〜1565・7546〜7640 行（[design-review.md](design-review.md) の B1 で確かめた行） |

## 2. 責務と境界

| 層 | 責務 | 触らない物 |
| --- | --- | --- |
| 共通の GPU の層（`gpu.c`、`gpu.h`） | session の喪失の公開、close の stop の確認、isolate・fault の呼び出し | 変えない（UAPI も） |
| nvrtx の renderer（`runtime.c`） | session ごとの lane（frame・marker）、ops（stop・isolate）、reaper | queue の entry の形、marker の slot |
| nvrtx の executor の口（`vk-node.c`） | 期限と犯人の判定の呼び出し、`node->lock` の外の待ち、喪失した session の memory の扱い | `vk/nvrtx-vk.h` の関数の形 |
| nvrtx の channel（`work.c`、新 `recover.c`） | channel の open・close・kill、submit・passed・待ち、始まりの印、犯人の判定（hardware に触らない純粋な関数は `recover.c`） | space・PTE の形式 |
| nvrtx の RPC（`rpc.c`、`node.c` の drain） | `rpc_lock`、RC の event の配送 | RPC の形式・sequence の扱い・timeout |
| display（`display/`） | 変えない（P2 の所有）。runtime に RM を呼ぶ道があれば `rpc_lock` を取る必要がある（9 節の 8、Q1 に連絡） | — |
| HAL・libvulkan・compositor | 変えない | — |

## 3. 調べた事実（公開の source、値と手順だけ）

正本と license は [ws037-license-audit](../ws037/nvrtx-license-audit.md): Linux v6.19 nouveau（MIT の file だけ）、open-gpu-kernel-modules 570.144（MIT）、
Mesa 25.3.6（MIT）。実装では block ごとに「Value(s) referenced:」「Procedure referenced:」の複数行の comment で示し、code・comment・構造・symbol の名前は写さない。

| # | 事実 | 出典（project・file・行） |
| --- | --- | --- |
| F1 | `NVA06F_CTRL_CMD_STOP_CHANNEL` 0xa06f0112、param は `NvBool bImmediate` の 1 byte。意味: disable・unbind・runlist から外す、preempt か外すのに失敗したら RM がその channel を RC する。bImmediate=1 は待たずに強制に preempt、timeout なら RC。GSP client では RPC で GSP へ転送 | ogkm `src/common/sdk/nvidia/inc/ctrl/ctrla06f/ctrla06fgpfifo.h` 204〜230 行、`src/nvidia/src/kernel/gpu/fifo/kernel_channel.c` 1995〜2032 行 |
| F2 | `NV906F_CTRL_CMD_RESET_CHANNEL` 0x906f0102、param `engineID`・`subdeviceInstance`・`resetReason`（0 DEFAULT）・`bIsRcPending`（u8）。意味: channel を reset し、その engine も reset する。STOP が失敗した時は GSP が RC の中で reset を済ませてから event を送る | `ctrl/ctrl906f.h` 106〜148 行、`kernel_channel.c` 3146〜3192 行、`gpu/rc/kernel_rc_notification.c` 355〜437 行 |
| F3 | RC の event `rpc_rc_triggered_v17_02`（nvrtx の `rpc.c` 93〜99 行の offset はこの構造）。`exceptType` は Xid（13 GR exception、31 MMU fault、32 PBDMA、44・45・69、79 bus から落ちた） | `src/nvidia/generated/g_rpc-structures.h` 1439〜1454 行、`src/common/sdk/nvidia/inc/nverror.h` |
| F4 | nouveau の GSP の道: RC の event の chid で channel を引き、errored にして以後の EXEC は -ENODEV（channel は作り直さない）。scheduler の job の timeout は 10 s で channel を kill するだけ。Mesa NVK は -ENODEV を DEVICE_LOST に。VkQueue ごとに channel（app ごとの channel が普通の形） | nouveau `nvkm/subdev/gsp/rm/r535/fifo.c` 45〜70・340〜380 行、`nouveau_chan.c` 44〜62 行、`nouveau_sched.c`、Mesa `nvk_queue.c` 286〜322 行、`nvkmd_nouveau_ctx.c` |
| F5 | hardware の engine の reset は engine に載っている context の group だけを失わせ、save 済みの他の channel の context は残る（nouveau の GSP を使わない道の手順）。GSP の中は firmware で未確認 | nouveau `nvkm/engine/fifo/runl.c` 30〜175 行、`tu102.c` 137〜185 行 |
| F6 | GSP の ctxsw の timeout は registry `RmEngineContextSwitchTimeoutUs`（既定 ≈ 4.3 s）、RC は既定で有効。CPU 側の RC watchdog は kernel RM の物で nvrtx には無い | ogkm `nvrm_registry.h` 1365〜1408 行、`gpu/rc/kernel_rc.c`、`kernel_rc_watchdog.c` 115〜160 行 |
| F7 | channel の USERD: client の RM も「INDEX_FIXED=FALSE、PAGE_FIXED=TRUE、index=chid%8、page=chid/8」で送る（nvrtx の `objects.c` 483〜487 行と同じ）。page は 9 bit。nouveau は GSP の道で chid を 2048 個 | `alloc/alloc_channel.h` 173〜205 行、`kernel_channel.c` 2835〜2850 行、nouveau `r535/fifo.c` 457〜472 行 |
| F8 | promote の VA: nouveau は global の buffer も channel ごとに別の VA に map して promote に書く。channel ごとに own の buffer を別の VA に置くのは RM の普通の形 | nouveau `nvkm/subdev/gsp/rm/r535/gr.c` 40〜135 行、ogkm `gpu/gr/kernel_graphics_context.c` 1446〜1660 行 |
| F9 | preemption は既定 WFI: 長い draw の途中で GR は channel を切り替えない。無限 loop の draw の間、同じ runlist の他の channel は GR に載らない（取り消しが要る理由）。GFXP・CILP は追加の buffer が要り nouveau も使わない | `ctrl2080gr.h` 805〜833 行、`kernel_graphics.c` 155〜159 行 |
| F10 | GPU の timer: PTIMER の TIME_0 0x9400（下位）・TIME_1 0x9410（上位）、上位・下位・上位の順に読み、上位が変わったら読み直す。semaphore の release の timestamp と同じ ns の時計 | nouveau `nvkm/subdev/timer/nv04.c`（`nv04_timer_read`）、`regsnv04.h` |
| F11 | Ampere 以降は runlist ごとの chid。この WS は Turing（RTX 2070）だけを受け入れの対象にする（WS242 に連絡済み） | ogkm `kernel_fifo_init.c` 139〜194 行 |
| F12 | FECS の current context: register 0x409b00 の下位 28 bit が GR に載っている context の instance block の address >> 12（nouveau は GR の割り込みでこれから channel を引く） | nouveau `nvkm/engine/gr/gf100.c`（`gf100_gr_intr`、`gf100_gr_ctxsw_inst`） |

## 4. 設計

### 4.1 構成

```
session（/dev/gpu0 の open）
  ├─ lane: worker（frame・marker を順に）と completer（park の marker）    … 4.9
  ├─ exec_lock: その session の executor を 1 度に 1 つ                      … 4.9
  └─ channel（最初の frame で作る）: chid 16〜47、VA の区画 2 MiB             … 4.2
node
  ├─ channels[32]（chid − 16 → channel、queue_lock の下）
  ├─ reaper: 250 ms の見張りと取り消し                                       … 4.5
  └─ drain: GSP の message（RC の event → channel）                          … 4.6
gpu
  ├─ work: space（table・heap）と driver の channel（chid 4、今のまま。退避先）
  └─ rpc_lock                                                                … 4.7
```

- 案 A（session ごとの channel）の効き目: 詰まった run の後ろに他の session の run が ring で並ばない。取り消しの後、他の channel はそのまま進む
  （共有の channel では後ろの run も全部失った: 1 節の実機の事実）。GR の上は WFI なので、取り消しまでの約 2 s は他の channel も GR に載らない（F9）。
  この 2 s の停滞は session ごとの worker でも消えない（GPU の側の制限）。消すには GFXP の preemption（Future Work）。
- 退避: session の channel が作れなかった（RM が拒んだ、32 個が尽きた）session は driver の channel（chid 4）を共有で使う。その session が犯人の時は
  reaper が共有の channel ごと取り消し、共有の全 session を喪失にする（review v5 の M1。今は誰も取り消さず GR が止まったまま）。取り消した後の
  新しい session は共有に入れない（channel を作れなければ EIO）。boot の語 `nvrtx.channels=0` で全 session を退避にできる（実機で A/B を比べる。既定は 1）。

### 4.2 session の channel（p002）

- 構造: `struct nvrtx_work_channel`（`nvrtx-gpu.h`、名前は GSP の queue の `struct nvrtx_channel`（`nvrtx-gsp.h`）と分ける）に、今の
  `struct nvrtx_work_state` の channel の部分（ring・semaphore・fences・methods・lock・put・fence・entry_fence・made の印・ready）と、
  chid・RM の handle 3 つ・VRAM の arena（instance 4 KiB・USERD 4 KiB・own の context buffer）・`physical[]`・`va[]`（own の buffer の VA）・
  VA の区画の先頭・state・rc の印を持たせる。`struct nvrtx_work_state` は space（arena の table・heap・space_made・root_set・timed）と
  driver の channel（`struct nvrtx_work_channel driver`）を持つ。
- 関数（`work.c`）: `nvrtx_work_channel_open(gpu, channel, chid)`・`_close`・`_kill`（4.5）・`_submit`・`_passed`・`_wait_late`・`_run_time`・`_started`（4.3）。
  今の `nvrtx_work_submit(gpu, …)` 等は driver の channel を渡す薄い形にして boot（`work_first`）はそのまま。
- open の手順（今の `work_arena`・`work_buffers`・`work_channel`・`work_objects`・`work_first` の channel の版。Procedure referenced は今の block のまま）:
  1. VRAM の arena（instance・USERD・own の buffer、`buffer->page_shift` に揃える）、system memory の ring・semaphore・fences・methods。
  2. VA の区画: user の heap から 2 MiB（2 MiB 境界）。own の buffer を区画の頭から page_shift に揃えて **privileged**（access map は read-only）で、
     その後に ring・semaphore・fence の page を書く（`work_entries_write` と TLB の invalidate。PTE の形は `nvrtx_mmu_gr_map` が context buffer に使う物と同じ）。
     新しい関数 `nvrtx_mmu_gr_entries_vram_flags`（privileged・read-only を選べる）を `mmu.c` に足す。
  3. instance block（共有の table の root）、channel の allocate（chid、USERD の index と page は chid から、F7）、promote に **VA の配列**を渡す
     （`nvrtx_gr_promote(gpu, handle, physical, va, golden)`。driver の channel は `gr->virtual_address`、session の channel は区画の VA。global の buffer は
     どちらも golden の VA、F8）、3D・compute の object、bind、schedule。
  4. first submission（class の bind）。**fence を待たない**（review v5 M2 a: GR が他の channel に詰まっている間は first fence が来ず、待つと
     その session を一生退避にしてしまう）。後の run はこの submission の後ろに並ぶので、class の bind が先に済む。
  RM の失敗（status）だけを open の失敗にし、途中まで作った物を close と同じ順で戻して退避（4.1）にする。RM が通しても GPU の側で働かない
  channel（U3）は、その session の run が期限で喪失になる形で現れる（その時の逃げ道は `nvrtx.channels=0`）。
  log `channel %u: session channel on runlist %u` と失敗の段。
- 大きさ: VA の区画と arena の大きさは own の buffer の `page_shift` の揃えを含めて計算し、2 MiB に入らなければ ENOSPC で退避（review v5 m5）。
- 不変条件: session の channel は最初に決めた物（自分の物か共有か）から変わらない。作るのは `node->channel_lock` の下で 1 度だけ（worker と
  `runtime_command` の thread が同時に来ても 1 つ）。`session->channel` と `channels[]` は RM の call が終わってから `queue_lock` の下で公開する。
- chid: 16〜47 の 32 個を bitmap で、**直前に使った次から**探す（next-fit。遅れた RC の event が新しい channel に当たらないよう、すぐ再利用しない）。
  RM の handle は channel 0xf1f00010+n、3D 0x97000010+n、compute 0xc5c00010+n（n = chid − 16）。
- 作る時: lane の worker（または `runtime_command` の thread）が frame を executor に渡す前、`node->lock` の外で（RM の call を `node->lock` の下で
  しない）。faulted・stopping の session は作らない。
- close（`node_close`）の順: lane の queue が空（drain 済み）→ executor の close（detach・free は channel の semaphore を読む）→ channel の close
  （review v5 m7）。channel の close は READY なら SCHEDULE off → object・channel の FREE → 区画の PTE を外す → VRAM・buffer・chid を返す。
  FREED（取り消し済み）なら RM の call は無く資源を返す。STOPPED・STUCK（4.10）は返さない（GSP の停止まで device の物）。
- 範囲の明記: vaspace を共有するので、壊れた shader が他の session の memory を壊すことは防がない（今の 1 本の channel と同じ。分割は別の課題）。

### 4.3 犯人の手がかり（p003）

- **GR に今載っている context**（主）: FECS の current context の register（0x409b00、下位 28 bit が instance block の address >> 12、nouveau の
  `gf100_gr_intr` が channel を引くのに使う形、F12）を reaper が読み、channel の instance block と照合する。WFI の GR では、終わらない draw を持つ
  channel が GR に載ったまま離れないので、これが犯人。GSP の下で CPU から読めるかは未確認（U7）: 読めない（0・全 bit 1・どの channel とも合わない）時は代わりへ。
- **始まりの印**（代わり）: 全ての submit を「start stamp（待たない release、timer つき）→ segment →（timed なら end stamp）→ fence」にする
  （ring の entry は 3、timed で 4。今の timed の record、semaphore の page の 64 個をそのまま使う）。channel の「最も古い終わらない run」
  （semaphore の値 + 1。待っている fence ではなく channel の最も古い物、review v5 m4）の start の値が fence なら始まった、その GPU の時刻も分かる。
  「始まった」は「GR に載った」ではない（PBDMA が読んだだけ、WFI の preempt で draw の境に GR を取られうる、review v5 B2）。そこで代わりの規則は
  「始まって終わらない run を持つ channel のうち、**最後に始まった**物が犯人」: GR を後から取った者が最後に始まり、取られた側はそれより前に始まっている。
- GPU の今の時刻は PTIMER（F10、`nvrtx_gpu_timer_ns`）。読めない時は start の時刻の比較だけを使い、経過は CPU の時刻で見る。

### 4.4 期限と犯人の判定（p003）

- **待つ側**（`vk_node_wait`: executor の待ちと lane の completer）: 期限は `max(submit, 最後の取り消しの終わり) + 2 s`。期限で reaper を起こし、
  自分の channel が READY の間は待ち続ける。上限は同じ起点（submit か最後の取り消しの終わり）から 8 s で、reaper が取り消している間は越えても待つ（2026-10-29 実機: STOP が 1 回 2.3〜4 s かかり、S4 の犯人 2 つの後ろの mview の run が submit から 8 s で巻き添えの喪失になったので直した）: 越えたら自分を喪失
  （今と同じ EIO、`run_lost`）。channel が LOST 以後になったら即 EIO。共有の channel の待ちも同じ。
- **reaper の判定**（`nvrtx_recover_culprit`、`recover.c`、hardware に触らない）: 入力は channel ごとの view（終わらない run があるか、その最も古い物の
  submit の時刻、始まったか・始まりの時刻、GR に載っているか（照合が働いた時だけ））、今の時刻、最後の取り消しの終わり、`RUN_US`。
  1. 期限を過ぎた run（`now − max(submit, kill_end) ≥ RUN_US`）を持つ channel が無ければ何もしない。
  2. GR に載っている channel があり、終わらない run を持つ → それが犯人。
  3. 始まって終わらない run を持つ channel があれば、最後に始まった物が犯人。
  4. どれも始まっていない → 期限を過ぎた run の submit が最も古い channel（今と同じ結末に近い: GPU が別の所で止まっている）。
  取り消しは 1 度に 1 つ。取り消しの後は `kill_end` から 2 s の猶予があるので、次の犯人（S4 の 2 つ目）は次の判定で選ばれる。
- 被害者の待ちの最長: 犯人の期限（2 s）＋見張りの周期（≤ 250 ms）＋取り消し（通常 数十 ms〜1 s、最悪 STOP・RESET の 5 s ずつ）＋自分の run。
  8 s の上限を越えた時だけ被害者も喪失。
- 退避の共有の channel が犯人なら、共有の channel ごと取り消す（4.1）。

### 4.5 reaper と取り消し（p003）

- reaper は node に 1 本の kthread（node の寿命の間、終わらない。reboot の stop は印を立てて止める）。250 ms ごと（または起こされた時）に:
  1. 見張り: `channels[]` と共有の channel の view を queue_lock の下で集め、4.4 の判定で犯人を 1 つ選び LOST にする（待つ者がいない run も
     見つかる。close の途中の session（S5）も）。RC の event で LOST になった channel もここで拾う。
  2. LOST の channel を 1 つずつ取り消す（`rpc_lock` は call ごと。`node->lock` は取らない: executor の待ちが取り消しを待つので）。
     **取り消しの実行者は reaper だけ**（isolate はしない、review v5 M3）。`nvrtx.kill=0` の時は LOST と log だけで取り消さない。
  3. 共通の層への `drv_gpu_report_session_error` は**呼ばない**（p003 の実装で決めた: reaper が `channel_lock` を持ったまま core の lock を取ると、
     core が close の中で node の close（`channel_lock`）を呼ぶ時に順が逆になりうる。喪失した session の操作は `run_lost` と channel の state で EIO に
     なり、marker も EIO で終わるので、公開しなくても app は DEVICE_LOST を受ける）。
- 取り消しの手順（`nvrtx_work_channel_kill`、R1・R2 の判定を一本にした物）:
  1. state = KILLING。`STOP_CHANNEL`（bImmediate=1、RPC の timeout は今の 5 s、F1）。
  2. STOP が 0 → 証拠あり。STOP が失敗 → `rpc_lock` を手放して、その chid の RC の event を 500 ms 待つ（drain が受ける）。来れば証拠あり。
  3. どちらも無い → `RESET_CHANNEL`（engine は graphics、resetReason 0、bIsRcPending は rc の印、F2）。0 なら証拠あり。
  4. 証拠あり → SCHEDULE off・3D・compute の FREE（失敗は log だけ）→ channel の FREE。FREE が 0 → FREED、失敗 → STOPPED（GPU は走らせないが
     RM がまだ資源を持つ: 返さない）。証拠なし → STUCK。
  5. `kill_end_us` を記録、被害者の待ちが次の判定で見る。log `channel %u stopped and freed (%u ms)`・`channel %u stuck: %s status %08x`。
- RPC の timeout を短くしない（review B2）: 遅れた reply を後の call が拾うので。GSP が 5 s 答えなければ `rpc_unanswered` で device の fault（段 3）。
- boot の語 `nvrtx.kill=0`: 取り消しをしない（LOST の判定と log だけ。S1 の観測の build）。

### 4.6 RC の event（p003）

- `nvrtx_rpc_wait` の `NVRTX_EVENT_CHANNEL_ERROR` の枝は log に加えて `gpu->channel_error(gpu->channel_error_argument, chid, type)` を呼ぶ
  （rpc.c は node を知らないので pointer。node の start で設定）。呼ぶのは drain か、RPC を待っている他の thread（reaper の kill の途中など）で、
  どちらも `rpc_lock` を持っている → handler は queue_lock と atomic と waitq の wake だけ（RM も `node->lock` も取らない）。
- handler: chid が session の範囲なら `channels[]` を引き、READY・LOST・KILLING の channel に rc の印（type）を立て、READY なら LOST・`run_lost`、
  reaper を起こす。driver の chid（1〜8）は今のまま log だけ。type 79 は `runtime_fault` に繋ぐ（device の fault、報告は reaper から）。
- 遅れた event: next-fit の chid（4.2）と、FREED の後に `channels[]` から外すこと（close の時）で、新しい channel に当たる機会を減らす。
- reboot の stop（`nvrtx_node_stop`）の後の shutdown の RPC で handler が呼ばれても、印を立てて reaper の waitq を起こすだけなので安全（reaper は止まっている）。

### 4.7 rpc_lock と drain（p002）

- `struct nvrtx_gpu` に `struct mutex rpc_lock`（`LOCK_RANK_DEVICE`）と `rpc_wanted`（atomic、取りたい thread の数）。runtime の RM の call
  （channel の open・close・kill、isolate の中の kill）は `nvrtx_rpc_lock(gpu)`／`nvrtx_rpc_unlock(gpu)` で囲む（call の組み立てから reply の読みまで:
  `gpu->call`・`gpu->reply` は 1 組）。boot（node の start の前）の call は今のまま lock 無し（単一の worker）。
- drain: turn を 1 s から 20 ms にし、turn ごとに `rpc_lock` を取って手放す。手放した後 `rpc_wanted` が 0 でなければ 1 tick 寝る（kernel の mutex は
  待つ者に渡さない: review M4）。refresh の sample と perf の行は今の 1 s ごとのまま（時刻で間引く）。
- reboot の stop（`nvrtx_node_stop`）は今のまま drain を止めてから RM を呼ぶ。stop の前に reaper も止める（4.10）。
- lock の順: `exec_lock` → `channel_lock`（session の channel の作成・close、`node->lock` と同時に持たない）→ `node->lock` → `rpc_lock` →
  `work->lock`（space の heap と PTE）・`channel->lock`（ring）→ `window_lock`。`queue_lock`（spin）は最後で、中で sleep・RM をしない。
  reaper の取り消しは `node->lock` を取らない（`rpc_lock` から）。

### 4.8 共通の層の ops（p003）

| op | 実装 |
| --- | --- |
| `stop_begin` | 今のまま（`stopping` だけ。kill を頼まない: 共通の層は正常な close のたびに呼ぶ、review B1） |
| `stop_poll` | 今の pending の判定に加えて、channel が READY で終わらない run を持つ（正常な close の最後の run が走っている）か、LOST・KILLING なら EAGAIN。FREED・STOPPED・NONE と、終わらない run の無い READY は 0。STUCK も EAGAIN（期限で isolate に進む） |
| `isolate` | 今の state だけで答える（取り消しはしない、review v5 M3）。FREED・STOPPED（GPU はもう走らせない証拠がある）、NONE（channel が無い）、終わらない run の無い READY・共有 → quarantine を立てて（`session->quarantined`、以後その session の destroy・close は hardware に触らず資源を返さない）0。LOST・KILLING・STUCK・終わらない run のある READY・共有 → EIO（device の fault）。isolate が呼ばれるのは stop の期限 10 s の後で、reaper はそれまでに取り消している（4.4 の上限 8 s） |
| `fault` | 今のまま |
| `reset` | NULL のまま（作らない: reboot に任せる。p006 は選択肢） |

- quarantine の session の close: `nvrtx_vk_gpu_free`・`nvrtx_vk_memory_detach` は今も「session の run が済まなければ map と memory を残す」
  （`vk-node.c` 152〜157・344〜349 行）ので、喪失した session は自然に残す側に入る。blob の backing も quarantine なら返さない（`kept` と同じ扱い）。
- FREED の session（GPU はもう読まない）の close: run は済まなくても安全に返せる（`vk_node_quiet`: idle か FREED）。ただし isolate で
  quarantine になった session は FREED でも返さない（quarantine が優先、review v5 m8）。

### 4.9 session ごとの lane（p005）

- **lane の pool**: channel の番号（0〜31）ごとに 1 組（worker と completer）の thread と queue（head・tail、parked の head・tail、waitq）を node に持つ。
  thread はその番号を最初に使う時に作り、node の寿命の間終わらせない（kernel に thread の終わりを待つ口が無い: `thread_wait` は zombie でなければ
  EBUSY を返すだけ、review v5 M5）。その番号の channel を持つ session の frame と marker を扱い、session の close の後は空で寝て次の session を待つ。
  共有の session と channel を持つ前の session（最初の frame）は今の node の worker・completer。lane の thread を作れなければ node の worker に退く。
- queue の entry・marker の slot・`queued`・`pending` の意味は今のまま。marker の順は session の中で保ち、他の session の marker を待たない。
- **executor の待ちと `node->lock`**（review v5 M4）: 手放すのは executor の frame の中の待ちのうち、slot の再利用の待ちだけ（p005 の実装: CPU の copy の
  前の execute_drain は `nvrtx_vk_gpu_idle` を使うので、vk 層を変えずに済む slot の再利用だけにした。以下の「CPU の copy の前」は読み替える）
  （vk 層が呼ぶ `nvrtx_vk_gpu_wait`。手放す前に `mutex_owned` で持っていることを確かめる。completer は持たずに呼ぶ）。destructor の中の待ち
  （memory の detach・run の free の `nvrtx_vk_gpu_idle`）と vkQueueWaitIdle は手放さない。session の executor は `exec_lock`（session ごとの mutex）で
  守り、`exec_lock` を取るのは frame（`runtime_execute`・`runtime_command`）、blob の attach（`nvrtx_vk_node_blob_attach`）、close の executor の close。
  順は `exec_lock` → `node->lock`。p005 の最初に vk 層の待ちの呼び出し元を全部数え、手放してよい所を表にする（手放す前後で保たれる不変条件を書く）。
- 効き目: 詰まった session の slot の再利用・CPU の copy の前の待ちと park の marker の待ちが、他の session の frame と marker を止めない。
  GR の上の停滞（4.1、約 2 s）と destructor の中の待ちは残る。
- perf の行（`blocked`・`parked` 等）は node の数のまま。

### 4.10 状態遷移と寿命

session の channel:

```
NONE ─frame（open 成功）→ READY ─期限の LOST・BLAME・RC・見張り→ LOST ─reaper→ KILLING ─証拠＋FREE 0→ FREED ─close→ 資源を返す
  │                          │                                                    ├─証拠＋FREE 失敗→ STOPPED ─close→ 返さない
  └─open 失敗→ 退避（共有）   └─close→ SCHEDULE off・FREE → 資源を返す             └─証拠なし→ STUCK ─close→ 返さない（isolate の失敗 → fault）
```

- state・rc の印・`channels[]` は queue_lock の下。READY → LOST は 1 回だけ。LOST 以後の submit は EIO（`run_lost`、今の道）。
- 資源の寿命: channel の VRAM・buffer・VA の区画・chid は session の物で、FREED か正常な close で返す。STOPPED・STUCK は GSP の停止（reboot）まで device の物。
  executor の run・blob は今のまま（FREED なら返す、それ以外の喪失は残す）。
- node の stop（reboot）: reaper を止め（印と wake、終わりを待つ）、それから drain を止める今の順。session の channel は GSP の unload が消す。

### 4.11 失敗の段

| 段 | 事象 | 対応 | 動き続ける物 |
| --- | --- | --- | --- |
| 0 | run が 2 s 内に終わる | 通常 | 全員 |
| 1 | run が 2 s を越える・RC | その session の喪失（DEVICE_LOST）、channel の取り消し（FREED か STOPPED） | 他の session（GR の上で約 2 s の停滞の後） |
| 2 | 取り消しの証拠が無い（STUCK） | 共通の層の stop の期限 10 s → isolate の同期の取り消し → 失敗で device の fault | 誰も（今と同じ: reboot） |
| 3 | RPC が返らない（`rpc_unanswered`） | 今のまま device の fault | 誰も |
| 4 | RC の type 79 | device の fault | 誰も |
| 退避 | session の channel が作れない | 共有の channel で今の振る舞い | 今と同じ |

## 5. インタフェース

UAPI、`include/drivers/gpu/gpu.h`、HAL、libvulkan、`vk/nvrtx-vk.h` は変えない。nvrtx の内部の案（名前は実装で coding-style に合わせる）:

```c
/* nvrtx-gpu.h */
struct nvrtx_work_channel {
	uint32_t chid;
	uint32_t handle_channel, handle_threed, handle_compute;
	struct nvrtx_bo arena;			/* instance, USERD, own context buffers (a session's) */
	uint64_t physical[NVRTX_GR_BUFFER_MAX];
	uint64_t va[NVRTX_GR_BUFFER_MAX];	/* where the promote names each buffer */
	uint64_t region;			/* the 2 MiB of VA a session's channel holds, 0 for the driver's */
	uint64_t ring_va, semaphore_va, fences_va;
	struct nvrtx_buffer ring, semaphore, fences, methods;
	struct mutex lock;
	uint32_t put, fence, entry_fence[NVRTX_WORK_RING_ENTRIES];
	int arena_made, region_made, channel_made, threed_made, compute_made, scheduled, ready;
	uint32_t state;				/* NONE, READY, LOST, KILLING, FREED, STOPPED, STUCK (under the node's queue_lock) */
	uint32_t rc_type;			/* the RC event's exceptType + 1 once one came, else 0 */
	int lost_reason;
};
struct nvrtx_gpu { ... struct mutex rpc_lock; uint32_t rpc_wanted;
	void (*channel_error)(void *, uint32_t chid, uint32_t type); void *channel_error_argument; };

int  nvrtx_work_channel_open(struct nvrtx_gpu *, struct nvrtx_work_channel *, uint32_t chid);
void nvrtx_work_channel_close(struct nvrtx_gpu *, struct nvrtx_work_channel *);	/* READY: RM frees; FREED: none; returns resources unless retained */
int  nvrtx_work_channel_kill(struct nvrtx_gpu *, struct nvrtx_work_channel *);	/* 4.5; the evidence and FREE decide the state */
int  nvrtx_work_channel_submit(struct nvrtx_gpu *, struct nvrtx_work_channel *, uint64_t va, size_t length, uint32_t *fence);
bool nvrtx_work_channel_passed(struct nvrtx_work_channel *, uint32_t fence);
int  nvrtx_work_channel_started(struct nvrtx_work_channel *, uint32_t fence, uint64_t *gpu_ns);
int  nvrtx_work_channel_wait_late(struct nvrtx_work_channel *, uint32_t fence, uint32_t timeout_us, uint32_t quiet_us);
int  nvrtx_work_channel_run_time(struct nvrtx_work_channel *, uint32_t fence, uint64_t *gpu_ns);
void nvrtx_rpc_lock(struct nvrtx_gpu *); void nvrtx_rpc_unlock(struct nvrtx_gpu *);
uint64_t nvrtx_gpu_timer_ns(struct nvrtx_gpu *);

/* recover.c (no hardware; host-tested) */
struct nvrtx_recover_view { int busy; uint64_t oldest_submit_us; int started; uint64_t start_ns; int resident; };
int nvrtx_recover_culprit(const struct nvrtx_recover_view *views, uint32_t count, uint64_t now_us, uint64_t kill_end_us, uint32_t run_us);	/* index or -1 */
uint32_t nvrtx_recover_kill_state(int stop_ok, int rc_seen, int reset_ok, int free_ok);
int nvrtx_recover_chid_next(uint32_t used, uint32_t last, uint32_t count);	/* next-fit */

/* nvrtx-node.h */
struct nvrtx_session { ... struct nvrtx_work_channel *channel; int shared; int quarantined;
	struct mutex exec_lock; struct nvrtx_lane *lane; };
struct nvrtx_node { ... struct nvrtx_session *channels[32]; uint32_t chid_bitmap, chid_last;
	struct thread *reaper; struct waitq reaper_wake; uint64_t kill_end_us; int killing; void *reaping; };
```

- RM の param の layout（STOP 1 byte、RESET 16 byte）は `records.c`・`objects.c` の形（`nvrtx_put32` で byte offset）、block ごとに出典。
- log の行（開発の管理情報を入れない）: `channel %u: session channel on runlist %u`、`channel %u: no session channel (%d); the session shares the work channel`、
  `channel %u lost: run %u running %u ms`、`channel %u lost: rc type %u`、`channel %u lost: blamed by a run waiting %u ms`、`channel %u stopped and freed (%u ms)`、
  `channel %u stuck: %s status %08x`、`session isolated: channel %u state %u`。

## 6. 試験計画

### 6.1 host 試験（p002〜p005、実機なし）

- 既存の `plan/tools/nvrtx/nvrtx-vk-host-test.sh` を壊さない（`vk/` の口を変えないので stub はそのまま。各 Phase の終わりに流す）。
- 新 `plan/ws250/tests/nvrtx-recover-host-test.sh`（`recover.c` だけを host の cc で）:
  - chid の next-fit（尽きる・回る・直前を避ける）。
  - **scheduler の model**（review v5 M7）: GR に 1 つの context、timeslice で draw の境ごとに切り替え、終わらない draw は境に来ない、という小さな
    simulator の上で、犯人 1 つ・2 つ（S4）、被害者が先に submit・後に submit、被害者が始まってから GR を取られる（B2 の形）、GR の照合あり・なし、
    共有の channel の犯人、の各場合に `nvrtx_recover_culprit` が毎回犯人だけを選び、被害者を選ばないことを確かめる。
  - state の遷移の表（`nvrtx_recover_kill_state(stop, rc, reset, free)` → FREED・STOPPED・STUCK）と `stop_poll`・`isolate` の返す値の表。
  - RESET・STOP の param の byte の layout（組み立ての関数を `recover.c` に置く）。
- build: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws250/k build/ws250/k/vmunix`（amd64、nvrtx=y）で warning 0。

### 6.2 QEMU

GPU が無いので nvrtx の道は通らない。boot の回帰（`plan/tools/boot-test.sh`）だけ（Q1 の割当で）。

### 6.3 実機（RTX 2070 を差した 10.0.10.3、素の起動、`plan/tools/gpu-perf/machines/rtx.conf`、Q1 の割当で）

script `plan/ws250/tests/rtx-hang-check.sh OUT VMUNIX`（p004）: `measure.sh deploy --kernel` → 以下を順に → 要約。合否は script の要約で判定する。

| scenario | 手順 | 合格 |
| --- | --- | --- |
| S0 対照 | `measure.sh mview` を 12 s（scene-5000）、vkhang --loops | fps が p011 の値（49 前後）から −5% 以内、`VKHANG ok`、dmesg に `session channel` の行 |
| S1 probe（`nvrtx.kill=0`） | mview（12 s）を起動し、その中で vkhang（無限）、10 s 待つ。STOP の返る時間は S2 の `channel %u stopped …（… ms）` の log で測る（STOP だけの probe の boot の語は作らなかった）。GSP が RC しなければ GR は詰まったままなので終わりは reboot | 観測だけ: RC の event が来るか・何秒か・type、mview の frame が進むか、0x409b00 の値と照合の結果、STOP の時間（5 s の RPC の timeout との差、review v5 m10） |
| S2 取り消し | vkhang（無限）と同時に mview（12 s） | vkhang は `VKHANG lost`（≤ 3 s）、dmesg に `lost: run` と `stopped and freed`、mview は止まらず完走（fps の落ち込みは 2〜3 s の 1 回）、その後 `vkhang --loops` が ok、desktop の shot が新しい frame |
| S3 繰り返し | S2 を 5 回 | 5 回とも合格、chid が回る（16, 17, …）、VRAM の残り（perf の行）が戻る |
| S4 同時 | vkhang 2 つと mview | 2 つとも lost、mview 完走 |
| S5 close の途中 | vkhang を起動して 0.5 s で kill -9 | 見張りで取り消し、process が 3 s 内に終わる、desktop 継続 |
| S6 段 2 | S1〜S5 で `stuck` が出た時だけ | 10 s 後に isolate → fault の log |

実機の証拠と QEMU の証拠は分けて書く。

## 7. Phase（必須 34 LW、LW は ±50%）

| Phase | 内容 | 受け入れ | 依存 | LW |
| --- | --- | --- | --- | --- |
| p002 | session の channel（4.2、chid、VA の区画、promote の VA の配列、退避、`nvrtx.channels`、first fence を待たない）、`rpc_lock` と drain の turn（4.7）、`recover.c` の chid の部分と host 試験 | build warning 0、host 試験 PASS。実機: S0（first fence、fps） | なし（WS291 p011 は main） | 8 |
| p003 | 犯人の手がかり（4.3: GR の照合と始まりの印）、判定（4.4）、reaper と取り消し（4.5）、RC の event（4.6）、ops（4.8）、`nvrtx.kill`、host 試験（scheduler の model・状態の表） | build、host 試験。実機: S1〜S6 | p002 | 12 |
| p004 | `plan/ws250/tests/rtx-hang-check.sh`（6.3 の S0〜S6、measure.sh の関数を使う） | `sh -n`、Q1 の割当の実機で流す | p002（script は並行に書ける） | 2 |
| p005 | lane の pool と executor の待ちの一部の lock の外出し（4.9） | build、host 試験。実機: S2 で mview の停滞が GR の分（約 2 s）だけ | p003 | 10 |
| p006 | 選択肢: `reset`（GSP の再起動） | ユーザーの判断（既定は作らない） | — | 12 |
| p007 | 規約の全文と出典の監査、ws.md を完了の形に | 指摘 0 | 全部 | 2 |

試験なしの実装（2026-10-29 の指示）: p002〜p005 は build と host 試験までで「実装済み・試験未実施」と記録し、cleared にしない。実機は後で Q1 が割り当てる。
p003 を p005 より先に入れる間（review v5 M6）: executor の待ちは `node->lock` の中のまま延長に入るので、詰まった時の desktop の停滞は今の 2 s より
長くなりうる（犯人の期限 2 s＋見張り＋取り消し。上限 8 s）。p003 と p005 は同じ branch で続けて実装し、実機の試験は p005 の後にまとめる。

## 8. 決めた事と判断の点

| # | 点 | 決め | 理由 |
| --- | --- | --- | --- |
| D1 | session ごとの channel | する（Q250-4） | ユーザーの回答 |
| D2 | 期限の値 | 2 s（`VK_NODE_RUN_US` のまま。kernel の config に出すのは Future Work） | 今の値と同じ。desktop の run は ms 台、scene-5000 で 20 ms |
| D3 | session ごとの worker | 含める（Q250-5） | ユーザーの回答。ただし非同期の submit（WS291 p011）の後は、効き目は「詰まった session の CPU の側の待ちが他を止めない」に縮む（GR の上の約 2 s は残る、4.1）。Q1 に連絡した |
| D4 | `reset` | 作らない | reboot と同じ結果に近い（display の作り直しが絡む） |
| D5 | RC の event で喪失 | する（Q250-1 の back-up） | MMU fault・GR exception は期限を待つ理由が無い |
| D6 | 犯人の判定に GR の照合と始まりの印 | する（P5 の推し、第 5・6 版で足した） | 被害者の run が犯人より先に期限を迎えうる（session ごとの channel で、犯人の channel が timeslice の中で次の run に進んだ時）。期限だけでは compositor を誤って喪失にしうる |
| D7 | 退避（共有の channel） | する（P5 の推し） | session の channel の RM の受け入れ（USERD・promote の VA）は実機でしか確かめられない。拒まれても desktop は今のまま動く |

## 9. 危険と未確認

1. **U1** STOP_CHANNEL（bImmediate）が無限 loop の channel に何を返すか・何秒か（F1 では timeout で RC）。S1・S2 で見る。
2. **U2** RESET_CHANNEL が同じ runlist の他の channel を壊さないか（F5）。S2 の mview の完走と shot。
3. **U3** chid 16〜47 の USERD（page fixed）と、golden と違う VA の own の buffer の promote を RM が通すか（F7・F8）。S0 の first fence。通らなければ退避で今の振る舞い。
4. **U4** 待たない release（start stamp）が、GR を占められて channel が載らない間は本当に出ないか（犯人の判定の前提）。S2 で被害者が喪失にならないことで確かめる。
   出てしまう（PBDMA が先に進む）なら、始まりの印は「GR が載った」ではなく「PBDMA が読んだ」になり、被害者の判定が LOST に寄る。その時は判定の入力を
   GR の current context の register（nouveau の gf100 の 0x409b00）に替える案（p003 の再試行で）。
5. **U5** PTIMER を CPU から読めるか（GSP の下）。読めなければ CPU の時刻の代わり（4.3）。
5b. **U7** FECS の current context（0x409b00）を GSP の下で CPU から読めるか。読めなければ始まりの印の「最後に始まった」規則（4.3）。S1 で値を記録する。
6. **U6** GSP の ctxsw の timeout の RC（約 4.3 s、F6）が 2 s の取り消しより先に来ないか。来ても RC の道（4.6）で同じ結末。
7. drain の turn を 20 ms にする費用: `nvrtx_rpc_wait` の step は 10 µs、turn の切り替えの mutex は軽い。refresh と perf は 1 s のまま。
8. display の runtime の RM の call: `display/scanout.c` は channel を allocate・free する（1195〜1352 行）。node の start の後に呼ばれるなら drain と
   同時に GSP の queue を読みうる（今からある競争）。この WS は display を触らない（P2 の所有）。`rpc_lock` の口を作るので、display の側が使うかは
   Q1 が P2 と決める。
9. lane の thread の数: 描く session ごとに 2 本。session は数個〜数十個の想定。
10. vaspace の共有（4.2 の範囲の明記）。
11. 取り消しの後の VRAM の leak（STOPPED・STUCK）: 段 1 の STOPPED を繰り返すと VRAM が減る。S3 で perf の行を見る。

## 10. license と転記

- 参照した source とその SPDX は 3 節の表（nouveau は MIT の file だけ、Linux の GPL の file は読んでいない、ogkm 570.144 は MIT、Mesa は MIT）。
- 取った物は値（control の番号、param の欄と大きさ、RC の struct の欄、Xid、PTIMER の register）と手順（STOP → RC の待ち → RESET → FREE の順、
  nouveau の channel の消す順、RC の扱い）だけ。zedBSD の code は Zlib。
- 公開の source の写し（他の worktree の `plan/ws037/temp/`、`plan/ws250/temp/ogkm-src/`）は commit しない。

## 11. 参照

[ws.md](ws.md)、[phase001](phase001/phase.md)、[design-review.md](design-review.md)、[ws291-p011](../ws291/phase011/phase.md)、
[WS037 の設計](../ws037/nvrtx-design.md) 8.3、`include/drivers/gpu/gpu.h` 61〜88 行、`src/drivers/gpu/gpu.c`、`src/drivers/gpu/i915/reset.c` 686〜716 行。

## 12. 前の版と review の指摘の行き先

| 指摘・改訂 | この版 |
| --- | --- |
| B1（stop_begin が正常な close で kill） | 4.8 の `stop_begin` は kill を頼まない。見張りが走り続ける run を見つける（4.5） |
| B2（短い RPC の timeout） | 4.5: 今の 5 s と `rpc_unanswered` のまま |
| M1（案 A の利点が無い） | 非同期の submit の後は ring の後ろに並ばない利点がある（4.1）。GR の上の停滞は残ると明記 |
| M2（isolate がほぼ失敗） | 4.8: 証拠の基準と、isolate の中の同期の取り消し |
| M3（kill を worker の次の turn に頼る） | reaper（4.5）が見張りと取り消しを行う |
| M4（rpc_lock の公平） | 4.7: 20 ms の turn と `rpc_wanted` |
| M5（chid の再利用・ABA） | next-fit の chid、`channels[]` から外す時期、`reaping` の待ち（4.2・4.5・4.6） |
| M6（U2・S1 の probe） | S1 は mview（別 session、別 channel）を同時に流すので他の channel に仕事がある |
| M7（vkhang の loop） | 既存の `plan/ws039/tests/vkhang`（0 から 3x+1: 0 で止まらない）を使う |
| M8（kill の判定の食い違い） | 4.5 の一本の手順 |
| M9（VA の区画） | 4.2 |
| M10（名前） | `nvrtx_work_channel` |
| M11（実機の要る Phase） | 7 節: 実装と host 試験までと、実機の scenario を分けた |
| M12（重ねた版の矛盾） | この版で一本に書き直した |
| M13（全 open に channel） | 最初の frame で遅延して作る（4.2） |
| R1〜R12・T1〜T13・W1〜W5 | F1〜F11 と 4 節に入れた。T1（案 B）は Q250-4 で取り消し済み |

第 5 版の review（[design-review-v5.md](design-review-v5.md)）の行き先:

| 指摘 | 第 6 版 |
| --- | --- |
| B1（被害者が先に期限で LOST） | 判定を reaper だけにし、待つ側は自分の channel が READY の間待つ（上限 8 s）（4.4） |
| B2（始まった ≠ GR に載っている） | 主を GR の current context の照合に、代わりを「最後に始まった channel」に（4.3） |
| M1（共有の犯人を誰も取り消さない） | 共有の channel ごと取り消す（4.1・4.4・4.5） |
| M2（遅延の作成） | first fence を待たない、`channel_lock` で 1 度だけ、不変条件、公開の順（4.2） |
| M3（二重の取り消し） | 実行者は reaper だけ、isolate は state だけで答える（4.5・4.8） |
| M4（lock を手放す） | 手放すのは slot の再利用と CPU の copy の前だけ、`exec_lock` を取る所の一覧、p005 の最初に呼び出し元の表（4.9） |
| M5（thread の終わり） | lane は番号ごとの pool で終わらせない、reaper も終わらせない（4.5・4.9） |
| M6（p003 の後の停滞） | 7 節の注（同じ branch で続けて実装、試験は p005 の後） |
| M7（試験の計画） | scheduler の model の host 試験（6.1） |
| m1〜m12 | 行の番号は実装の Phase で今の tree に合わせる（m1）。m2: space の lock（`work->lock`、heap と PTE）と channel の lock を分けた（p002 の実装）、map は driver の channel の ready を見る。m3: 呼び出し元は p002 で全部替えた。m4・m5・m7・m8・m9・m10 は本文。m6: `node->lock` → `rpc_lock` の順（display の control）と、open の公開は RM の後。m11: ws.md を同じ commit で直した。m12: RC の handler は reaper の waitq を起こすだけで、reboot の stop の後に呼ばれても安全（4.6） |
