<!-- awesome-plan project=zedbsd record=ws213-p007 -->

# ws213-p007: kernel の入力の遅れ（K-D の診断、K-1・K-2 の設計）

Parent: [WS213](../ws.md)
Status: in-progress（2026-10-13 ユーザーの判断「SMP に進む」で Pi の読みを途中で止めた（下の「Pi の K-D の 1 boot の読み」）。2026-10-12 Q1 が P5 に依頼。K-D は実装済み。review-1 の後、Q1 の判断で K2-1・K2-8・KD-1・K-2 (b) を実装（agent/p9、T1 の amd64 の boot と Pi の確認は未実施）。K-1 と K-2 (d) は設計を直しただけで、K-D の Pi の結果の後に Q1 が再 review に掛ける）
Phase disposition: normal

## 由来

Pi 4（1 core）で compositor の入力の thread I の box_lag が 17〜50ms（[p004](../phase004/phase.md) の Pi の数字、`KWL CURSOR_LAG` 38 行）。P4 の解析（p004 の「kernel の source から」「kernel の変更の設計」）では原因は kernel の側で、候補は 2 つ:

1. **1 core の同じ優先度の順番**（`src/kern/sched.c`）: `sched_yield`・`sched_preempt`・`switch_without_enqueue`・`sched_exit_current` は切り替えのたびに `cpu->preempt = 0` にするので、`woken` に残った thread は走っている thread が眠るか quantum（10 tick = 10ms）を使い切るまで待つ。起床で横取りされた thread は `run` の頭に戻る。`poll_notify`（`src/kern/poll.c`）は system 全体の 1 本の待ち行列を全部起こす。
2. **user の写しの global な lock**: copyin・copyout は毎回 `vm_metadata_enter()`（system 全体の 1 つの mutex）と process の `vm->lock` を取り、`mutex_unlock`（`src/kern/lock.c`）は待ち手を 1 つ起こすだけで持ち主を渡さない。

log からは 1 と 2 を分けられない。この Phase は分けるための診断（K-D）を入れ、直し（K-1・K-2）を設計する。

## 範囲

- K-D（診断、動作は変えない）: 起床・横取りの後に走るまでの最長と pid・tid、metadata lock の持った時間の最長と呼び出し元、待った時間の最長。2 秒ごとに 1 行。`SCHED_WAKE_LATENCY` の build の使い方。
- K-1（sched）と K-2（lock・VM）の設計: 不変条件、SMP（WS208）、公平さ、VM の不変条件、試験。**実装はしない**（Q1 が design-reviewer に掛けてから）。
- 触る所: `src/kern/sched.c`・`src/kern/vm.c`・`include/kern/sched.h`・`include/kern/vm-lock.h`、review-1 の後は `src/kern/lock.c`・`src/kern/waitq.c`・`src/kern/uaccess.c`・`src/kern/vmspace.c` も（2026-10-12 Q1 の許可、K2-6）。HAL の API・toolchain・`src/drivers/` は変えない。
- review-1 の後に足した実装（2026-10-12 Q1 の判断、review の推しの順）: K2-1（既存の欠陥、`mutex_lock` の忙しい回り）と K2-8（1 CPU で spin しない）、KD-1（報告の費用 `report_us=`）、K-2 (b)（(a) を含む）。

## 受け入れの条件

1. K-D: rpi4 と amd64 の vmunix が warning 0。`kern.perf=1` の無い boot では動作が今と同じ（測る所は全部 `sched_perf_active` の 0 で素通り）。host 試験 `plan/ws213/tests/kd-host-test.sh` PASS（plain と ASan+UBSan）。
2. K-D の Pi の 1 boot（焼き直しの後、BUG-294 で今は root の UFS が書けない）で、`KWL CURSOR_LAG` の 17ms 超えの行ごとに、下の「読み方」で原因を 1・2・両方・どちらでもない に分けた表を作る（Q1 かユーザーの手で）。
3. K-1・K-2 の設計が design-reviewer を通る（Q1）。実装は別の Phase（K-D の結果で要る方だけ）。
4. K2-1・K2-8・KD-1・K-2 (b): rpi4 と amd64 の vmunix が warning 0。host 試験 `plan/ws213/tests/lock-host-test.sh`・`pin-host-test.sh`・`kd-host-test.sh` が PASS（plain と ASan+UBSan）。T1 の amd64 の boot の回帰（下の「T1 で見る点」）が PASS。

## review-1 の対応（2026-10-12）

review の要点は [review-1.md](review-1.md)。Q1 の判断は review の推しの順: (1) K2-1 → (2) KD-1 と Pi の 1 boot → (3) K-2 (b)（(a) を含む）と K2-8 → (4) 直した K-1 → (5) 直した (d)。

