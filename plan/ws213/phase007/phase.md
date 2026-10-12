<!-- awesome-plan project=zedbsd record=ws213-p007 -->

# ws213-p007: kernel の入力の遅れ（K-D の診断、K-1・K-2 の設計）

Parent: [WS213](../ws.md)
Status: in-progress（2026-10-12 Q1 が P5 に依頼。K-D は実装済み（agent/p9）、K-1・K-2 は設計だけ、Q1 が design-reviewer に掛けるまで実装しない）
Phase disposition: normal

## 由来

Pi 4（1 core）で compositor の入力の thread I の box_lag が 17〜50ms（[p004](../phase004/phase.md) の Pi の数字、`KWL CURSOR_LAG` 38 行）。P4 の解析（p004 の「kernel の source から」「kernel の変更の設計」）では原因は kernel の側で、候補は 2 つ:

1. **1 core の同じ優先度の順番**（`src/kern/sched.c`）: `sched_yield`・`sched_preempt`・`switch_without_enqueue`・`sched_exit_current` は切り替えのたびに `cpu->preempt = 0` にするので、`woken` に残った thread は走っている thread が眠るか quantum（10 tick = 10ms）を使い切るまで待つ。起床で横取りされた thread は `run` の頭に戻る。`poll_notify`（`src/kern/poll.c`）は system 全体の 1 本の待ち行列を全部起こす。
2. **user の写しの global な lock**: copyin・copyout は毎回 `vm_metadata_enter()`（system 全体の 1 つの mutex）と process の `vm->lock` を取り、`mutex_unlock`（`src/kern/lock.c`）は待ち手を 1 つ起こすだけで持ち主を渡さない。

log からは 1 と 2 を分けられない。この Phase は分けるための診断（K-D）を入れ、直し（K-1・K-2）を設計する。

## 範囲

- K-D（診断、動作は変えない）: 起床・横取りの後に走るまでの最長と pid・tid、metadata lock の持った時間の最長と呼び出し元、待った時間の最長。2 秒ごとに 1 行。`SCHED_WAKE_LATENCY` の build の使い方。
- K-1（sched）と K-2（lock・VM）の設計: 不変条件、SMP（WS208）、公平さ、VM の不変条件、試験。**実装はしない**（Q1 が design-reviewer に掛けてから）。
- 触る所: `src/kern/sched.c`・`src/kern/vm.c`・`include/kern/sched.h`・`include/kern/vm-lock.h` だけ。HAL の API・toolchain・`src/drivers/` は変えない。

## 受け入れの条件

1. K-D: rpi4 と amd64 の vmunix が warning 0。`kern.perf=1` の無い boot では動作が今と同じ（測る所は全部 `sched_perf_active` の 0 で素通り）。host 試験 `plan/ws213/tests/kd-host-test.sh` PASS（plain と ASan+UBSan）。
2. K-D の Pi の 1 boot（焼き直しの後、BUG-294 で今は root の UFS が書けない）で、`KWL CURSOR_LAG` の 17ms 超えの行ごとに、下の「読み方」で原因を 1・2・両方・どちらでもない に分けた表を作る（Q1 かユーザーの手で）。
3. K-1・K-2 の設計が design-reviewer を通る（Q1）。実装は別の Phase（K-D の結果で要る方だけ）。

## K-D の実装（2026-10-12 P5、agent/p9 45ae288d7）

### 有効にする

boot の引数 `kern.perf=1`（cmdline.txt の行、または config.mk の `ZEDBSD_BOOT_EXTRA_LINES += kern.perf=1`）。既定は無し。

既定で出さない理由（負荷で決めた）: 有効にすると、切り替えのたびに counter の読み 1〜2 回、metadata lock の外側の入りごとに 2〜3 回と trylock が増える（arm64 は CNTVCT の読み、軽い）。重いのは log の方で、1 行（最大 499 byte）を 2 秒ごとに足すと Pi の 256 KiB の kernel log の ring が約 20 分で一周し、起動の messages が消える。v3d の perf（`v3d.perf=1`）と同じく boot の引数で入れる。無効の時の費用は、切り替え・起床・metadata の入りごとに global の int を 1 回読むだけ。

有効になると起動の時に `sched: latency diagnostic on (kern.perf=1), one report every 2000 ms` が出る。

### 何を測るか

