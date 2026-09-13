# q310 transport・同期・回復の実装契約

WS014 p007 の P1/P3/P6/P8 と review2 B を、2026-09-13 の実装と有限受入に基づいて記録する。標準 OPAQUE memory の host renderer 差分は [renderer-opaque](renderer-opaque/README.md)、表示・WSI は [display-wsi.md](display-wsi.md)、追加 UAPI は [gpu-uapi-contract.md](gpu-uapi-contract.md) を参照する。HAL の追加変更は行っていない。

## 通知が示す境界

Venus の control queue は 4 request slot を持ち、それぞれが独立した command/response DMA と descriptor chain を所有する。GPU queue marker は先頭 3 slot に限定し、残る 1 slot を decoder/control の進行用に確保する。GPU core の completion record は別に 1 open あたり最大 64 件あり、完了済み record は明示的な CONSUME まで保持できる。両者は同じ上限ではない。

`GPU_COMMAND_SUBMIT` は command をコピーして受理し、session に属する sequence を返す。slot/record が確保できない `EAGAIN` は未受理であり、callback 義務を生じない。受理済み request は used descriptor、応答サイズ、fence/context/ring の対応を確認して一度だけ完了する。queue lock の外で GPU core callback と readiness 通知を呼ぶ。

| 観測 | 分かること | その観測だけでは分からないこと |
| --- | --- | --- |
| IRQ、control used-ring | 対応する transport request の応答が利用可能 | Vulkan command の成功、GPU が画像への書込みを終えたこと |
| context fence、timeline 0 | Venus decoder timeline 上の通知境界 | 個々の Vulkan reply の `VkResult`、native GPU fence の signal |
| 登録済み queue timeline 1–63 の marker | その queue に関連付けた観測を再確認する機会 | 任意の未登録 timeline の進行条件、共有 payload の成功 signal |
| reply trailer と `VkResult` | 対応する native API 呼出の応答 | 非同期 GPU 処理の完了。ただし API 自身の定義する同期結果は別 |
| 実 native `vkGetFenceStatus` 等の成功 | 対象 GPU work の完了 | 別 generation、別 queue、別 allocation の完了 |

U の decoder transaction は通知後も shared reply trailer と応答を検証する。queue marker は実 native fence の状態観測を促すものであり、`drv_gpu_complete(..., 0)` が K fence payload を成功状態に変更することはない。transport error は関連する binding を error 終端できる。標準 external fence の成功 signal は U worker が実 native fence の完了を確認した後に行う。

参考実装: [transport.c](../../../src/drivers/gpu/venus/transport.c)、[GPU command UAPI](../../../include/uapi/gpu.h)、[context.c](../../../userland/base/libvulkan/context.c)、[sync.c](../../../userland/base/libvulkan/sync.c)。

## 待機と排他の範囲

| 経路 | 待つ内容 | 待機中の保持・解放 |
| --- | --- | --- |
| 同一 open の通常 ioctl admission | 他 thread の resource table 操作終了 | `gpu_registry_lock` を waitq が解放する。admission を得た 1 操作は backend と copyout を終えるまで session の busy reservation を保持する |
| `GPU_COMMAND_SUBMIT` | コピー・queue 受理まで | backend は controller mutex を取得して完了まで待たない。受理後は別 context の request と重なり得る |
| `GPU_COMMAND_WAIT` | 指定 sequence の transport 終端 | 通常 session admission を取得しない。registry lock と waitq で観測を接続し、sleep 中は lock を解放する |
| 従来の `GPU_COMMAND` と同期 control/resource 操作 | 対応 control response | 呼出元が取得した controller mutex は保持し得る。transport の queue spinlock は runtime sleep 中に解放する。全 control 操作が非同期化されたとは扱わない |
| `GPU_FENCE_WAIT`、command/display の prerequisite | 指定 payload generation の終端 | payload の waitq で待つ。prerequisite 待ちは controller mutex と通常 session admission の取得より前に行う |
| Venus display の nominal refresh | 次の guest tick 上の表示境界 | owner/lease/generation を保存して controller mutex を解放し、再取得後に transport failure・同じ所有権・console claim を再検査する |
| U の fence all/any wait | native fence 又は shared payload | native 状態を短い transaction で観測し、blocking wait/poll 中は device/queue mutex を保持しない。any wait は候補 notification を pin し、別 observer の CONSUME による通知消失を防ぐ |