| 指摘 | 重さ | 対応 |
| --- | --- | --- |
| K2-1 `mutex_lock` が signal の保留・terminate・stop の間に忙しく回る | 重大（既存） | 実装（`lock.c`、下）。あわせて見つけた起床の持ち去り（EINTR で抜けた待ち手が release の起床を持ち去る）も直した |
| K1-1 持ち分が次の tick で尽き、降格が `run` の頭に LIFO | 重大 | K-1 の設計を直した（持ち分は走り始めから 2 回目の tick、降格は横取りの区間の尾に FIFO）。実装しない |
| K2-2 (d) の仕様不足 | 重大 | (d) を状態の表と遷移で書き直した。実装しない |
| K2-3 (c) の前提不成立（`vmspace_destroy` が metadata だけで regions を書く） | 中 | (c) を落とした |
| K2-4 (a) で確かめを外すと pin の fault が先に起きる | 中 | (a) を (b) に含めて実装（範囲の確かめを fault より前に locks の下で） |
| K1-2 K-1 は mutex を持つ thread の tick の横取りを増やす | 中 | K-1 の試験に「mutex を持ったまま横取り」を足した。K2-1 と (b) の後に実装する順 |
| K1-3 古い印で同じ優先度の `run` の thread を選ぶ | 中 | K-1 の設計に入れた（`woken` の頭か厳密に高い `run` だけ） |
| KD-1 K-D の報告は user へ戻る任意の thread が同期に書く | 中 | 実装（次の行の `report_us=`） |
| K2-5 (d) の優先度と SMP | 中 | (d) の設計に書いた |
| K1-4 持ち分は `woken` に入れる時だけ。steal・`sched_set_cpu` | 軽微 | K-1 の設計に入れた（`run` へ移す所で 0 に） |
| K1-5 (a)+3 と比べていない | 軽微 | K-1 の設計に比較を書いた |
| K1-6 印を使う所は 4 か所、100Hz の arch では効かない | 軽微 | K-1 の設計に書いた |
| K2-6 触る file | 軽微 | 範囲を広げた（上） |
| K2-7 trylock の利用者 | 軽微 | (d) の設計に書いた（trylock は FREE からの CAS だけで、受け渡しの間は失敗する。利用者は今も失敗を扱う） |
| K2-8 1 CPU で `mutex_spin_take` を飛ばす | 軽微 | 実装（K2-1 と一緒に） |

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
kern: perf at_ms=T ms=W cpus=N switches=S report_us=R | woken=N max_us=X end_ms=E who=PID/TID/NAME ahead=K | preempted=N max_us=X end_ms=E who=PID/TID/NAME ahead=K dropped=D | meta takes=N waits=N max_us=X end_ms=E who=PID/TID/NAME at=ADDR | hold max_us=X end_ms=E who=PID/TID/NAME at=ADDR switched=K
```

- `report_us`（KD-1、2026-10-12 追加）: **前の行**を書いた thread がその報告に使った時間（窓を集めてから行を書き終わるまで、途中で横取りされた時間を含む）。最初の行は 0。報告は user へ戻る途中の任意の thread が同期に書く（`kern_logf` は 1 文字ずつ）ので、前の行の `at_ms` から `report_us` の間に重なる `KWL CURSOR_LAG` は、報告そのものの遅れを含み得る（その行が compositor の thread の報告なら特に）。行の長さは現実的な値で約 500 byte（`kern_logf` の 512 byte に収まる。値が全部最大の桁なら行の尾が切れる）。
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

## K2-1・K2-8・KD-1・K-2 (b) の実装（2026-10-12 P5、agent/p9）

commit: 874b5f935（K2-1・K2-8、`lock-host-test.sh`、review-1.md）、a30ce8e27（KD-1）、0c12276d0（K-2 (b)、`pin-host-test.sh`）。

### K2-1・K2-8（`src/kern/lock.c`、全部の mutex に効く）

- `mutex_lock` は `mutex_lock_interruptible` を EINTR の間繰り返すのをやめ、自分で `mutex_spin_take` の後に guard を取って `mutex_sleep_take(mutex, 0, &error)`（割り込み不可）で眠る（`mutex_wait` の再取得と同じ形）。signal の保留・terminate・stop の間も 1 回眠るだけで、持ち主の release で起きて取る。thread の無い時・mutex の無い時に何もせず戻るのは今と同じ。
- 副作用の違い: 今までは忙しく回る間に `waitq_sleep` が stop の要求で `thread->stop_interrupted = 1` を立て得た（`mutex_lock` 自体は取るまで回るので、system call は EINTR を返さず、印だけが残る）。割り込み不可の眠りはこの印を立てない。stop・signal は今どおり user へ戻る点で扱われる。
- 起床の持ち去り（実装中に見つけた既存の欠陥、同じ関数）: release で起こされた割り込み可能な待ち手が、起きた後の terminate・cancel・stop の確かめ（`waitq.c:183-193`）で EINTR を返すと、release の起床を持ったまま抜ける。lock は FREE か、FREE から CAS で取った thread（その release は誰も起こさない）が持ち、他の待ち手は眠ったまま残る。`mutex_sleep_take` が EINTR で抜ける時、待ち行列が空でなければ 1 つ起こす（`mutex_sleep_give_up`）。起きた待ち手は lock を取るか、CONTENDED に戻して眠り直す（余分な起床は無害）。K2-1 で割り込み不可の待ち手（signal でも起きない）が増えるので、この直しが要る。
- K2-8: `mutex_spin_take` は `hal_cpu_count() <= 1` なら 1 回だけ見て（FREE なら取る）、`hal_atomic_relax` で回らない。多 CPU は今と同じ 256 回の見直し（relax は見直しの間だけになり 257 → 256 回）。
- host 試験 `plan/ws213/tests/lock-host-test.sh`: `lock.c` の mutex の関数を切り出し、本物の `struct mutex`・`struct thread` と偽の waitq（signal の保留中の割り込み可能な眠りは眠らずに EINTR、他の眠りは持ち主の本物の `mutex_unlock` を走らせて起こす）・偽の CPU 数で: signal の保留中の `mutex_lock` が割り込み不可で 1 回眠って取る（EINTR の繰り返し 0）、1 CPU で relax 0 回・4 CPU で 256 回、FREE は guard・眠り無しで取る、割り込み可能な形は今どおり EINTR、EINTR で抜けた待ち手が次の待ち手を 1 つ起こす・誰もいなければ起こさない。旧 `lock.c` では 10 項目が FAIL（忙しい回りは 50 回で偽の持ち主が放して止まる）。

### KD-1（`src/kern/sched.c`）

`perf_report` の始めと終わり（`SCHED_WAKE_LATENCY` の行の後）で counter を読み、差を `sched_perf_report_cost` に置き、次の行の `report_us=` に出す（上の「行の形と読み方」）。

### K-2 (b)（(a) を含む、`src/kern/vmspace.c`・`src/kern/uaccess.c`）

- `vmspace_pin_user_pages`: 1 回の locks（metadata ＋ `vm->lock`）の持ちで、`vmspace_check_locked` → 全 page の準備の確かめ（`vmspace_pin_mapping_ready_locked`、今の `vmspace_pin_mapping_ready` の中身を locks の外に出した物）→ 全部準備済みなら今の validate と pin。準備の無い page があれば locks を外し、その page から最後までを今と同じ page ごとの確かめ ＋ `vmspace_fault` で fault し（`vmspace_pin_fault_from`）、locks を取り直す。fault の直後の回は準備の確かめを飛ばして validate と pin に進む（今の手順と同じ。fault しても準備の条件を満たさない page、例えば MAPPED の印の無い page でも、今どおり pin で決まり、fault を繰り返さない）。refault・BUSY の待ち・EBUSY の待ちの後は準備の確かめから。
- 意味: 範囲が無い・権限が足りない時は、どの page も fault する前に EFAULT（今の copyin・copyout の `user_range_check` と同じ。`uaccess_pin` は今まで先頭の page を fault してから EFAULT になり得たが、これも副作用無しになる）。fault の ENOMEM は今どおり ENOMEM。
- `copy_backing`: `VM_COPY_STACK_PAGES`（4 page）以下は pin の配列を stack に置く（`kern_calloc` 無し、ENOMEM の元が 1 つ減る）。それより長い範囲は先に `vmspace_check` で確かめてから配列を確保する（写せない巨大な範囲は今どおり EFAULT で、確保しない）。
- `copyin`・`copyout`: `user_range_check` を外した（同じ確かめを pin が locks の下で行う）。返す errno は今と同じ（destination・source が NULL は EFAULT、範囲は EFAULT、ENOMEM は ENOMEM）。
- metadata の入り（準備済みの時）: 4 page 以下の copyin・copyout は **1 回**（今は 1 ＋ N ＋ 1）、それより長い写しは 2 回（確かめと pin）。準備の無い時は 1 ＋（fault する page ごとに 1）＋ 1。exec・elf・ptrace の `vmspace_copy_to`・`vmspace_copy_from` も同じ道を通る。
- 持つ時間: 大きな pin（GPU の import の 512 page）は、locks の下の walk が 2 回から 3 回（準備・validate・pin）になる。K-D の `hold max_us` の `at` が `vmspace_pin_user_pages` なら見る。
- host 試験 `plan/ws213/tests/pin-host-test.sh`: `copyin`・`copyout` と `copy_backing`・pin・範囲の確かめ・準備の確かめを切り出し、本物の `struct vmspace`・`vm_region`・`vm_page` と偽の fault・backing・数える metadata lock で: 準備済みの 2 page は入り 1 回・fault 0・byte が届く、後半が無い範囲・後半が読み取りだけの copyout は fault 0・書き込み無しで EFAULT、準備の無い 3 page は 3 回 fault して写す、6 page は入り 2 回・確保 1 回、6 page の後に無い範囲は確保 0・fault 0 で EFAULT、COW の page は 1 回 fault して写す、fault が準備にしない page は 1 回の fault の後に pin される（fault の繰り返しは 100 回で FAIL）、fault の ENOMEM は ENOMEM。変異（stack の配列を使わない、範囲の確かめを最初の回で飛ばす（旧の順）、fault の直後も準備を確かめる）がそれぞれ FAIL になることを確かめた。

### 確かめた事

- build（warning 0）: rpi4 `make -o /home/awe/zedBSD-worktrees/p9/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix`（`arm64 vmunix check: PASS`）、amd64 `make -o /home/awe/zedBSD-worktrees/p9/build/amd64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/vmunix`（`amd64 vmunix check: PASS`）。4 つの file が両方で compile された。
- host: `lock-host-test.sh`・`pin-host-test.sh`・`kd-host-test.sh` が PASS（plain と ASan+UBSan）。
- 未実施: QEMU（T1）・Pi。

### T1 で見る点（amd64 の boot の回帰、Q1 が依頼）

全部の mutex（K2-1・K2-8・起床の受け渡し）と全部の copyin・copyout・pin（K-2 (b)）に効くので:

1. `plan/tools/boot-test.sh` で amd64 の login prompt（1 CPU と `-smp 4` の両方。1 CPU は K2-8 の spin 無しの道、4 は今までの spin の道）。
2. fork・vfork・posix_spawn と COW（`plan/tools/process/guest-vfork.sh`）: copyout の COW の page の fault（準備の確かめ → fault → pin）。
3. guest の make（`plan/tools/guest/make-cases.sh`）: exec の引数・環境の写し（`vmspace_copy_to`、短い写しは stack の配列）、多数の process の終了（terminate の間の `mutex_lock`）。
4. 終了・signal の回帰: 複数の thread の process を signal で終わらせる場面（terminate の間の vmspace の後始末で `mutex_lock` が眠ること）。tests/ のシナリオに signal・thread の終了の物があればそれ。
5. 時間があれば `plan/tools/kbench` の system call・pipe・fault の時間を前後で（悪化しない）。

## Pi の K-D の 1 boot の読み（2026-10-13 P5、途中で止めた）

2026-10-13 ユーザーの判断「これ以上詰めなくていい、SMP に進む」で途中で止めた。K-1・K-2 (d) は実装しない。`KWL CURSOR_LAG` 225 行の 1 行ごとの分類の表は作っていない。以下は止めるまでに読めた事（Q1 が写した main 1392d52 の image の log、1024x680、`v3d.perf=1 kern.perf=1`）。

- 場面の時刻（session.log から）: A pointer だけ 約 153〜164 s、B App Home の開閉 165.3〜181.0 s（開きは 6 回）、C files の起動 185.2 s と窓の drag 〜209.8 s、D calendar 212.7〜236.1 s・settings 237.8〜255.5 s、E terminal 257.0 s、wlshm 295.7〜379.5 s。
- **report_us**: 大きいのは最初の 4 行（7〜15 s、23〜38 ms）だけ。17 s 以降（場面の全部）は 18〜52 µs。表示の driver が画面を取る（14.3 s の `KWL MODE`、`rpi4_cons_suspend_output`）前は、rpi4 の早い console（`src/hal/arm64/bsp-rpi4/cons.c`）が 1 文字ごとに割り込みを止めて fb に 8x16 を描き、改行・折り返しごとに 80x25 を全部描き直すので、約 500 byte の行が 20〜40 ms かかっていた。この image は serial の console が無い（`CONFIG_RPI4_SERIAL_CONSOLE := n`）ので、画面を取った後の klog の console への写しは 1 文字ごとの確かめだけ。→ **報告そのものは場面の `CURSOR_LAG` の原因ではない**。直し（ring だけに書く報告）は要らないと判断し、実装していない。serial の console の build（115200 baud で 1 行 約 43 ms）や amd64 の text console では同じ懸念が残る（その時は ring だけに書く `kern_logf` の形を足す）。
- **meta（候補 2 と両方）**: 場面の殆どの 2 秒窓で `hold max_us` が約 10 ms（時々 20 ms）、持ち主は compositor（24/41、他に 24/55・24/56・24/61）、`at=0xffff0000000dae7c`、`switched=1`。待ちの最長も約 10 ms（24/56 など compositor の他の thread）。持ち主が metadata lock を持ったまま quantum（10 ms）分横取りされ、別の compositor の thread が待つ形（上の表の「両方」）。`at` の関数名は未確認（Pi の vmunix は raw Image で symbol が無く、手元の build と config が違う）。
- **woken（候補 1）**: 窓の最長は多くが 1〜3 ms（kernel thread 0/3）、時々 5〜10 ms。compositor の thread が woken の最長の窓は少ない。`preempted` の最長は 13〜38 ms、`ahead` 300〜2000（kernel thread 0/8・0/11 と compositor の thread）。
- **その他**: `switches` が 2 秒に 60 万〜160 万（場面の前の何もしない間も 160 万、21〜45 s は 440 万）。woken・preempted の数（2 秒に 3 万・5 千）では説明できず、sched_yield の回り（`src/drivers/usb/` などに多数）の可能性。未調査。
- **pass_max**: 707 ms は起動（14.26 s、出力を開く pass、`schedule_us=613901`、`KWL MODE switch_ms=425`）で場面の外。135 ms は 296.2 s の wlshm の接続（`shm_us=121719`、最初の shm の写し）。


## Pi 4（4 core）の stall watch の trap（2026-10-13 P5）

### 起きた事（Q1 から）

WS208 p007 の全部入りの kernel（`p29-kernel.elf`）で、使っている途中に `cpu2 unhandled exception esr=f2000001 elr=ffff0000000d0890`（brk）。chain は `mutex_unlock` ← `vmspace_pin_user_pages`（`db9b0`）← `copy_backing`（`de41c`）。1 core では起きていない。

### 原因（K-2 (b) ではなく arm64 の HAL）

- `db9ac` の `bl mutex_unlock` は `vmspace_pin_user_pages` の唯一の `mutex_unlock(&vm->lock)`（locks の下の walk と pin の後、`add x0, x24, #0x18`）。`d0890` の brk は `mutex_check_owner` の trap（持たれていない、または持ち主が `thread_current()` と違う）。
- `thread_current()` → `hal_task_get_current()` → `running_task()`（`src/hal/arm64/task.c`）は `mrs x8, TPIDR_EL1` と `ldr x8, [x8, #0x20]`（`cpu->running_task`）の **2 命令**。TPIDR_EL1 は task でなく CPU を指す。kernel は割り込みの中で横取りする（tick の quantum の終わり、起床の横取り）ので、2 命令の間で横取りされ、別の CPU で再開した thread は、元の CPU の今の task（他の thread）を自分と読む。`mutex_lock` で読み違えれば持ち主に他の thread を書き、`mutex_unlock` で読み違えれば持ち主の確かめが他の thread と比べる。どちらも unlock の trap になる。amd64 は同じ欠陥を BUG-082 で直していた（`%gs` の 1 命令の読み、`src/hal/amd64/task.c` の注記）が、arm64 は 2 命令のままだった。
- 1 core では移る先が無いので起きない。`vmspace_pin_user_pages` に出たのは、copyin・copyout ごとに通る最も頻繁な sleeping mutex の lock・unlock だから（K-2 (b) の locks の扱いは正しい。下の pthread の試験）。