- **woken**: 起床（`sched_wakeup`、時刻つきの sleep の満了。どちらも `wake_enqueue_locked`）で runnable になった thread が、実際に走るまで。
- **preempted**: 起きた thread に CPU を取られた thread（`sched_preempt` が `run` の頭に戻した thread）が、再び走るまで。quantum の終わりの `sched_yield` は測らない。
- **ahead**: その間に同じ CPU が切り替えた（他の thread が走った）回数。別の CPU に取られた（steal）時は -1。
- **dropped**: 切り替えで `cpu->preempt` が消えた時に、新しく走る thread 以上の優先度の起きた thread が `woken` にまだ残っていた回数（候補 1 の (a) の状況そのもの）。
- **meta**: `vm_metadata_enter()` の外側の入り（takes）、そのうち lock が持たれていて待った数（waits）、待った最長、持った最長（外側の入りから出まで。持ち主が横取りされていた時間を含む）と、その間に持ち主が CPU を失った回数（switched）。待った最長・持った最長には `vm_metadata_enter()` を呼んだ所の address（at）が付く（`vm_metadata_enter` は noinline にした）。

### 行の形と読み方

2 秒ごとに 1 行（その 2 秒の窓の値。窓の後に 0 から数え直す）。報告は user mode へ戻る途中の thread（何も持っていない所、`sched_preempt_point`）が書く。

```
kern: perf at_ms=T ms=W cpus=N switches=S | woken=N max_us=X end_ms=E who=PID/TID/NAME ahead=K | preempted=N max_us=X end_ms=E who=PID/TID/NAME ahead=K dropped=D | meta takes=N waits=N max_us=X end_ms=E who=PID/TID/NAME at=ADDR | hold max_us=X end_ms=E who=PID/TID/NAME at=ADDR switched=K
```

- `at_ms`・`end_ms` は起動からの ms（`clock_ticks`、tick は Pi で 1ms）。compositor の `KWL` の `at_ms` と `*_us`（`CLOCK_MONOTONIC`）は同じ時計なので、そのまま並べられる。`end_ms` は最長の待ちが終わった時。
- `who` は pid/tid/process の command の先頭 15 字。kernel thread は pid 0（名前は空）。最長の無い窓は `max_us=0 who=0/0/`。
- lock が持たれていて metadata の窓を読めなかった時は `meta held (left to the next report)` で、その分は次の行に入る。
- address は `build/llvm/bin/llvm-symbolizer --obj=<build>/arm64/kernel.elf 0xADDR`（または `llvm-nm -n kernel.elf` で直前の symbol）で関数名にする。

**1 と 2 の分け方**（`KWL CURSOR_LAG` の 1 行ごとに、その event の時刻を含む窓の行を見る）:

| 見えた物 | 判定 |
| --- | --- |
| `woken` の `max_us` ≈ lag、`who` が compositor の I の tid、`end_ms` ≈ I の `poll_returned_us`、`ahead` ≥ 1、`dropped` > 0 | **1（sched の順番）**。`ahead` が 1 なのに `max_us` が 10ms を超えるなら、1 つの thread が preempt を止めたまま長く走った（kernel の preempt-disable の区間）か、I が別の CPU の列にいた |
| `meta` の待ちの `max_us` ≈ lag、`who` が I の tid（`poll_returned_us` と `read_us` の差、または poll の入りと戻りの差と重なる） | **2（lock）**。`hold` の `at` が長い持ちの場所（`vmspace_pin_user_pages` の範囲の pin、`vmspace_wire_range` の page ごとの loop など） |
| 2 の形で、`hold` の `switched` > 0 | **両方**: 持ち主が lock を持ったまま横取りされ、起きた thread の後ろに並んだ（K-1 と K-2 の両方が効く） |
| `waits` が多いのに `hold` の `max_us` が短い | 持ち主が出てすぐ入り直す（受け渡しの無い mutex の横取り、K-2 の (d)） |
| どの値も lag より小さい | kernel の外（userland の I の中、または C・M の lock）。p004 に戻す |

- `woken` の数 / 2 秒が `switches` に比べて大きいなら `poll_notify` の全起こし（herd）が多い。
- `preempted` の `who` が M・R（compositor の CPU を使う thread）なら、K-1 で彼らが待たされる量の基準になる（公平さの比較の元）。
- I の tid は compositor の log に無いので、`ps -T` 相当で見るか、I の thread の始めに tid を log に出す（p004 の userland、P4 への依頼の候補）。

