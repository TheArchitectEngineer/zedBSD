<!-- awesome-plan project=zedbsd record=ws291-job-submit-design -->

# 案: reserve・command・commit を 1 本の ioctl に（GPU core の job の ABI、未実装）

- 親: [WS291](ws.md)、[ws291-p006](phase006/phase.md) の q1668-i02 節。書いた: 2026-10-29 P2。状態: 案（Q1 の判断待ち、実装しない）。
- 理由: subs-N（1 frame に 1000 回の vkQueueSubmit）は 1 submit 約 35 µs（Linux の RADV は約 10 µs の CS 1 本）。成功の道で GPU の ioctl が
  JOB_RESERVE（timing で約 5〜7 µs）・GPU_COMMAND（約 27 µs、kernel の frame 約 13 µs）・JOB_COMMIT（約 3 µs）の 3 本と、後の CONSUME の WAIT
  1 本（record ごとに 1 回、ledger の reap）。system call 自体は getppid 190 ns・GPU_GET_INFO 1150 ns（plan/ws291/tests/ioctl-cost.c、実機）。

## 今の形（main d94ceac00、gpu.c の行は目安）

1. GPU_JOB_RESERVE（gpu_job_reserve_ioctl、gpu.c 5517〜）: session の admission（gpu_session_enter）の外。monitor の起動、fence の参照（fd ≥ 0 の時）、
   sequence（gpu_handle_allocate）、completion の slot（gpu_completion_reserve、64 個、満ちたら EAGAIN）、registry lock の下で RESERVED と deadline
   （CONFIG_GPU_JOB_RESERVATION_MS）、fence の bind（gpu_job_bind、fences[] の空き、drv_gpu_fence_bind）、copyout、driver の jobs->reserve
   （amdrdna は runtime_reserve: timeline の確かめ、marker を取る、pending++。venus は transport の descriptor の組を取る）。
2. GPU_COMMAND（gpu_command_ioctl、3338〜、admission の中）: stream を kernel に写し、ops->command(priv, backend, buf, bytes)。sequence も reservation も
   渡らない。amdrdna の runtime_command は session の queue が空なら呼び手の thread で frame を走らせる（native word の flag 2、無理なら EAGAIN →
   libvulkan は GPU_COMMAND_SUBMIT と WAIT に戻る）。Vulkan の結果は reply の channel で libvulkan に返る。
3. GPU_JOB_COMMIT（gpu_job_action_ioctl、5642〜、admission の外）: gpu_job_observe で record を pin、gpu_job_admit（RESERVED → COMMITTED、
   drv_gpu_fence_admit、deadline を EXECUTION_MS に、monitor を起こす）、driver の jobs->commit（amdrdna は marker を queue の frame の後ろに。venus は
   descriptor を publish）。失敗は jobs->cancel(fault) で session を失う。
- reservation と command を結ぶのは各 driver の中の順だけ（commit した marker は、それより前に送った frame の後ろに並ぶ）。userspace は native の
  reply が成功の時だけ COMMIT し、確かな拒否（OOM）なら CANCEL、不確か・device lost なら CANCEL_FAULT。

## 案 A: GPU_JOB_SUBMIT（1 本で reserve → 同期の command → commit か cancel）

- uapi（include/uapi/gpu-job.h に足す、既存の ioctl・struct は不変）:

```c
#define GPU_CAP_JOB_SUBMIT	131072U
#define GPU_JOB_SUBMIT		_IOWR('G', 46, struct gpu_job_submit)
#define GPU_JOB_SUBMIT_COMMITTED	1U	/* outcome */
#define GPU_JOB_SUBMIT_CANCELED		2U
#define GPU_JOB_SUBMIT_FAULTED		3U

struct gpu_job_submit {
	uint32_t version;
	uint32_t size;
	int32_t fd;		/* as gpu_job_reserve */
	uint32_t flags;		/* 0 */
	uint64_t generation;
	uint32_t timeline;
	uint32_t bytes;		/* the command stream, as gpu_command */
	uint64_t address;
	uint64_t sequence;	/* out: the job's sequence (0 when not reserved) */
	uint32_t outcome;	/* out */
	uint32_t reserved;
};
```

- core（gpu.c）: admission（gpu_session_enter）の中で、今の reserve と同じ確かめ・確保（同じ関数を分けて使う）→ driver の新しい op
  `jobs->submit(priv, backend, timeline, buf, bytes, completion, reservation, &outcome)` → outcome が COMMITTED なら gpu_job_admit と同じ遷移
  （registry lock の下）、CANCELED なら unbind と discard（fence は未 signal のまま）、FAULTED なら今の CANCEL_FAULT と同じ（session を失う）。
  copyout は最後に 1 回（sequence と outcome）。copyout の失敗は、job が既に COMMITTED なら EFAULT でも job は残る（今の COMMIT 後と同じ扱い:
  userspace は sequence を知らないので context を lost にする、を uapi に書く）。