### 直し（`src/hal/arm64/task.c`・`src/hal/arm64/space.c`、HAL の API は変えない）

- `running_task()`: CPU の record とその `running_task` を割り込みを止めて読む（`arm64_irq_save` → 読み → `arm64_irq_restore`）。`hal_task_get_current`・`arm64_task_running` と HAL の中の全部の利用がこれを通る。rpi4 の `mutex_unlock` の逆アセンブルで `msr DAIFSet` の後に `mrs TPIDR_EL1`・`ldr [x8,#0x20]`、その後に `msr DAIFClr` を確かめた。
- 同じ種類の欠陥（読んで見つけた、同じ commit）: `hal_space_switch()` は割り込みを止める前に `arm64_percpu_current()` を読んでいた。task の切り替え（割り込みは止まっている）の他に exec（`hal_task_exec_current`、横取りされ得る）から呼ばれ、間で別の CPU に移ると、選んだ space を元の CPU の record（`current_space`）に書き、その CPU の TLB の shootdown の判定を狂わせ得る。CPU を割り込みを止めてから読むようにした。

### 試験

- `plan/ws213/tests/current-host-test.sh`（新）: `running_task`・`hal_task_get_current` を切り出し、本物の pthread の 2 task を 2 つの模擬 CPU に置き、scheduler の thread が両方の割り込みが開いている時だけ 2 task の CPU を入れ替える。`hal_task_get_current()` が常に自分を返すこと。旧 `task.c` では `121913 moves, 266999 wrong answers` で FAIL、直した後は PASS（plain と TSan）。
- `plan/ws213/tests/pin-host-test.sh`（足した）: 偽の metadata lock・`vm->lock` を持ち主を覚える本物の pthread の mutex にし、持ち主でない release・二重の取り・順序（metadata が先）・lock を持ったままの fault を違反として数える。pthread の 3 つの copy（4 page 以下 2 つ、6 page 1 つ）と、locks の下で page を外す・COW にする・2 つ目の region を外して戻す thread を同時に走らせる。違反 0・copy の byte が読み戻せる・pin が全部戻ることを確かめる（plain、ASan+UBSan、TSan で PASS）。`vmspace_pin_user_pages` の `mutex_lock(&vm->lock)` を消す変異で FAIL（`vm->lock released by a thread that does not hold it`）。この試験は直す前の kernel の source でも PASS する（K-2 (b) の locks は正しく、原因は HAL）。
- `lock-host-test.sh`・`kd-host-test.sh` PASS。
- build（warning 0）: rpi4 `make -o /home/awe/zedBSD-worktrees/p9/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix`（`arm64 vmunix check: PASS`）、amd64 同じく `config-amd64.mk`（`amd64 vmunix check: PASS`）。
- 未実施: Pi 4（4 core）での確かめ（Q1 が vmunix を置く）、T1 の amd64 の boot。