### `SCHED_WAKE_LATENCY` の build（起床の遅れの histogram）

既存の診断（`sched.c` の先頭、同じ CPU・別の CPU の起床から走るまでの 5 段の histogram と平均・最長）。struct の配置が変わるので、自分の BUILD の dir に別に作る:

```
make BUILD=build/<自分の dir> -o <絶対 path>/build/arm64/sysroot/.zedbsd-sysroot-complete \
  ZEDBSD_CONFIG=config/ci/config-rpi4.mk ZEDBSD_TEST_CPPFLAGS=-DSCHED_WAKE_LATENCY=1 build/<自分の dir>/vmunix
```

今まで報告は i915 の perf（`src/drivers/gpu/i915/perf.c`）だけが呼んでいたので Pi では出なかった。K-D の報告が同じ窓で呼ぶようにした（`kern.perf=1` も要る）。行は `sched: wake latency same-cpu: n= avg= us max= us | <0.1ms <1ms <5ms <12ms more` と `cross-cpu` の 2 行。amd64 の i915 では i915 の perf も同じ数を読んで消すので、窓はどちらか先に書いた方まで（片方だけ見る）。2026-10-12 に rpi4 の `-DSCHED_WAKE_LATENCY=1` の build が warning 0 で通ることを確かめた（`build/ws213-p007-wl/vmunix`）。

### 確かめた事

- build（warning 0）: rpi4 `make -o /home/awe/zedBSD-worktrees/p9/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix`（`arm64 vmunix check: PASS`）、amd64 `make -o .../build/amd64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/vmunix`（`amd64 vmunix check: PASS`）、rpi4 + `-DSCHED_WAKE_LATENCY=1`（上）。`kern: perf at_ms` の文字列が rpi4 の vmunix に入り、`vm_metadata_enter` が関数のまま残る（LTO で inline されない）ことを `llvm-nm` で確かめた。
- host: `sh plan/ws213/tests/kd-host-test.sh` → `kd-host-test: PASS`（plain と ASan+UBSan）。`sched.c`・`vm.c` から関数を切り出し、本物の `struct thread`・`struct process` と偽の counter・tick・mutex で: woken の待ちと ahead、preempted を別に、同じ優先度の残りは dropped・低い優先度は数えない、自分への切り替えは数えない、steal の ahead -1、CPU をまたぐ最長の合わせ、消した印は測らない、3GHz の 1 時間でも溢れない換算、metadata の takes・waits・待ちの最長と呼び出し元・持ちの最長と switched・持たれている時は窓を次へ残す。dropped の数えを消す変異で FAIL になることを確かめた。
- sched・vm の既存の host 試験は tree に無かった（`git grep` で `src/kern/sched.c`・`vm.c`・`lock.c` を build する試験は無い。`plan/ws073/tests/region-index-host.sh` は vmspace の region の索引で無関係、かつ host の rm を含むので走らせていない）。
- 未実施: QEMU・実機（Pi は焼き直しの後、`kern.perf=1` を足した 1 boot）。T1 には頼んでいない（`kern.perf=1` の無い boot は動作が同じで、有効にした時の確認は Pi で行う。amd64 の起動の回帰を見るなら T1 の boot-test 1 回）。

## K-1 の設計（sched、候補 1 への直し）

### 今の順番（読んだ物、`src/kern/sched.c`）

- 起きた thread は、走っている thread 以上の優先度なら `woken[p]` の尾に入り、`cpu->preempt = 1`（`wake_enqueue_locked`）。`pick_next_locked` は各優先度で `woken` を `run` より先に取る。
- 印は 3 か所で使われる: user へ戻る途中（`sched_preempt_point`）、tick（`sched_clock_cpu` の `wanted`、kernel の中でも `preempt_count` が 0 なら）、他の CPU からの通知（`sched_cpu_notify`）。どれも `sched_preempt` を呼ぶ。
- `sched_preempt` は `cpu->preempt = 0` にしてから `woken` の頭を取り、走っていた thread を `run` の頭に（quantum の残りを持って）戻す。他の 3 つの切り替え（`sched_yield`・`switch_without_enqueue`・`sched_exit_current`）も最後に印を消す。
- **問題**: 印が消えた後に `woken` に残った thread は、新しく走った thread が眠るか、user へ戻る途中で新しい起床が印を立てるか、quantum（10 tick）を使い切るまで走れない。herd（`poll_notify` の全起こし）で `woken` に N 個並ぶと、最後の I は最大 N × 10ms 待つ（17〜50ms は 2〜5 個分に合う）。