別プロセスが独立 open すると別 session・Venus context を得る。dup/SCM_RIGHTS で共有した open の resource table は通常 admission によって直列化されるが、他 thread の競合を即座に `EBUSY` としない。自分自身の callback による再帰 admission は deadlock を避けるため `EBUSY` のままである。表示 plane の lease 排他と rendering session の admission は別の契約であり、同一 `VkQueue` に対する Vulkan の外部同期要件も変わらない。

`GPU_COMMAND_WAIT` の timeout 0 は状態確認、`UINT64_MAX` は割込み可能な継続待ち。有限 timeout や `EAGAIN` で pending record を破棄せず、CONSUME は terminal result の copyout が成功した後に行う。U は最大 63 件の optional queue notification を保持して decoder 用 record を残す。追加 marker の `EAGAIN`/`ENOMEM` を、すでに native が受理した `vkQueueSubmit` の失敗へ変換しない。

IRQ 化は「polling が完全にゼロ」という意味ではない。通常 runtime transport は waitq で sleep する一方、初期 idle/非 scheduler 経路には有限の polling が残る。U の通知非対応 backend、optional marker 飽和時の native 状態再確認、reply trailer 確認、console worker 等にも観測・有限待機がある。nominal refresh は guest の tick 精度に制約され、物理ホストの vblank 到達を保証しない。

参考実装: [gpu_session_enter / completion wait](../../../src/drivers/gpu/gpu.c)、[Venus callback](../../../src/drivers/gpu/venus/venus.c)、[display_next_refresh](../../../src/drivers/gpu/venus/display.c)。

## 参照を持つ fence fd

`KERNEL_HANDLE_FENCE` は GPU allocation fd と異なる typed payload であり、generic handle/fd と SCM_RIGHTS の参照移譲を使う。VFS の擬似 file を作らず、最後の fd・producer binding・実行中 lookup/wait の strong reference が消えてから payload を解放する。

- payload は generation 1 から始まり、`PENDING` / `SIGNALED` / `ERROR` を共有する。QUERY は状態を消費しない。terminal 状態は `POLLIN`、error はさらに `POLLERR` を返す。
- RESET は exact generation を要求し、未完了 producer が所有中なら `EBUSY`、古い generation は `ESTALE`、世代の周回は `EOVERFLOW`。成功すると同じ payload の次 generation が pending となり、既存 fd alias からもその状態を観測できる。
- BIND は GPU open に producer 権限を予約する。sequence 0 は native submit 前の予約で、未受理が確定した場合の `GPU_FENCE_BIND_RELEASE` は generation と pending 状態を変更せず解除する。受理後の失敗を通常 rollback にすり替えない。
- SIGNAL は同じ producer と exact generation を要求する。import しただけの alias は別 open の作業を成功 signal できない。同一 GPU identity の確認も K が行う。native display prerequisite の許可された cross-device 経路は別途検証する。
- 最後の producer GPU open が閉じると、未完了 binding は `ENODEV` による error 終端となる。importer の alias が producer の U worker を延命したり、その native Vulkan object を復活させる契約ではない。terminal payload の metadata 参照は producer 終了後も有効である。
- command/display の同期 wrapper は待機開始前に signal fd の strong reference を取得する。prerequisite の待機中に別 thread がその fd を close し、同じ番号を再利用しても、後から別 payload を signal しない。wait 側も lookup で保持した payload を待つ。

`GPU_COMMAND_SUBMIT_SYNC` は prerequisite 成功後に受理し、signal target を producer binding として保持する。transport の正常完了だけでは signal しない。`GPU_DISPLAY_PRESENT_SYNC` は prerequisite 成功前に backend present を呼ばず、同期 scanout selection の結果を別の completion payload に通知する。表示選択完了と、前面 allocation を将来の frame が置換する寿命条件は区別する。