### 残り（Q1 へ、この直しの外）→ 2026-10-13 P5 が直した（下の節）

- `hal_cpu_current()`（`src/hal/arm64/smp.c`）は `record->watch_countdown--` を割り込みを止めずに行う。横取りされて移った thread は他の CPU の record を書き、その CPU の自分の tick の確かめの間隔がずれ得る（WS208 の stall watch）。
- `hal_task_exec_current()`（arm64）は `task->fpregs` に初期値を写してから `arm64_fp_restore` するまでの間に横取りされると、切り替えの保存が古い program の FP の register で上書きし、新しい program が古い値で始まる。

### 同じ種類の洗い出しと直し（2026-10-13 P5、Q1 の判断で今直す。`src/hal/arm64/` の実装だけ、hal.h は変えない）

2 つの種類を探した: (A) TPIDR_EL1 で CPU の record を読み、割り込みを開けたまま record を使う。(B) task が走っている間は CPU の register（FP・TLS・debug の register）が本物で、切り替えが register を task の写しに保存するのに、写しと register を割り込みを開けたまま組で書き換える。

直した所:

| 所 | 種類 | 直し |
| --- | --- | --- |
| `smp.c` `hal_cpu_current()` | A | countdown と self-check は割り込みが止まっている呼び出しだけ（`arm64_irq_masked()`、DAIF.I）。開いている呼び出しは tick を受けるので要らない。番号の読みは今のまま（開いた呼び出しには読んだ時の CPU しか言えない） |
| `smp.c` `hal_cpu_panic_all()` | A | 最初に割り込みを止める（自分の番号を読んだ後に移ると、元の CPU を止めずに残す） |
| `watch.c` `arm64_watch_self_check()` | A | 割り込みが開いていれば何もしない（他の呼び出し元への備え） |
| `watch.c` `arm64_watch_fatal()`・`arm64_watch_fault()` | A | 入口で割り込みを止める（直後に止まる CPU。kernel の fault の処理や syscall の後の HAL_FATAL は割り込みが開いたまま来得る） |
| `watch.h` `arm64_watch_note_irq_on()` | A | 既に開いていれば何もしない（開いたままの `hal_irq_enable()`、例 `entry.c` で、移った先の CPU の印を消さない） |
| `task.c` `hal_task_exec_current()` | B | space・TLS・signal・FP の写しと `arm64_fp_restore`・TPIDR_EL0 の 0・`hal_space_switch` を割り込みを止めた区間で。TPIDR_EL0 の register を 0 にしていなかった（`task->tls = 0` だけ）のも直した。今の唯一の呼び出し元 `src/kern/exec.c` は割り込みを止めて呼ぶので、実機ではこの窓は開いていなかった（HAL の契約に割り込みの条件が無いので HAL で閉じた） |
| `task.c` `hal_task_set_tls()` | B | `task->tls` と TPIDR_EL0 を割り込みを止めて書く |
| `debug.c` `hal_task_set_user_gpregs()` の TLS | B | 同じ |
| `debug.c` `hal_task_set_user_fpregs()` | B | 保存・書き換え・読み込みを割り込みを止めて |
| `debug.c` `hal_task_set_single_step()`・`hal_task_set_debug_points()` | A+B | 走っている task への `arm64_debug_switch()` を割り込みを止めて（途中で移ると、元の CPU に points が残り、その CPU の `debug_points_loaded` は 0 のまま → 次の task が他の task の breakpoint で止まる） |

安全と判断した所（理由）:

| 所 | 理由 |
| --- | --- |
| `task.c` `hal_task_context_switch()`・`arm64_task_finish_switch()`、`debug.c` `arm64_debug_switch()` の switch からの呼び出し | kernel の 4 か所の呼び出し（`sched.c`）は全部 `hal_irq_disable()` の後。finish_switch は dispatch の stub が割り込みを開ける前に呼ぶ |
| `task.c` `running_task()` とその利用（enter/leave_user_frame・fork・exec_validate・signal・get_user_*） | 前の直しで割り込みを止めて読む。得た task は呼び出し元自身なので移っても変わらない |
| `task.c` `hal_task_fork_current()` の `arm64_fp_save` → 写し、`debug.c` `hal_task_get_user_fpregs()` | 保存と写しの間の切り替えは同じ live の値を保存するだけ（kernel は FP を使わない: rpi4 の kernel.elf の逆 asm で FP/SIMD の register を使うのは `arm64_fp_save`・`arm64_fp_restore` だけ） |
| `task.c` `hal_task_signal_enter()`・`signal_return()` | 保存先・読み元は `signal_fpregs` で切り替えが書かない。restore の後の切り替えは新しい値を保存する |
| `debug.c` `hal_task_get_user_gpregs()` の TPIDR_EL0 の読み | register は task に付いて移る |
| `irq.c` `hal_irq_disable()` | 先に止めてから記録 |
| `irq.c` `arm64_irq_dispatch()`、`int.c` `arm64_irq_handler()`、`bsp-rpi4/clock.c` `rpi4_timer_interrupt()` | 割り込みの中（DAIF.I は例外の入口で立つ）。handler の中の切り替えから戻る時も止まったまま（`sched_preempt` は handler の中では `hal_irq_disable()` が false を返すので開けない）。handler の後は record を読み直す |
| `irq.c` `hal_irq_send_eoi()` | handler の中で、ack した CPU で呼ぶ契約。移った後の呼び出しは `active_ack` の照合で HAL_FATAL になる |
| `space.c` `space_lock_handle()`・`space_unlock()`・`hal_space_switch()` | registry の lock・前の直しで割り込みを止めた中 |
| `space.c` `arm64_space_init` の 219 行・`arm64_space_select_system()`、`smp.c` `arm64_secondary_main()`、`bsp-rpi4/clock.c` `rpi4_timer_init()`、`task.c` `hal_task_create_for_init_context()` | boot 中（割り込みが開く前、task が無い） |
| `space.c` `space_active_anywhere()`、`watch.c` の他の CPU の読み | 他の CPU の record を acquire・relaxed で読むだけ（どの CPU からでもよい） |
| `spin.h` `arm64_spin_lock()`、`bsp-rpi4/cons.c` の output lock | `hal_irq_disable()` の後に spin |

試験: `plan/ws213/tests/percpu-host-test.sh`（新、`current-host-test.sh` の形）。`hal_cpu_current`・`running_task`・`arm64_task_running`・`hal_task_exec_validate`・`hal_task_exec_current`・`hal_task_set_tls`・`hal_task_set_user_fpregs` を切り出し（TPIDR_EL0 の asm は模擬の register の書き込みに置換）、2 つの模擬 CPU の上の 2 task を本物の pthread で回す。移す thread は両方の割り込みが開いている時だけ、両方の FP・TLS の register を task に保存し、CPU を入れ替え、新しい CPU に読み込む。`hal_memcpy` と `arm64_irq_save` の前で yield（割り込みの入る所）。確かめ: 各 CPU の countdown が自分の CPU の割り込みを止めた呼び出しの数だけ動く、self-check は割り込みを止めた時だけ、exec の後の FP が初期値で TLS が 0、set_tls の後の register と task が一致、set_user_fpregs の新しい値が CPU の register にある。
- 直した source: plain・TSan で PASS（2026-10-13）。
- 直す前の source（`git show HEAD:` の smp.c・task.c・debug.c を第 2 引数で渡す）: FAIL（countdown のずれ 2 CPU、割り込みが開いた self-check 1550、exec の古い FP 83354・古い TLS 99991、set_tls の不一致 190、set_user_fpregs の喪失 96565）。
- `current-host-test.sh` PASS。`git diff --check` OK。

build（warning 0）: rpi4 `make -j8 -o /home/awe/zedBSD-worktrees/p9/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix` rc 0（`arm64 vmunix check: PASS`）。amd64 `config-amd64.mk` は arm64 の HAL の変更だけなので up to date（rc 0）。逆 asm で `hal_cpu_current` が `mrs DAIF` の bit 7 で countdown を飛ばすこと、`hal_task_exec_current` の `msr DAIFSet` から `msr DAIFClr` の間に `arm64_fp_restore`・`msr TPIDR_EL0, xzr`・`hal_space_switch` があることを確かめた。
- vmunix: `/home/awe/zedBSD-worktrees/p9/build/ws213-p007-percpu/vmunix`（SHA256 1a3ef545…4e8e）、kernel.elf: 同じ directory（SHA256 5468b616…0051）。
- 未実施: Pi 4（4 core）の実機、QEMU。

その他の気づき（直していない）: `kernel_timer_handler()`（`src/kern/clock.c`、MI）は `sched_clock_cpu()` で切り替わって別の CPU で戻った後も、引数の古い `cpu` で `kern_random_tick(cpu)` を呼ぶ。`random_lock` の下なので害は sample の CPU 番号だけ。

## K-1 の設計（sched、候補 1 への直し）

### 今の順番（読んだ物、`src/kern/sched.c`）

- 起きた thread は、走っている thread 以上の優先度なら `woken[p]` の尾に入り、`cpu->preempt = 1`（`wake_enqueue_locked`）。`pick_next_locked` は各優先度で `woken` を `run` より先に取る。
- 印は 3 か所で使われる: user へ戻る途中（`sched_preempt_point`）、tick（`sched_clock_cpu` の `wanted`、kernel の中でも `preempt_count` が 0 なら）、他の CPU からの通知（`sched_cpu_notify`）。どれも `sched_preempt` を呼ぶ。
- `sched_preempt` は `cpu->preempt = 0` にしてから `woken` の頭を取り、走っていた thread を `run` の頭に（quantum の残りを持って）戻す。他の 3 つの切り替え（`sched_yield`・`switch_without_enqueue`・`sched_exit_current`）も最後に印を消す。
- **問題**: 印が消えた後に `woken` に残った thread は、新しく走った thread が眠るか、user へ戻る途中で新しい起床が印を立てるか、quantum（10 tick）を使い切るまで走れない。herd（`poll_notify` の全起こし）で `woken` に N 個並ぶと、最後の I は最大 N × 10ms 待つ（17〜50ms は 2〜5 個分に合う）。

### 変更（review-1 の K1-1・K1-3〜K1-6 で直した、2026-10-12。実装しない、K-D の Pi の結果の後に Q1 が再 review に掛ける）