### 変更

1. **起床の持ち分（wake budget）**: thread に `sched.wake_budget`（tick の数）を足す。`wake_enqueue_locked` で `SCHED_WAKE_BUDGET_TICKS`（`KERN_MS_TO_TICKS(1)`、最低 1、quantum を超えない）に設定する。走っている thread の tick ごとに（`sched_clock_cpu` の quantum の減算の隣で）1 減らす。
2. **持ち分を使い切った時の順番渡し**: tick で、走っている thread の `wake_budget` が 0 になった（または既に 0）で、その CPU の `woken` に走っている thread 以上の優先度の thread がいれば、`cpu->preempt = 1` にする（同じ tick の `wanted` が `sched_preempt` を呼ぶ。`preempt_count` が 0 でない時は今どおり `kern_preempt_enable` まで延びる）。
3. **横取りされた thread の戻り先**: `sched_preempt` で、走っていた thread の `wake_budget` が残っていれば（起きてから 1 tick を使っていない）**`woken` の尾**に戻す。残っていなければ今どおり `run` の頭（quantum の残りを持って）。尾に戻すのは「頭」に戻すと X と Y の 2 つが交互に印を立て合って Z が永久に走れない（ping-pong）ため。尾なら `woken` の中は FIFO の round robin になる。
4. 切り替えでの印の消し方は今のまま（P4 案の (a)「印を残す」は採らない）: 印を残すと、起きて走り始めた I 自身が poll から user へ戻る途中の `sched_preempt_point` で次の起きた thread に横取りされ、I が user に戻るのが遅れる。代わりに 2 の tick の順番渡しで「前に並ぶ起きた thread 1 つにつき最大 1 tick」にする。

### 不変条件

- (I1) `woken[p]` の thread は全部 RUNNABLE で、`wake_budget > 0` か、起床から 1 度も走っていない。`run[p]` の thread の `wake_budget` は 0 か、起床の時に走っている thread より優先度が低かった thread（今どおり `run` の尾）。
- (I2) `woken` を持つ CPU では、走っている thread の `wake_budget` が 0 なら、次の tick までに `cpu->preempt` が立つ（2 で立てる）。したがって `woken` の頭は、前の thread が持ち分を使った次の tick で走る。
- (I3) quantum の扱いは変えない: `wake_budget` は quantum を補充しない。起床と眠りを繰り返す thread も quantum を使い切れば `sched_yield` で `run` の尾へ行く（今の `wake_enqueue_locked` の注記の公平さの錨）。
- (I4) `wake_budget` と K-D の stamp は、その thread の CPU の lock の下で読み書きする。移動（`sched_set_cpu`・steal）では thread と一緒に動く（`need_migrate` の手順の中）。
- (I5) idle の thread は `wake_budget` を持たず、順番渡しの対象にならない。

### 待ちの上限（1 core）

`woken` に I の前に N 個並ぶと、I が走るまでは最大 N tick ＋ 走っている thread が次の preempt の点に着くまで（kernel の `preempt_count` の区間、割り込みを止めた区間）。今の最大 N × 10ms が N × 1ms になる。herd の N が大きい場合は残る（下の K-3）。

### 公平さ

- `run` の thread（CPU を使い続ける M・R・v3d の decode の worker・client の描画）は、`woken` が空になるまで走れない点は今と同じ。`woken` の各 thread は起床ごとに最大 1 tick（＋ quantum の残り）なので、`woken` に居られる時間は今より短い（今は起きた thread が眠るまで最大 10 tick 前に居られた）。CPU を使い続ける thread の待ちは今と同じか短い。K-D の `preempted` の `max_us` で前後を比べる。
- 起床を 1 tick より短い間隔で繰り返す thread は、`run` の thread を飢えさせ得る（今も同じ。quantum の錨 (I3) で 10 tick ごとに 1 回は `run` の尾へ行く）。悪化はしない。
- 同じ優先度の `woken` の中は FIFO＋尾への戻しで、どの thread も前に並ぶ数 × 1 tick で頭に来る（飢えない）。