最終 file close の入口で、共有 fence の未完了 binding を error 終端してから native command drain に進む。これにより、失われた producer の通知を、10 秒まで待ち得る transport retirement の後ろに置かない。binding の削除は registry lock で late callback と直列化し、callback が利用する session/completion 配列、resource、DMA は drain 完了まで従来どおり保持する。late transport success は binding を見つけず、すでに通知した error を成功に戻さない。

これは `close(fd)` の呼出開始や `_exit` の開始から一定時間以内の通知を保証するものではない。alias・mmap・実行中 ioctl 等が file reference を保持している間は final close に到達せず、producer authority もその寿命に従う。process exit は他 thread の SIGKILL/退役と descriptor cleanup を経る。final close 前の遅延と、final close 内の drain 順序は別の問題として評価する。

標準 `VK_KHR_external_fence_fd` の OPAQUE_FD はこの guest 内の参照共有を実装する。Linux `sync_file` 互換や host Vulkan の OPAQUE fence fd の直接 import を主張するものではない。U の native fence/worker と K payload を接続しており、temporary import の復帰等は [U 検証記録](userland-final-verification.md) を参照する。

参考実装: [fence.c](../../../src/kern/fence.c)、[fence UAPI](../../../include/uapi/gpu-fence.h)、[generic handle](../../../include/kern/handle.h)、[external-fence.c](../../../userland/base/libvulkan/external-fence.c)。

## Display topology 通知基盤

`GPU_CAP_DISPLAY_EVENTS` を広告する backend では `GPU_DISPLAY_EVENTS` と GPU fd の `POLLPRI` が利用できる。transport の sequence は 1 から始まり、新規 open は inventory 未確認として ready になる。sequence はイベントの個数や display の mode generation ではなく、一覧を再確認すべきことを示す。イベントは併合・重複通知され得る。

利用手順は `QUERY(S)` → `GPU_DISPLAY_QUERY` で count/各 output を再列挙 → `ACK(S)`。QUERY と ACK は通常 session admission を取得せず、QUERY 自体は readiness を消費しない。copyout 失敗は observed/acknowledged cursor を進めない。ACK が認められるのは、この open に正常に返した generation までである。照会中の新しい event は古い S の ACK では消えず、`POLLPRI` が残る。cursor は open description に属するので、独立 open は独立に確認し、dup/SCM_RIGHTS は同じ確認状態を共有する。

Venus は INTx の read-to-clear ISR config bit、又は共有 MSI-X vector の `events_read` から通知する。display query が遅延 IRQ より先に device event を clear するときも、clear 前に software sequence を公開する。更新は queue lock 下、`poll_notify()` は解放後。sequence の飽和は sticky error とし、確認済みの番号を再利用しない。command 完了用 `POLLIN` と inventory 用 `POLLPRI` は別の readiness である。

K/transport fixture に続き、実 [q310-wayland-007](../temp/remote/q310-wayland-007/evidence/result.json) でも初期 inventory QUERY/ACK/POLLPRI が PASS。[gpu-admission-test](../../../userland/base/tests/gpu-admission/main.c) が sequence=1、outputs=1、attempts=1、initial/query_preserved/acknowledged=1 を確認した。capability がある場合のみ実 inventory 列挙を挟み、最大 8 回で終える。実際のケーブル抜挿、QEMU monitor hotplug、DE の自動再配置を検証済みとはしない。

## Timeout と新規 open による回復

未完了 transport request は 10 秒の watchdog で失敗し、device の新規送信を停止する。他の retained request、completion/fence waiter、poll observer に terminal error を公開する。device がまだ参照する可能性がある descriptor/command/response DMA と resource backing は quarantine として保持し、単なる timeout や producer close では再利用しない。

回復を開始できるのは、登録 GPU が online で、回復を試みる新規 open の reservation 以外に session がなく、旧 `open_sessions` が空、共有 allocation hold がゼロの場合である。device mmap は元 file を保持するので、fd を閉じても mapping が残れば session retirement を満たさない。exported allocation と import/scanout が保持する共有 allocation 参照も gate に含む。実行中 callback は file/session の寿命で保護される。すでに error 終端した fence の metadata alias まで破棄する条件ではない。