1. **起床の持ち分（wake budget）**: thread に `sched.wake_ticks`（走り始めてから見た tick の数）と `sched.wake_budget`（真偽）を足す。`wake_enqueue_locked` で `wake_budget = 1` にする（`woken` に入れる時だけ）。**持ち分は走り始めから数える**（K1-1）: thread が切り替えで走り始める時（`perf_switch_locked` の隣、全部の切り替えの道）に `wake_ticks = 0`、走っている thread の tick ごとに 1 足す。持ち分は `wake_ticks` が 2 になった（走り始めてから 2 回目の tick）時に尽きる。1 回目の tick で尽きると、tick の直前に走り始めた thread が 1ms 未満で降格する（K1-1 の指摘）。2 回目なら実時間で最低 1 tick（1000Hz で 1ms）、最大 2 tick 走れる。counter（`sched_perf_counter`）の実時間で数える案は counter の無い platform があるので採らない。
2. **持ち分を使い切った時の順番渡し**: tick で、走っている thread の持ち分が尽き（または持ち分が無い thread）、その CPU の `woken` に**走っている thread 以上の優先度**の thread がいれば、`cpu->preempt = 1` にする（同じ tick の `wanted` が `sched_preempt` を呼ぶ。`preempt_count` が 0 でない時は今どおり `kern_preempt_enable` まで延びる）。
3. **横取りされた thread の戻り先**（K1-1）: `sched_preempt` で、走っていた thread の持ち分が残っていれば（走り始めて 2 回目の tick の前）**`woken` の尾**に戻す（持ち分はそのまま、次に走り始めた時に `wake_ticks` を 0 から）。持ち分が尽きていれば、`run[p]` の頭の**横取りされた区間の尾**に入れる（`struct sched_cpu` に優先度ごとの `run_preempted_tail[p]` を足し、横取りされた thread を区間の中で FIFO に並べる。区間は `run` の頭から続き、普通の `run` の thread（quantum を使い切って尾に行った物）より前）。今の `queue_prepend`（LIFO）だと、2 つ以上が続けて降格すると後に降格した方が先に走り、最初の I が他の quantum の後になる。区間の尾の印は、区間の thread が `run` から出る時（pick・remove・steal）に直す。
4. **古い印で同じ優先度の `run` の thread を選ばない**（K1-3）: `sched_preempt` は `best_priority_locked` が今の thread の優先度以下なら `pick_next_locked` を呼ぶので、`woken` が空で印だけが残っていると（印は読んでから lock を取るまでに古くなり得る）、同じ優先度の `run` の頭（横取りされた区間の thread）に切り替わる。直し: 切り替えるのは、(i) 今の thread の優先度以上の `woken` の頭、または (ii) 厳密に高い優先度の `run` の thread、の時だけ。それ以外は印を消して今の thread が走り続ける。
5. **持ち分を消す所**（K1-4）: `run` へ移す所（steal の `take_stealable_locked`（`sched.c:2442` 付近の 2 か所）、`sched_set_cpu`（`sched.c:1654` 付近の 2 か所）、quantum の終わりの `sched_yield`、持ち分の尽きた降格）では `wake_budget = 0`。`woken` に入れる所（`wake_enqueue_locked` と、3 の持ち分の残った戻し）だけが 1 にする。
6. 切り替えでの印の消し方は今のまま。印を使う所は 4 か所（`sched_preempt_point`、tick の `wanted`、`sched_cpu_notify`、`kern_preempt_enable`（`sched.c:647`）、K1-6）。全部 `sched_preempt` を通るので、3〜5 は `sched_preempt` と tick の中だけで足りる。

### P4 案の (a)「印を残す」＋ 3 との比べ（K1-5）

- (a)＋3: 切り替えの後も `woken` が空でなければ印を残し、横取りされた thread を `woken` の尾に戻す。利点: 待つ thread は次の preempt の点（user へ戻る途中・tick・`kern_preempt_enable`）ですぐ走る（tick を待たない、最短）。欠点: 起きて走り始めた I 自身も、poll から user へ戻る途中の `sched_preempt_point` で次の起きた thread に横取りされ、`woken` が空になるまで I は user に戻れない（herd の N 個が全部 kernel の中の短い区間を走るまで）。N 個が全部 compositor 以外（client の poll）なら I の遅れは今より短いが、K-1 の持ち分の案より長くなり得る。
- 持ち分の案: I は走り始めてから最低 1 tick は横取りされない（user へ戻って event を読むのに足りる）。前に並ぶ N 個の待ちは最大 N × 2 tick。
- どちらを採るかは K-D の Pi の `woken` の `ahead`・`dropped` で決める: `ahead` が小さい（1〜2）のに待ちが長いなら (a)＋3 でも十分、`ahead` が大きい（herd）なら持ち分の案。今の推しは持ち分の案。

### 100Hz の arch（K1-6）

i386・m68k・sparcv9 は `HAL_TIMER_FREQUENCY` が 100（tick 10ms）なので、持ち分の 1〜2 tick は 10〜20ms で quantum と大差なく、K-1 は効かない。対象は 1000Hz の amd64・arm64（Pi）で、100Hz の arch では動作が今とほぼ同じ（悪化しない）ことだけを確かめる。

### 不変条件

- (I1) `woken[p]` の thread は全部 RUNNABLE で `wake_budget` が 1。`run[p]` の thread の `wake_budget` は 0（起床の時に走っている thread より優先度が低かった thread は今どおり `run` の尾に入り、持ち分を持たない）。`run[p]` は頭から「横取りされた区間（FIFO）」「その他」の順。
- (I2) `woken` を持つ CPU では、走っている thread の持ち分が尽きているなら、次の tick までに `cpu->preempt` が立つ（2 で立てる）。したがって `woken` の頭は、前の thread が走り始めてから最大 2 tick で走る。
- (I3) quantum の扱いは変えない: `wake_budget` は quantum を補充しない。起床と眠りを繰り返す thread も quantum を使い切れば `sched_yield` で `run` の尾へ行く（今の `wake_enqueue_locked` の注記の公平さの錨）。
- (I4) `wake_budget` と K-D の stamp は、その thread の CPU の lock の下で読み書きする。移動（`sched_set_cpu`・steal）では thread と一緒に動く（`need_migrate` の手順の中）。
- (I5) idle の thread は `wake_budget` を持たず、順番渡しの対象にならない。

### 待ちの上限（1 core）

`woken` に I の前に N 個並ぶと、I が走るまでは最大 2N tick ＋ 走っている thread が次の preempt の点に着くまで（kernel の `preempt_count` の区間、割り込みを止めた区間）。今の最大 N × 10ms が N × 2ms になる。herd の N が大きい場合は残る（下の K-3）。

### 公平さ

- `run` の thread（CPU を使い続ける M・R・v3d の decode の worker・client の描画）は、`woken` が空になるまで走れない点は今と同じ。`woken` の各 thread は起床ごとに走り始めから最大 2 tick（持ち分の残った戻しの後も 2 tick ずつ、quantum の錨 (I3) まで）なので、`woken` に居られる時間は今より短い（今は起きた thread が眠るまで最大 10 tick 前に居られた）。CPU を使い続ける thread の待ちは今と同じか短い。K-D の `preempted` の `max_us` で前後を比べる。
- 起床を 1 tick より短い間隔で繰り返す thread は、`run` の thread を飢えさせ得る（今も同じ。quantum の錨 (I3) で 10 tick ごとに 1 回は `run` の尾へ行く）。悪化はしない。
- 同じ優先度の `woken` の中は FIFO＋尾への戻しで、どの thread も前に並ぶ数 × 2 tick 以内で頭に来る（飢えない）。

### SMP（WS208）