### SMP（WS208）

- 列・lock・tick は CPU ごと（`struct sched_cpu`、`sched_clock_cpu` はその CPU の tick だけ）。`wake_budget` を減らすのは thread が走っている CPU の tick で、その CPU の lock の下。WS208 で arm64 の CPU ごとの generic timer が入れば Pi でも同じ。
- 他の CPU からの起床: `wake_enqueue_locked` は相手の CPU の lock の下で `woken` に入れ印を立て、`notify_cpu` が相手の `sched_cpu_notify` → `sched_preempt` を起こす（今どおり）。3 の尾への戻しは自分の CPU の列だけ触る。
- steal（`take_stealable_locked`）は `woken` の頭を他の CPU の `run` の尾へ移す。移った thread は idle の CPU ですぐ走るので順番は悪くならない。`wake_budget` は移った先で減る。
- 4 core では 1 core より起きた thread の待ちは小さいが、herd・I の待ちの形は同じ（compositor の thread が同じ CPU に並ぶと同じ事が起きる）。K-1 は SMP の前後どちらでも効き、WS208 の作業と衝突する所は無い（HAL を触らない、MI の `sched.c` だけ）。WS208 の merge の後に、K-1 の host 試験に 2 CPU の場面を足す。

### 試験（実装の Phase で）

1. host（新規）: `sched.c` を偽の HAL（`hal_task_context_switch` は記録だけ、`curthread` は試験が切り替える）と偽の tick で build する順番の試験。場面: (a) CPU を使い続ける thread の上で N 個が起きる → 各々が 1 tick 以内に順に走り、最後が N tick 以内、(b) X・Y・Z の ping-pong が起きない（尾への戻し）、(c) 起床と眠りを繰り返す thread も quantum で `run` の尾へ行く（I3）、(d) `woken` が空になると `run` の頭（横取りされた M）が走る、(e) `preempt_count` の間は延び、`kern_preempt_enable` で行われる、(f) idle は対象外、(g) 2 CPU の steal（WS208 の後）。K-D の `kd-host-test.sh` も通す。
2. T1: amd64 の boot-test（1 CPU と `-smp 4`）、`plan/tools/latency/run-wakebench-qemu.sh` の起床の遅れを K-1 の前後で（QEMU の数字は傾向だけ）。
3. Pi（`kern.perf=1`）: p004 の場面 (1)〜(4) で、`woken` の `max_us`・`dropped`・`preempted` の `max_us` と `KWL CURSOR_LAG` の数を K-1 の前後で比べる。受け入れは p004 の 5（box_lag の p99・最長 ≤ 16.7ms）。

## K-2 の設計（lock・VM、候補 2 への直し）

### 今（読んだ物）

- `copyin`・`copyout`（`src/kern/uaccess.c`）は、1 回の写しで metadata lock を **3 回**取る: `user_range_check` → `vmspace_check`（metadata ＋ `vm->lock`）、`copy_backing` → `vmspace_pin_user_pages` の中の `vmspace_pin_mapping_ready`（page ごとに metadata ＋ `vm->lock`）と、範囲全部の pin（metadata ＋ `vm->lock` を最後の page の pin まで持つ）。`vmspace_pin_user_pages` は自分でも `vmspace_check_locked` で範囲を確かめるので、最初の `user_range_check` は重複している。
- `vmspace_check_locked` は `vm->regions` の list を頭から辿るだけ（region の数に比例）。region の中身（private page・object）には触らない。
- 大きな範囲の pin（import の 2MB の buffer、512 page）は metadata を全 page の pin の間持つ（「wire/unlock/rewalk の隙間で unmap・remap・fork が page の同一性を変えるのを閉じる」ため、`vmspace.c` の注記）。`vmspace_wire_range` は page ごとに入り直す。
- `mutex_unlock` は `locked` を FREE にしてから待ち手を 1 つ起こす。起きた待ち手が走る前に、出た thread（または他の thread）が CAS で取り直せる（横取り）。1 core では、出た thread が同じ kernel の入りの中で入り直すと（pin の loop、fault の中の 2〜3 回の入り）、待ち手は次の preempt の点まで走れず、走った時には lock が持たれていて、また眠る。`mutex_spin_take` の 256 回の見直しは 1 core では無駄（持ち主は走れない）。