新規 open は `recovering` で他の新規 open を排除し、controller mutex の外で console worker の停止・join を行う。transport 停止では worker/IRQ の実行を終え、checked device reset を確認してから旧 DMA と quarantine resource を退役させる。display metadata を片付け、新しい transport を初期化できた場合だけ device error を解除する。次の open は新しい renderer context を作る。旧 `VkDevice` や失敗した submit が復活することはない。途中の stop/reset が失敗した場合、所有権を残して失敗を返す。

## 実 QEMU 証拠

いずれも既存 private host、QEMU 10.0.11、Intel ANV を使った有限試験である。006 と fault 試験の isolated renderer pair は [host 差分の記録](renderer-opaque/README.md) を参照する。

| Attempt | 確認結果と範囲 | 保存証拠 |
| --- | --- | --- |
| `q310-wayland-005` | 同じ GPU fd を 4 pthread が U 側 ioctl 直列化なしで操作し、32 回ずつ合計 128 回の CREATE/WRITE/READ 全 4096 byte 比較/DESTROY が PASS。attempt 全体は別の optimal external-memory profile で FAIL しており、全体成功とはしない | [観測ログ](../temp/remote/q310-wayland-005/evidence/wayland-observed.log)、[result](../temp/remote/q310-wayland-005/evidence/result.json) |
| `q310-wayland-006` | 上記 admission 128 回、標準 external fence、producer exit 後の共有 allocation import、Wayland 12 frame oracle/lifecycle が PASS。46.651 秒、QEMU exit 0。複数 VkQueue の無制限負荷試験を意味しない | [観測ログ](../temp/remote/q310-wayland-006/evidence/wayland-observed.log)、[result](../temp/remote/q310-wayland-006/evidence/result.json) |
| `q310-wayland-007` | 初期 display inventory の QUERY/ACK/POLLPRI が `sequence=1 outputs=1 attempts=1`、`initial/query_preserved/acknowledged` は各 1。続く admission 128 回も PASS。実 hotplug の受入ではない | [観測ログ](../temp/remote/q310-wayland-007/evidence/wayland-observed.log)、[result](../temp/remote/q310-wayland-007/evidence/result.json) |
| `q310-fence-exit-001` | `/bin/gpu-fence-test --producer-exit` による `GPUFENCE PRODUCER_EXIT_ERROR PASS`。共有された未完了 payload が producer 終了により error となるケース。15.076 秒、QEMU exit 0 | [観測ログ](../temp/remote/q310-fence-exit-001/evidence/wayland-observed.log)、[result](../temp/remote/q310-fence-exit-001/evidence/result.json) |
| `q310-fence-exit-002` | 後続の再実行は FAIL。consumer の `vkWaitForFences` が期待する DEVICE_LOST=-4 に対し VK_TIMEOUT=2 を返した。従来の drain 後 fence retirement と同じ 10 秒の consumer deadline が競合し得ることを検出し、最終 close の順序を修正。001 の成功でこの失敗を取り消さない | [観測ログ](../temp/remote/q310-fence-exit-002/evidence/wayland-observed.log)、[result](../temp/remote/q310-fence-exit-002/evidence/result.json) |
| `q310-recovery-002` | 明示 `--isolated` と READY/RESUME handshake。harness 所有 VM の renderer 子プロセスのみを一時 SIGSTOP し、実 decoder request を未完了にする。`elapsed_ms=10000`、guest ETIMEDOUT=42。旧 peer の error と旧参照を残した新規 open の拒否を確認。全旧 session 退役後に renderer を SIGCONT し、新規 transport/context・4096 byte roundtrip・decoder 完了が PASS。13.909 秒、QEMU exit 0 | [観測ログ](../temp/remote/q310-recovery-002/evidence/wayland-observed.log)、[result と対象 PID/実行条件](../temp/remote/q310-recovery-002/evidence/result.json) |

recovery002 のログには失敗前 `submitted=9 completed=8 interrupts=6 sleeps=5`、回復後 `submitted=12 completed=12 interrupts=12 sleeps=11` がある。これは実 IRQ と waitq sleep の稼働の証拠であり、CPU 使用率測定や全待機の polling 消滅の証拠ではない。実回復 probe が直接試した retirement gate は旧 peer session であり、mmap/export を保持したあらゆる組合せの実 VM 試験を済ませたとはしない。