- 列・lock・tick は CPU ごと（`struct sched_cpu`、`sched_clock_cpu` はその CPU の tick だけ）。`wake_budget` を減らすのは thread が走っている CPU の tick で、その CPU の lock の下。WS208 で arm64 の CPU ごとの generic timer が入れば Pi でも同じ。
- 他の CPU からの起床: `wake_enqueue_locked` は相手の CPU の lock の下で `woken` に入れ印を立て、`notify_cpu` が相手の `sched_cpu_notify` → `sched_preempt` を起こす（今どおり）。3 の尾への戻しは自分の CPU の列だけ触る。
- steal（`take_stealable_locked`）は `woken` の頭を他の CPU の `run` の尾へ移す。移った thread は idle の CPU ですぐ走るので順番は悪くならない。`wake_budget` は移った先で減る。
- 4 core では 1 core より起きた thread の待ちは小さいが、herd・I の待ちの形は同じ（compositor の thread が同じ CPU に並ぶと同じ事が起きる）。K-1 は SMP の前後どちらでも効き、WS208 の作業と衝突する所は無い（HAL を触らない、MI の `sched.c` だけ）。WS208 の merge の後に、K-1 の host 試験に 2 CPU の場面を足す。

### 試験（実装の Phase で）

1. host（新規）: `sched.c` を偽の HAL（`hal_task_context_switch` は記録だけ、`curthread` は試験が切り替える）と偽の tick で build する順番の試験。場面: (a) CPU を使い続ける thread の上で N 個が起きる → 各々が 2 tick 以内に順に走り、最後が 2N tick 以内、(b) X・Y・Z の ping-pong が起きない（尾への戻し）、(c) 起床と眠りを繰り返す thread も quantum で `run` の尾へ行く（I3）、(d) `woken` が空になると `run` の頭（横取りされた M）が走る、(e) `preempt_count` の間は延び、`kern_preempt_enable` で行われる、(f) idle は対象外、(g) 2 CPU の steal（WS208 の後）、(h) **tick の直前に走り始めた thread は次の tick で降格しない**（K1-1）、(i) **2 つ以上が続けて降格した時、降格した順に走る**（K1-1、横取りされた区間の FIFO）、(j) 古い印で同じ優先度の `run` の thread に切り替わらない（K1-3）、(k) steal・`sched_set_cpu` の後に持ち分が残らない（K1-4）、(l) **mutex を持ったまま tick で横取りされた thread**（K1-2）: 待ち手は K2-1 の割り込み不可の眠りで回らず、持ち主は横取りされた区間の FIFO で 2N tick 以内に戻って放す。K-D の `kd-host-test.sh`・`lock-host-test.sh` も通す。
2. T1: amd64 の boot-test（1 CPU と `-smp 4`）、`plan/tools/latency/run-wakebench-qemu.sh` の起床の遅れを K-1 の前後で（QEMU の数字は傾向だけ）。
3. Pi（`kern.perf=1`）: p004 の場面 (1)〜(4) で、`woken` の `max_us`・`dropped`・`preempted` の `max_us` と `KWL CURSOR_LAG` の数を K-1 の前後で比べる。受け入れは p004 の 5（box_lag の p99・最長 ≤ 16.7ms）。

## K-2 の設計（lock・VM、候補 2 への直し）

### 今（読んだ物）

- `copyin`・`copyout`（`src/kern/uaccess.c`）は、1 回の写しで metadata lock を **3 回**取る: `user_range_check` → `vmspace_check`（metadata ＋ `vm->lock`）、`copy_backing` → `vmspace_pin_user_pages` の中の `vmspace_pin_mapping_ready`（page ごとに metadata ＋ `vm->lock`）と、範囲全部の pin（metadata ＋ `vm->lock` を最後の page の pin まで持つ）。`vmspace_pin_user_pages` は自分でも `vmspace_check_locked` で範囲を確かめるので、最初の `user_range_check` は重複している。
- `vmspace_check_locked` は `vm->regions` の list を頭から辿るだけ（region の数に比例）。region の中身（private page・object）には触らない。
- 大きな範囲の pin（import の 2MB の buffer、512 page）は metadata を全 page の pin の間持つ（「wire/unlock/rewalk の隙間で unmap・remap・fork が page の同一性を変えるのを閉じる」ため、`vmspace.c` の注記）。`vmspace_wire_range` は page ごとに入り直す。
- `mutex_unlock` は `locked` を FREE にしてから待ち手を 1 つ起こす。起きた待ち手が走る前に、出た thread（または他の thread）が CAS で取り直せる（横取り）。1 core では、出た thread が同じ kernel の入りの中で入り直すと（pin の loop、fault の中の 2〜3 回の入り）、待ち手は次の preempt の点まで走れず、走った時には lock が持たれていて、また眠る。`mutex_spin_take` の 256 回の見直しは 1 core では無駄（持ち主は走れない）。

### 変更（小さい順。K-D の結果で要る物だけ実装する）