### 変更（小さい順。K-D の結果で要る物だけ実装する）

- **(a) 重複の範囲確かめを外す**（`uaccess.c` の `copyin`・`copyout` の `user_range_check`。`copyinstr` は page ごとに `copyin` を通る）: pin が同じ確かめを locks の下で行う。metadata の入りが 3 → 2 回。返す errno: 今は確かめの失敗で EFAULT、pin の失敗は ENOMEM 以外 EFAULT（`copy_backing`）。ただし `copy_backing` は pin の配列を `kern_calloc` で確保してから pin するので、確かめを外すと、写せない巨大な範囲が EFAULT ではなく ENOMEM になり得る（大きな確保も起きる）。そこで外すのは小さな写し（配列が固定の小さな大きさ、例えば 4 page 以下で、配列を stack に置ける時）だけにし、大きな写しは今の確かめを残す。範囲の検証（`vmspace_user_range_valid`）は `copy_backing` の先頭に残る。
- **(b) 準備済みの page の確かめと pin を 1 回の持ちで**: `vmspace_pin_user_pages` で、まず locks を 1 回取って全 page が準備済み（`vmspace_pin_mapping_ready` と同じ条件）ならそのまま pin まで進む。準備済みでない page があれば locks を外して fault し、今の手順（retry_faults）に戻る。小さな写し（I の pollfd・event）では metadata の入りが 1 回になる。
- **(c) `vmspace_check` を metadata 無しで**: `vm->lock` だけで `vmspace_check_locked` を呼ぶ。lock の順（metadata → `vm->lock` → reclaim・object → spin の state）には反しない（`vm->lock` だけを取り、持つ間に metadata を取らない）。**前提（実装の前に確かめる VM の不変条件）**: `vm->regions` の list と各 region の `start`・`size`・`prot`・`next` を書く所が全部 `vm->lock` を持つ（`vmspace.c` の region の作成・split・unlink・mprotect・stack の guard、約 16 か所。`find_region_locked` の読み手と同じ）。1 つでも metadata だけで書く所があれば (c) は採らない。
- **(d) 横取りの後の受け渡し（handoff）**: `struct mutex` の `locked` に `MUTEX_HANDOFF` の bit を足す。起こされた待ち手が `mutex_sleep_take` で取り損ねたら（横取りされた）、guard の下で HANDOFF を立てて眠り直す。HANDOFF が立った mutex の `mutex_unlock` は FREE にせず、guard の下で待ち行列の頭の thread（`wait_token->thread`）を `owner` にして（`locked` は HELD、他の待ち手がいれば CONTENDED）起こす。起きた待ち手は `owner` が自分なら取ったものとして戻る。Linux の mutex の HANDOFF と同じ形で、普段（横取りの無い時）の速さは今のまま、飢えは「1 回の取り損ね」で止まる。
  - 不変条件: (H1) HANDOFF の間は `locked` が FREE にならない（`mutex_take_free` の CAS は FREE からだけなので、横取りできない）。(H2) 渡された所有は拒めない: 割り込み可能な待ち（`mutex_lock_interruptible`）が EINTR で起きても、`owner` が自分なら取ったとして 0 を返す（signal は pending のまま次の点で配られる）。(H3) 受け渡しは待ち行列の頭へ（waitq は FIFO）。(H4) `mutex_wait` の「condition で眠る間 mutex を譲る」も同じ unlock の手順を通す。(H5) `owner` は渡す側が guard の下で書く（今は持ち主だけが書く、という `mutex_owned` の注記を「持ち主か、guard の下の渡す側」に直す）。
  - 範囲: 全部の mutex に効く（lock.c の共通の変更）。metadata だけに限る案（`metadata_lock` 専用の flag）より単純で、他の mutex の飢えも止まるが、review の範囲が広い。
- **(e) 長い持ちを切る**（K-D の `hold` の `at` が `vmspace_pin_user_pages` で、`max_us` が 16ms に近い時だけ）: 大きな範囲の pin を N page ごとに locks を外す形にするには、上の注記の「page の同一性」を別の方法（vmspace の generation の確かめと、変わったら pin 済みを外してやり直す）で守る必要がある。VM の不変条件の review が要るので、この Phase では方針だけ: 写し（copyin・copyout）は同一性の途中の変化を許してよい（POSIX は並行の munmap と写しの結果を決めない）が、GPU の import の pin は許さない。分けるなら呼び出し元ごとに。
- 採らない物: copyin・copyout の pin 無しの直接の写し（user の VA を kernel から直接読み、fault を回復する）は、例外の fixup の表が HAL に要る（HAL の API の変更、事前承認が要る）ので、この Phase の外。