- driver の op の約束: frame（command）を走らせた結果を、Vulkan の reply を書いた上で outcome で返す。COMMITTED の時は marker をその frame の
  直後に並べる（今の runtime_commit と同じ順）。frame を走らせられない時（amdrdna の queue に待つ frame がある時の EAGAIN、ring の空きが無い等）は
  何もしないで EAGAIN を返し、core は reservation を確保していればそれを戻して EAGAIN を返す。
- outcome を driver が決める理由: reply の VkResult（OOM は確かな拒否で fence を触らない、Vulkan の約束）を core は読まない。amdrdna は executor が
  VkResult を作るので、reply を書く所で outcome も分かる。
- libvulkan: capability と capset の同期の flag がある時だけ、queue_attempt の reserve・execute・finish を GPU_JOB_SUBMIT 1 本に（ledger への
  notification の追加は sequence の返りの後、pin は不要: 同じ ioctl の中で COMMITTED になる）。EAGAIN なら今の 3 本の道（reserve から）をそのまま。
  capacity の EAGAIN（completion の slot・fences[] の満杯）は今の reserve の EAGAIN と同じ扱い（GPU_JOB_CAPACITY の query と待ち）にするため、
  outcome とは別に errno を EAGAIN、`sequence = 0`、`outcome = 0` で区別し、driver の EAGAIN（frame を今は走らせられない）は errno EBUSY に分ける。
- 互換: 古い kernel は GPU_CAP_JOB_SUBMIT を出さないので libvulkan は今の道。古い libvulkan は新しい ioctl を使わない。op を持たない driver は
  capability を出さない（core が ops->jobs->submit の有無で決める）。GPU_ABI_VERSION は上げない（新しい ioctl と bit だけ）。

## 各 driver への影響

| driver | 今の同期の command | 案 A の作業 |
| --- | --- | --- |
| amdrdna | runtime_command（flag 2） | jobs->submit を足す: runtime_command の同期の道で frame を走らせ、reply の VkResult から outcome、COMMITTED なら runtime_commit と同じ marker の並べ。最初に実装する |
| nvrtx・i915・ghci・powervr・bcm2711 | ops.command あり（同期） | 足さなくてよい（capability を出さないだけ）。足すなら amdrdna と同じ形 |
| venus | 無し（SUBMIT と transport） | 足さない。transport の descriptor の reserve と publish が別の段で、1 本にしても host の往復は減らない |

- gpu.c の変更: reserve と commit の本体を、ioctl の copyin・copyout と分けた内部の関数にして、GPU_JOB_RESERVE・GPU_JOB_COMMIT と新しい ioctl の
  両方から呼ぶ。monitor・deadline・fence の bind・admit の論理は 1 か所のまま。admission の中で走る（今の reserve と commit は外）ので、同じ open の
  別の thread の reserve・commit・wait は今どおり admission を取らずに進める。stop・drain は session の pending（amdrdna）と record の状態で今どおり。

## 期待の効果（見積もり、未計測）

- 減る物: system call 2 本（入口・copyin・copyout・file の参照、約 1〜2 µs ずつ）、record の pin・observe の lookup の 2 回目、registry lock の
  取り直し。JOB_RESERVE（5〜7 µs）と JOB_COMMIT（約 3 µs）の大部分のうち、確保と状態の遷移の本体（約 2〜3 µs の見込み）は残る。
- subs-1000 の 1 submit 約 35 µs → 約 28〜30 µs（−15〜20 %）。subs-1000 の fps 24.7 → 約 29〜31。Linux（58 fps、10 µs）にはまだ届かない:
  残りは GPU_COMMAND の中の kernel の frame（decode・executor・ring、約 13 µs）と libvulkan の encode・system call。

## 案 B（案 A に足せる、別の判断）: 終わった record をまとめて consume

- GPU_COMMAND_WAIT は 1 本で 1 sequence。ledger の reap は record ごとに 1 本（subs-1000 で 1 submit に約 1 本、timing で reap 1 回 約 16 µs = 8〜14 本）。
- 案: `GPU_JOB_RETIRE`（struct に user の配列の address と最大数）: 終わって observer の無い record を最大 N 個 consume し、sequence と status を
  配列に返す。libvulkan の reap は 1 本で済む（失敗の status は今どおり context の loss）。互換は案 A と同じ（capability の bit）。
- 効果の見積もり: 1 submit あたり約 1 µs（system call 約 1 本分）。

## 決めてほしい事（Q1・ユーザー）

1. 案 A を採るか（uapi の追加、gpu.c の reserve・commit の分割、amdrdna の jobs->submit、libvulkan の新しい道）。採るなら別の Queue で、
   design-reviewer に通してから。
2. 案 B を一緒にするか。
3. outcome を driver が返す形（core は reply を読まない）でよいか。