- **(a)・(b) は実装した**（2026-10-12、review-1 の K2-4 で (a) を (b) に含めた形。上の「K-2 (b) の実装」）。以下は設計の時の記録。
- **(a) 重複の範囲確かめを外す**（`uaccess.c` の `copyin`・`copyout` の `user_range_check`。`copyinstr` は page ごとに `copyin` を通る）: pin が同じ確かめを locks の下で行う。metadata の入りが 3 → 2 回。返す errno: 今は確かめの失敗で EFAULT、pin の失敗は ENOMEM 以外 EFAULT（`copy_backing`）。ただし `copy_backing` は pin の配列を `kern_calloc` で確保してから pin するので、確かめを外すと、写せない巨大な範囲が EFAULT ではなく ENOMEM になり得る（大きな確保も起きる）。そこで外すのは小さな写し（配列が固定の小さな大きさ、例えば 4 page 以下で、配列を stack に置ける時）だけにし、大きな写しは今の確かめを残す。範囲の検証（`vmspace_user_range_valid`）は `copy_backing` の先頭に残る。
- **(b) 準備済みの page の確かめと pin を 1 回の持ちで**: `vmspace_pin_user_pages` で、まず locks を 1 回取って全 page が準備済み（`vmspace_pin_mapping_ready` と同じ条件）ならそのまま pin まで進む。準備済みでない page があれば locks を外して fault し、今の手順（retry_faults）に戻る。小さな写し（I の pollfd・event）では metadata の入りが 1 回になる。
- **(c) は落とした**（review-1 の K2-3）: 前提の「regions の書き手が全部 `vm->lock` を持つ」が成り立たない（`vmspace_destroy`（`vmspace.c` の 6871-6911 付近）が metadata だけで regions を書く）。(b) で copyin・copyout の metadata の入りが 1 回になったので、(c) の利点（metadata を取らない確かめ）は小さい。以下は設計の時の記録。
- **(c) `vmspace_check` を metadata 無しで**: `vm->lock` だけで `vmspace_check_locked` を呼ぶ。lock の順（metadata → `vm->lock` → reclaim・object → spin の state）には反しない（`vm->lock` だけを取り、持つ間に metadata を取らない）。**前提（実装の前に確かめる VM の不変条件）**: `vm->regions` の list と各 region の `start`・`size`・`prot`・`next` を書く所が全部 `vm->lock` を持つ（`vmspace.c` の region の作成・split・unlink・mprotect・stack の guard、約 16 か所。`find_region_locked` の読み手と同じ）。1 つでも metadata だけで書く所があれば (c) は採らない。
- **(d) 横取りの後の受け渡し（handoff）**（review-1 の K2-2・K2-5 で書き直した、2026-10-12。実装しない。K-D の Pi で `waits` が多く `hold` が短い時だけ実装に進み、その前に Q1 が再 review に掛ける）。K2-1・K2-8 は実装済みで、(d) はその上に立てる。
  - **locked の語の bit**（今の 0・1・2 の値を bit にする）: `HELD`（1、誰かが持つ）、`WAITERS`（2、眠っている待ち手がいるかもしれない、release は guard を取って起こす）、`HANDOFF`（4、次の release は FREE にせず `handoff_to` に渡す）。FREE は 0。`HANDOFF` は `HELD` と一緒にだけ立つ（FREE|HANDOFF は無い）。`struct mutex` に `handoff_to`（`struct thread *`、guard が守る）を足す。
  - **遷移の表**（G は guard の下）:

    | 操作 | 前 | 後 | 方法 |
    | --- | --- | --- | --- |
    | `mutex_take_free`（trylock・spin・lock の速い道） | 0 | `HELD` | CAS（0 からだけ。受け渡しの間は `HELD` なので失敗する、H1） |
    | 待ち手の印（`mutex_sleep_take`、G） | 任意 | `old \| HELD \| WAITERS` | `fetch_or`。`old` に `HELD` が無ければ取った（`HANDOFF` は `HELD` と一緒にしか立たないので、取った時 `HANDOFF` は無い）。今の `exchange(CONTENDED)` は `HANDOFF` を消すので使わない（K2-2 の lock.c:526） |
    | 横取りの検出（G） | 起こされた（waitq の sequence が進んだ）待ち手の `fetch_or` が `HELD` を見た | `\| HANDOFF`、`handoff_to = 自分` | `handoff_to` が空の時だけ（既に他の thread なら立てない、1 つの受け渡しは 1 つの thread へ） |
    | release（持ち主） | `HELD`（待ち手無し） | 0 | CAS `HELD → 0`（速い道、guard 無し） |
    | release（持ち主） | `HELD\|WAITERS`（`HANDOFF` 無し） | 0、起こす | G で `exchange(0)` の後 `waitq_wake_one`（今と同じ）。`exchange(FREE)` を guard の外で行う今の形（lock.c:382）は、`HANDOFF` を立てる待ち手と競うので、`WAITERS` のある release は G の中で語を書く |
    | release（持ち主） | `HELD\|WAITERS\|HANDOFF` | `HELD`（他の待ち手がいれば `\|WAITERS`）、`owner = handoff_to` | G で: `handoff_to` の token を waitq から外して起こす（新しい waitq の API `waitq_wake_thread(queue, thread)`、token が queue に無ければ起こすだけ）、`owner` を書き、`handoff_to = NULL`、`HANDOFF` を消す。語を FREE にしない（H1） |
    | `mutex_wait` の譲り（G） | 同上 | 同上 | release と同じ関数（`mutex_release_locked`）を通す（H4。lock.c:429 の `exchange(FREE)` を置き換える） |
    | 受け取り | — | — | 待ち手は G の下で、**眠りの前と、`waitq_sleep` のどの戻り（0・EAGAIN・EINTR）の後も**、まず `owner == 自分` を見る。自分なら取ったものとして戻る（H2: EINTR でも 0 を返す。signal は保留のまま user へ戻る点で配る） |
    | 割り込み可能な待ち手が諦める（G） | `handoff_to == 自分` | `HANDOFF` を消し `handoff_to = NULL`、待ち行列が空でなければ 1 つ起こす（K2-1 の `mutex_sleep_give_up` と同じ） | `owner == 自分` なら諦めず取ったとする（H2） |
  - **待ち行列の尾の問題**（K2-2: 取り損ねた待ち手は `waitq_sleep` で尾に戻る、waitq.c:149-157）: 受け渡しの相手は waitq の頭ではなく `handoff_to`（取り損ねた thread 自身）にする。尾に並び直しても次の release はその thread に渡る。`waitq_wake_one` は void で頭を外すだけなので、特定の thread を外して起こす `waitq_wake_thread` を `waitq.c` に足す（MI の kernel の内部の API、HAL ではない）。
  - **guard を外す窓**（K2-2: waitq.c:110-112・206-208、`waitq_sleep` は signal を確かめる間 guard を外す）: その窓の間に release が `owner = 自分` にして起こす（token は queue にあるか、既に外れている）。`waitq_sleep` の戻りの後に `owner == 自分` を見るので取りこぼさない。`KERN_TEST_CHECKPOINT(KERN_TEST_WAIT_BEFORE_REGISTER)`・`AFTER_REGISTER` で窓の中に release を入れる host 試験を作る。
  - 不変条件: (H1) `HANDOFF` の間は語が 0 にならない（`mutex_take_free` の CAS は 0 からだけ）。(H2) 渡された所有は拒めない。(H3) 受け渡しは `handoff_to`（取り損ねた thread）へ。(H4) `mutex_wait` の譲りも同じ release を通す。(H5) `owner` は持ち主か、G の下の渡す側が書く（`mutex_owned` の注記を直す）。(H6) `handoff_to` は G の下でだけ読み書きし、立っている間は `HANDOFF` も立つ。
  - **trylock の利用者**（K2-7: `file.c:717,1200,1268,2699,3150`、`readahead.c:285,294`、`unix-socket.c:1677`、`vm.c:4166,10292`）: 受け渡しの間（相手が起きて走るまで）の trylock は失敗する。利用者は今も「持たれている」時の失敗を扱っている（読んだ所では、どれも諦めるか後でやり直す）ので意味は変わらないが、受け渡しの間だけ失敗が増える。実装の時に各々の失敗の道を確かめる。
  - **優先度と SMP**（K2-5）: 受け渡しは優先度を見ない。`handoff_to` より高い優先度の待ち手（例 `USB_BT_SCO_PRIORITY` 4 の BT SCO の kthread、`usb-bt.c:1787`）は、受け渡しの 1 回分（`handoff_to` の 1 回の持ち）だけ長く待つ。上限は 1 回の持ちで、飢えは無い。SMP では、受け渡しの間は相手が起きて走るまで（他の CPU なら IPI と切り替え）誰も持てないので lock が空く。受け渡しは「横取りされた待ち手がいる時」だけ立つので普段の速さは変わらない。K-D の窓に受け渡しの数を足して Pi と `-smp 4` で見る。
  - 範囲: 全部の mutex に効く（lock.c・waitq.c の共通の変更）。metadata だけに限る案より単純で、他の mutex の飢えも止まるが、review の範囲が広い。
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

- K2-1・K2-8・KD-1・K-2 (b) は実装済み（agent/p9）。T1 の amd64 の boot の回帰（上の「T1 で見る点」）の結果を Q1 が判定するまで受け入れの 4 は未了。
- K-1（直した設計）と K-2 (d)（書き直した設計）の実装は、K-D の Pi の結果の後、Q1 の再 review を通ってから、要る方から（別の Phase にするかは Q1）。
- Pi の K-D の 1 boot は、BUG-294（root の UFS が書けない）の焼き直しの後。
- WS208（SMP）の merge の後は K-1 の host 試験に 2 CPU の場面を足す。