先行 [q310-recovery-001](../temp/remote/q310-recovery-001/evidence/result.json) は未登録 timeline 1 が直ちに status 0 で返ったため FAIL。未登録 timeline で必ず host timeout を発生させられるという仮説は棄却した。002 はその入力を成功扱いへ変更せず、専用 VM の renderer 停止という別の確実な fault stimulus を使用した。

## 限定 fixture と限界

| Fixture | 実 production 境界で確認した内容 | 結果・証拠 |
| --- | --- | --- |
| [run-venus-transport-test.sh](../tests/run-venus-transport-test.sh) | 実 avail/used descriptor、out-of-order context、fence/ring echo、一度だけの callback、3 marker＋予約 decoder、runtime waitq、queue wrap、遅い/停止 clock、reset 不成立時の DMA 保持。追加で INTx/MSI-X config-only、queue 完了との共存、sequence 飽和、lock 外 poll wake | ordinary＋ASan/UBSan PASS、`/tmp/q310-transport-topology.log` |
| [run-kernel-fence-test.sh](../tests/run-kernel-fence-test.sh)、[run-gpu-fence-test.sh](../tests/run-gpu-fence-test.sh) | 実 handle/fd/SCM_RIGHTS/poll と payload generation、producer 権限、copyout rollback、GPU completion/binding、terminal failure | ordinary＋ASan/UBSan PASS。入力は各 fixture source に固定 |
| [run-gpu-fence-close-test.sh](../tests/run-gpu-fence-close-test.sh) | 実 GPU/fd/fence core。追加 file reference が残る間は pending、最終 close の drain 入口で error、resource/session/completion 存命、late success が error を覆さない、resource 退役は drain 後 | 修正前は drain 入口の error assertion で FAIL（`/tmp/q310-fence-close-before.log`）。修正後 ordinary＋ASan/UBSan PASS（`/tmp/q310-fence-close-after.log`）、既存 GPU fence suite も PASS（`/tmp/q310-fence-close-existing.log`） |
| [run-gpu-fence-reuse-test.sh](../tests/run-gpu-fence-reuse-test.sh) | 実 prerequisite wait 中の signal fd close/reuse で元 capability を保持 | ordinary＋ASan/UBSan PASS、`/tmp/q310-fence-reuse-after.log` |
| [run-gpu-topology-test.sh](../tests/run-gpu-topology-test.sh) | 実 GPU/fd core の exact ACK、copyout rollback、照会との event race、独立 open と alias、admission bypass、command readiness 共存、offline/overflow | ordinary＋ASan/UBSan PASS、`/tmp/q310-gpu-topology.log` |
| [表示・WSI 検証](display-wsi.md) | refresh sleep 中の controller mutex 解放と lease 変更拒否、prerequisite error 時に present 未実行、成功時の別 signal payload | 同資料の有限 fixture 結果を参照 |
| [U 最終検証](userland-final-verification.md) | native 結果の再確認、exact/all/any wait、record pin と saturation fallback、external fence import/reset/worker 所有権 | 同資料の command、hash、ABI/DSO 検証結果を参照 |

transport fixture の PCI/MMIO・時刻・scheduler peer は決定的なモデルで、worker 作成/join 境界を模擬し、実 watchdog kernel thread の scheduler 負荷を再現しない。IRQ/descriptor の意味 fixture と実 recovery002 を合わせて評価する。実 native renderer の全 Vulkan 動作、実機 MSI-X/INTx 全構成、長時間 stress、任意 GPU の reset を保証するものではない。ここに記録した基盤のみで一般 Wayland 環境や DE の hotplug 対応が完成したとは扱わない。


## q310 最終実受入

同一最終kernelのwayland-008、direct-004、fence-exit-003、recovery-004は全てPASS。producer終了の実waitは10msでDEVICE_LOST、recoveryは10000ms timeout後に新contextを確認。直接表示全runのQEMU traceで320×240 BLOB要求107回・同寸法legacy要求0回と実画面を照合した。source/artifact/renderer hashと全失敗履歴は[最終結果](results.md)および[verification](final-verification.json)に対応する。旧attemptの数値・fixture限界は履歴として保持する。