### VM の不変条件（(a)〜(c) で守る物）

- (V1) lock の順: metadata → `vm->lock` → reclaim・object の lock → private page の `state_lock`（spin）。(c) は `vm->lock` だけを取り、その中で metadata を取らない。
- (V2) pin した page は、pin が外れるまで同一の backing を指し、reclaim されない（`vm_private_page_pin`・`vm_object_page_pin` の参照）。(a)・(b) は pin の手順を短くするだけで、pin の中の確かめ（`vmspace_check_locked`、`find_page`、BUSY の待ち）は同じ lock の下に残す。
- (V3) fault は VM の lock を持たずに行う（「faulting may sleep and may break COW」）。(b) で準備済みでない時は locks を外してから fault する。
- (V4) `vmspace_check` が `vm->lock` だけで正しいのは、regions の書き手が全部 `vm->lock` を持つ時だけ（(c) の前提）。

### SMP（WS208）

- metadata lock は system 全体で 1 つなので、4 core では faults・pins が並行に増え、競合は増える。(a)〜(c) は入りの数を減らし、SMP の伸びにも効く。
- (d) の受け渡しは SMP では待ち手を起こす IPI の分だけ lock が空く（受け渡しの間は誰も持てない）。普段は今の 256 回の見直しで隣の CPU の解放を待てるので、受け渡しは「取り損ねた待ち手がいる時」だけにする（上の形）。
- `mutex_spin_take` を 1 CPU の時に飛ばす（`hal_cpu_count()==1` なら見直さずに眠る）は小さな改善の候補（1 core での無駄な 256 回）。WS208 の `cpus=1` の起動（D3）でも同じ判断になる。

### 試験（実装の Phase で）

1. host（新規）: (d) は `lock.c` と `waitq.c` を pthread の偽の scheduler（`sched_sleep_locked` を thread ごとの条件変数、`sched_wakeup` を signal）で build し、取り直しを繰り返す thread と待ち手で、待ち手が 1 回の取り損ねの後の次の unlock で必ず取る、`owner`・`locked` の不変条件（H1〜H5）、割り込みの待ちでの受け渡し、TSan の報告 0。(a)〜(c) は `vmspace_check` と pin の手順を切り出す試験（region の list と page の表を本物の構造体で、`region-index-host.sh` の形）で、errno が今と同じ、準備済みの時は metadata の入りが 1 回。
2. T1: amd64 の boot-test、fork・vfork・posix_spawn と COW（`plan/tools/process/guest-vfork.sh`）、guest の make（`plan/tools/guest/make-cases.sh`）で VM の回帰が無い。`plan/tools/kbench` の system call・pipe・fault の時間を前後で（悪化しない）。
3. Pi（`kern.perf=1`）: `meta` の `takes`（2 秒あたり）・`waits`・待ちの最長の前後、`KWL CURSOR_LAG` の数。

## K-3（関係する物、この Phase の外）

- `poll_notify` の全起こし（`src/kern/poll.c`、system 全体の 1 本の waitq を `waitq_wake_all`）: 入力・pipe・socket の変化ごとに system の全部の `poll` が起き、各々が pollfd を copyin（metadata ×3）・file を走査して眠り直す。herd が K-1 の N と K-2 の競合の両方を増やす。file ごとの待ち行列への変更は poll の設計の変更で、K-D の `woken` の数が大きければ別の WS・Phase として Q1 に提案する。
- I の thread の優先度を上げる手段（p004 の「始めに確かめる事」）: kernel は 16 段の優先度を持つが user から変える API があるかは未確認。UAPI と権限の判断が要るので、ここでは扱わない。

## 依存と再開の条件

- K-1・K-2 の実装は、Q1 の design-reviewer の review の後、K-D の Pi の結果で要る方から（別の Phase にするかは Q1）。
- Pi の K-D の 1 boot は、BUG-294（root の UFS が書けない）の焼き直しの後。
- WS208（SMP）の merge の後は K-1 の host 試験に 2 CPU の場面を足す。
