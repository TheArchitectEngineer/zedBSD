# p008: GPU job の完了責任と Venus transport

native submission の前に K が descriptor・completion record・fence generation を予約し、U が停止しても K の独立 watchdog が終端する。Venus の成功は paired renderer が**当該 submission の実 native VkFence**を確認した結果であり、CPU0 の decoder 完了や単なる送信受理を成功証明にしない。

## 受理・完了・失敗の契約

| 境界 | 所有と保証 |
| --- | --- |
| `GPU_JOB_RESERVE` / `drv_gpu_job_ops.reserve` | native work より前に callback と slot を確保する。context nonzero、timeline 1–63、exact strict profile が必要。容量不足は EAGAIN。予約時から10秒の監督を開始する |
| native QueueSubmit / QueueBindSparse | U が成功した VkResult を確認する。fence=NULL でも U の queue cache が実 native fence を付ける |
| `GPU_JOB_COMMIT` | token と expected completion が同じ RESERVED slot を指すことを検査し、追加allocation・容量待ちなしで marker を公開する。予約からの期限を延長しない |
| marker の正常 used completion | host が実 VkFence の成功を確認し、最後の借用 access を終えた後だけ発生する。common GPU が対応する generation を signal する |
| `GPU_JOB_CANCEL`, flags=0 | 確実に native 未受理の RESERVED だけを返却し、callback を作らない。POSTED の取消し・古いtokenは ESTALE |
| `GPU_JOB_CANCEL_FAULT` | RESERVED/POSTED のどちらも未知の native work がありうる。transport を失敗させ ERROR を通知し、DMAをquarantineする |
| 自律期限 | RESERVED も POSTED も監督し、U のwait・commit・CPU使用を必要とせず ERROR にする。予約後native成功前後で停止する隙間を残さない |
| teardown | 成功しないDMAはdevice resetの確認まで保持する。worker停止・IRQ drain・resetの既存順序を維持する |

予約は未commitの間、GPU依存の実行根拠にはならない。共通GPU側のSYNC admissionは、COMMITTED jobかterminal generationを要求する。予約期限を単にsessionの取消しにしないのは、未公開markerとは別にnative GPU workが既に動いている可能性があるためである。

CPU0 markerはこれまでどおり decoder reply と trailer の可視化境界であり、GPU queue の実行成功とは区別する。reply store・CPU0通知後のtrailer検査はUに残す。

## strict host profile

[renderer-strict/README.md](renderer-strict/README.md) に isolated pair、patch、license、実buildと意味fixtureを保存した。capsetは exact168B、offset160 magic=`0x5a424453`、offset164 flags=`3`。同じ値のproxy/server INIT ACKを経た場合だけ広告する。flags1のp007 pair、stock160B、未知length/magic/flagsをstrictと推測しない。

既存QEMUのcallbackにstatusを追加する変更はしていない。native DEVICE_LOST・wait失敗・未確認teardownはhost側でstickyにし、非zero timelineの正常retireを抑止する。proxy断でもpending fenceをforce-retireしない。Kは返信エラーまたは自律10秒期限によりERRORを得る。host固有VkResultの数値をKへ保存するsidebandは今回の契約ではない。

新libvulkanのdevice作成にはstrict profileとGPU_CAP_JOBを必要とする。旧hostとのVulkan動作互換は維持しないが、kernelのlegacy 2D・能力照会は残す。profile拒否とdevice初期化診断はU側資料も参照する。

同queueではhostがsync listをFIFOで処理し、先行実fenceが成功するまで後続の成功watermarkを出さない。従って、全submissionが必須markerへ結び付く今回の契約では、idleの最後のmarkerは先行BindSparseも覆う。Vulkanのempty QueueSubmitそのものにその保証があるとは主張しない。実host fixtureはpending sparse→後続native fenceがreadyでもcallbackが追い越さないことを検査する。公式定義は [vkQueueWaitIdle](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueWaitIdle.html) と [vkQueueBindSparse](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueBindSparse.html) を参照。

## queue容量とメモリ

| deviceの最大descriptor数 | 選択descriptor数 | DMA chain数 | GPU job上限 | 制御予約 |
| --- | --- | --- | --- | --- |
| 8 | 8 | 4 | 3 | 1 |
| 48 | 32 | 16 | 14 | 2 |
| 64以上 | 64 | 32 | 28 | 4 |

最大64を上限にadvertised maximum以下の最大2のべき乗を選ぶ。8未満は既存同様未対応。各chainはreadable requestとwritable responseの2descriptor。制御容量はchain数の1/8、最小1。GPU jobが満杯でもordinary/CPU0が進められる。32chainのrequest/response要求payloadは `4096 + 32 × (65568 + 4096) = 2233344 bytes`。4KiB pageで個別丸めした最低backingは2363392 bytesであり、allocator metadata等は別である。この値は配置からの計算であり実機メモリ使用量測定ではない。理由なく128descriptorへ増量しない。

ringのavailable offset1024、used offset1280、全体4096B。64descriptorと両ringは重ならず、実際のindexは選択queue_sizeで剰余を取る。固定max32の配列はpartial attachのcleanupに使用し、active traversalは実slot_countで制限する。

## 故障通知とwrapper寿命

transportはpublished GPU wrapperをretainする。交換はqueue_lock下、旧refのreleaseはlock外。故障処理は同lock下で独立snapshot refを取り、lock外で `drv_gpu_report_error` とreleaseを行う。pending callbackが0件でも故障は可視化し、故障とunpublishの競合やregistration前の故障を失わない。controller mutexは取得しないため、display側からの明示故障報告も同じ契約を使える。

## 検証

[transport-evidence/verification.json](transport-evidence/verification.json) と [fixture.log](transport-evidence/fixture.log) が対象source hash、commandと結果を保存する。

- target Clang syntax（amd64 freestanding、warnings-as-errors）PASS。
- `timeout 120 sh plan/ws014/tests/run-venus-transport-test.sh` の通常・ASan/UBSan PASS。
- exact cap拒否、queue縮小、28予約＋4CPU0、commitのallocation増加なし、32件逆順IRQ・16-bit wrap・exact callback、rollbackとposted fault、予約後U進捗なしのwatchdogを実transportで検査。
- callbackゼロの故障、故障通知中のunpublish、故障後のpublishをfixture所有refで検査。malformed response・reset quarantine・topology INTx/MSI-X・waitの既存検証も保持。
- host側の実queue/proxy source fixtureと独立socket・scripted native calls、通常/ASan/UBSan、実server INIT5caseは別証拠。

実QEMUのdirect/Wayland/producer-stop/producer-exit/recovery受入はroot担当の同Phase結果と実run evidenceを正本とする。本資料はPhase状態やGitHub同期を変更しない。実 sparse-capable GPUの受入、CTS、任意host revisionの互換性は主張しない。HAL・system package・git add/commit/pushへの変更はない。
