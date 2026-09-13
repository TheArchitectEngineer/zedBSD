# GPUスタック p008 レビューへの対応方針

- 作成日: 2026-09-13
- 対象: `gpu-stack-review4.md`、zedBSD `cca124456908acdb39512bc5d339e1a6f0e61a7e`
- レビュー原本 SHA256: `6bd6ddf18dc4c1ec6d713f2451afbf6f89bc5bc7c6847b55ea025872f628fa42`
- 照合資料: [p008結果](phase008/results.md)、[GPU JOB契約](phase008/gpu-job-contract.md)、[strict renderer](phase008/renderer-strict/README.md)、固定virglrenderer 1.1.0とQEMU 10.0系のソース、下記の公式仕様。

本書は対応案である。現コードの読解と仕様照合を行い、新しいコード変更・実行試験は行っていない。p008/q311の過去の受入は保持し、新Phase・Queueの作成や実行はまだ行わない。KはGPUドライバ側を含むカーネル空間を指し、fenceを汎用kernへ戻す意味ではない。

方針の中心は、**一時的な混雑をOOMにせず、待機が他の進行を止めないようにすることと、GPUジョブの異常を可能な限りそのcontextに限定すること**である。stock hostへの対応も目指すが、既存の拒否判定を削除するだけの互換化は採用しない。

## 回答一覧

| 項目 | 判断 | 提案する対応 |
| --- | --- | --- |
| R1: slot枯渇がsubmitのOOMになる | 最優先で修正 | `EAGAIN`と実メモリ不足を分離し、libvulkanの完了回収と容量待機を追加する。待機中は進行に必要なmutexを保持しない。単純なK待機・POLLIN再試行にはしない。 |
| R2: strict host必須 | 配布上の制約を認め、互換性を別途実証 | 当面の既定はstrictを維持する。stockで保証できる能力と退役条件を検証し、成立した範囲を互換profileとして有効にする。`SIGNALED`の一括再定義と全機能の無条件有効化はしない。 |
| R3: 10秒で全deviceを故障にする | 障害範囲と期限を改善 | 予約・control・GPU実行の期限を分離する。context失敗通知と資源の安全な停止・回収を分け、停止確認を含むbackend契約を作る。確認不能なら全体resetへの退避を残す。 |
| R4: direct acquireも10ms起床 | 改善 | 画像release、GPU故障、表示変更を同じ待機へ届ける。通知の取りこぼしを解消したうえで、direct経路の定期起床を除く。 |
| R5: fenceなしsubmitのreset往復 | 条件付きで最適化 | 完了したprivate fenceをまとめてresetし、reset済みの再利用リストへ置く。まず専用負荷で実コストを測る。既存CPU測定との因果は主張しない。 |
| R6: pending fenceのreset/destroy待機 | 制約を記録 | valid usageを満たす通常経路を優先する。異常・終了経路はR3の有限な終端へ接続し、未完了資源の強制解放はしない。 |

## R1: 容量待機と完了回収

症状はコードから確認できる。[sync.c](../../userland/base/libvulkan/sync.c)の`vulkan_sync_job_reserve`は`EAGAIN`と`ENOMEM`を同じ`VK_ERROR_OUT_OF_DEVICE_MEMORY`へ変換する。native submit前の拒否なので、直ちにVulkanの失敗時契約に反するとは断定しないが、通常の短時間の混雑でアプリを失敗させる実装は改善する。VulkanはsubmitのOOM時に資源・同期状態を変更しないことを要求し、それを保証できない失敗にはDEVICE_LOSTを要求する。[vkQueueSubmit](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueSubmit.html)

レビューの修正案には次の補足が必要である。

| 容量・通知 | 現在の性質 | 単純に待つ場合の問題 |
| --- | --- | --- |
| Venusの最大28 job slot | device全体で共有。通常control用に別の4枠を残す | 別openが全枠を使用している場合、自分のfdのPOLLINでは空きを観測できない。 |
| openあたり64 completion record | GPU完了に加えてCONSUMEとobserver参照の退役が必要 | 完了だけでは空かない。回収側も必要とするcontext mutexを持ってK内で待つと、回収を止める。 |
| GPU fdのPOLLIN | そのopenの未CONSUME終端recordを示す | device全体の空き通知ではない。参照保持中のrecordでは、readinessが続いて再試行が空回りする場合もある。 |
| 完了とslot解放の順序 | `drv_gpu_complete`の通知がtransport slotのFREEより先 | 完了通知で再試行してもまだ空いていない場合がある。実際の容量解放時にも通知が必要。 |

根拠は[GPU共通層](../../src/drivers/gpu/gpu.c)の`gpu_poll`、`gpu_completion_reserve`、`gpu_completion_wait`とobserver退役、[transport](../../src/drivers/gpu/venus/transport.c)のjob予約・完了回収、[queue](../../userland/base/libvulkan/queue.c)と[sync](../../userland/base/libvulkan/sync.c)の排他である。

対応は次の順とする。

1. native未受理の容量不足を内部の再試行結果として保つ。`ENOMEM`、native受理後の失敗、device lossは待ち直しで隠さない。
2. admissionを、状態を確定する短い区間と容量を待つ区間へ分ける。待機時にはcontext/device mutex、進行を妨げるqueue mutexを外し、必要なobject参照とqueue順序は別途保持する。再取得後はerror・世代・同期状態を再検査する。WSIの既存locked呼出しも同じ規約に直す。
3. Uの完了回収を進めてから、Kの容量変化を待つ。専用の容量待機ioctlを第一候補とし、`EAGAIN`を返す既存RESERVEは非待機操作として保持する。名前とlayoutは実装設計で決め、ここでは既存ABIを変更済みとしない。
4. 待機条件はdeviceのjob枠とopenの記録枠の両方を扱う。実際のslot解放・取消・最後のobserver退役・session終了・故障で起こす。条件検査からsleepまでの通知取りこぼし、別processによる空き獲得、公平性を扱う。
5. 待機はinterruptibleとし、終了・故障で解除できるようにする。容量待機の時間と、受理済みGPUジョブの監督期限を混同しない。単なる混雑を全GPU故障へ変換しない。

GPU処理の完了だけではUの記録枠が空かないため、正常な仕事でも回収側を止めないことが必要である。一方、binary semaphoreとeventの有効な使い方は、後続アプリ操作なしで進行できることを要求する。未設定eventを待つsubmitの後にhostから設定する例は、現在の仕様では正常負荷の進行保証に使わない。故障注入とは区別する。将来timeline semaphoreを追加するときはwait-before-signalの進行を別に設計する。[Queue Forward Progress](https://docs.vulkan.org/spec/latest/chapters/cmdbuffers.html#commandbuffers-submission-progress)、[vkSetEvent](https://docs.vulkan.org/refpages/latest/refpages/source/vkSetEvent.html)

現Vulkan 1.0範囲では二段階admissionと容量通知を主案とし、無制限のsoftware queueは追加しない。watchdogによるDEVICE_LOSTを、通常負荷の進行成功として数えない。

検証は、明示fenceあり・なしの連続submit、28超の通常進行負荷、64枠の終端未回収・observer保持、別processが全枠を使う場合、待機中の別threadの完了回収・他queue処理、close・割り込み・故障を含める。実メモリ不足ではnative未受理と同期状態の不変を確認する。複数queue/zwlとクライアントの同時進行で、枯渇によるOOM・取りこぼし・busy loopがないことを受入条件にする。

30回submitするだけでは途中の完了で枠が空く可能性があるため、再現fixtureでは占有数と解放時点を観測する。GPU完了済みでcommon記録だけが埋まる場合にはjob watchdogが救済しないことも、回収試験で確認する。

## R2: stock互換とfenceの意味

独自rendererのビルドが必要という代償はそのとおりであり、QEMU本体が無変更でも利用者の負担は残る。一方、固定1.1.0を調査した結論を、すべての将来のディストリビューション版に一般化しない。対応version・capability・実際の検証結果を明示する。

まず説明を補正する。**Vulkanはdevice loss時にfence待機がSUCCESSを返すことを許している。** `vkWaitForFences`と`vkGetFenceStatus`の仕様から、失敗を含むretirementを通知する設計それ自体をVulkan違反とはいえない。strictの意味も「実submissionのnative fenceに対する成功応答を根拠にする」であり、描画内容が常に正しいという保証ではない。[vkWaitForFences](https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitForFences.html)、[vkGetFenceStatus](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetFenceStatus.html)

ただし、現在のstock markerをすべて「GPUが資源を使い終わった」と扱う根拠は不足している。

- 固定1.1.0の`vkr_queue.c`は、対象submitの実fenceを借りず、別のempty submitでmarker fenceを作る。ordinary submitとBindSparseを同じ保証と推定しない。
- 同ファイルのwait処理はTIMEOUT以外をretireする。DEVICE_LOSTだけでなくOOM等も含み、待機APIの失敗だけではGPU使用終了を証明できない。
- `proxy_context.c`には切断時のforce-retireがある。通知と実GPU停止・資源寿命の順序を確認する必要がある。
- 現在のUAPI/consumerは、KのSIGNALEDを依存解決や画像再利用に使う。別processのconsumerにproducerの「次のAPIのVkResult」が届く保証はない。「壊れた画像は最大1枚」という上限も現実装からは導けない。

根拠: [固定virglrendererのqueue](https://gitlab.freedesktop.org/virgl/virglrenderer/-/blob/1.1.0/src/venus/vkr_queue.c)、[proxy](https://gitlab.freedesktop.org/virgl/virglrenderer/-/blob/1.1.0/src/proxy/proxy_context.c)。ローカルの同版ソースとも照合した。

Linuxのsyncobjとの比較も、UAPIの見え方と内部状態を分ける。`drm_syncobj`は`dma_fence`を包み、後者は成功・エラーを保持できる。syncobjの待機結果がエラー理由を示さないことは、ドライバ内部の停止・寿命管理を不要にする根拠にはならない。[DRM Memory Management](https://docs.kernel.org/gpu/drm-mm.html)、[dma_fenceの状態](https://docs.kernel.org/driver-api/dma-buf.html)

提案は、**strictを当面の既定として保持し、stock互換は能力別の実証を経て追加する**ことである。

| 能力・契約 | 互換化の条件 |
| --- | --- |
| 普通の描画と直接/Wayland WSI | queue順序、marker、host異常、画像再利用をstock構成で照合し、通常BLOB経路を通す。 |
| 外部fence | 終端通知、成否が不明な状態、資源を再利用できる状態を区別する。既存SIGNALED/ERRORを能力判定なしに別の意味へ変更しない。Kだけで保証できない場合は広告を制限するか、別の完了証明方式が必要。 |
| sparse | 実BindSparseの完了を保証できる場合のみ広告する。empty markerだけでの保証を前提にしない。 |
| OPAQUE memory共有 | p007由来のOPAQUE対応とp008のSTRICT_QUEUEを独立に扱う。strictを外してもstock proxyのoptimal OPAQUE対応不足は解消しない。 |

変更箇所は`device_validate`と`job_reserve`だけではない。[venus.c](../../src/drivers/gpu/venus/venus.c)のGPU_CAP_JOB広告、capset/INIT、[external-fence](../../userland/base/libvulkan/external-fence.c)、共有・WSI・能力列挙まで含める。未対応拡張を非広告にする場合も、標準アプリに必要なcore機能やWSIが成立するかを再評価する。

strictをoptionalにすることは到達候補であり、この読解だけでstock互換の安全性や実装規模を確定しない。検証で契約が成立しなければstrictを維持し、不足するhost/guest機能と配布負担を示す。標準ホスト対応を優先して既存の外部同期契約を変更するかは、その具体案で判断できるようにする。

## R3: ジョブ期限とcontext単位の障害処理

全submitを監督対象にしたことで、10秒期限の影響が広がった指摘は正しい。p008のproducer-stop試験は、依存consumerが有限時間で解放されることを示したが、独立した他sessionが動き続ける証拠ではない。

期限は次のように分ける案とする。値はVulkan仕様の要求ではなく、zedBSDの運用既定値の提案である。

| 対象 | 既定案 | 期限超過の扱い |
| --- | --- | --- |
| RESERVE→COMMIT/CANCEL | 10秒 | 予約ownerを失敗へ進める。RESERVEDでもnative受理済みの可能性があるため、未投入として即解放しない。 |
| control/decoderの無応答 | 10秒 | 応答・世代・他contextの進捗を検査する。単一decoder停止はcontext回復、共通control/transport停止は全体故障とchecked resetへ進める。 |
| COMMIT済みGPU実行 | 60秒、設定可能 | まずそのcontextをlostへ移し、停止処理を開始する。正当な長時間computeも期限対象になることを明示する。 |
| context停止確認 | 独立した有限期限 | 停止・退役を確認できなければ資源を保持し、全体resetへ移る。 |

GPU実行期限はCOMMIT時からの単調時刻で管理し、controlが動いただけで延長しない。設定はまず既存のdriver/boot設定へ置き、必要なら管理者向けruntime設定を追加する。実効値を問い合わせ可能にし、非特権clientが任意に無期限化する方式にしない。`gpu_info`の既存layoutへ無断でfieldを足さず、設定・照会が必要な場合は版付きの追加契約にする。

レビューの「CTX_DESTROYを送れば他sessionを継続できる」は、現契約では不足する。QEMUの`virgl_renderer_context_destroy`呼出し後のOKは、proxy/serverとGPUの停止ACKではない。固定版proxyはdestroyをsendするだけでreplyを待たず、process workerの終了要求もreapを待たない。thread workerではjoinが長時間停止し得る。pending markerもQEMUのfence queueに残り、context破棄だけでvirtqueue descriptorを回収できるとは限らない。

根拠: [QEMUのvirgl処理](https://gitlab.com/qemu-project/qemu/-/blob/v10.0.0/hw/display/virtio-gpu-virgl.c)、[proxy client](https://gitlab.freedesktop.org/virgl/virglrenderer/-/blob/1.1.0/src/proxy/proxy_client.c)、[render worker](https://gitlab.freedesktop.org/virgl/virglrenderer/-/blob/1.1.0/server/render_worker.c)。固定ローカルソースで送信・応答と終了の順序を読んだ結果であり、今回停止試験を実行した結果ではない。

従ってcontext単位の改善は、以下をまとめた変更になる。

1. GPU共通層にsession単位のsticky errorとfence・待機者通知を用意する。device全体のerrorと分離する。
2. backendがそのcontextへの新規submitを止める。既存jobをerror終端し、依存先は失敗として解除する。ただしerror通知をDMA資源の解放許可にしない。
3. hostでの仕事の停止、pending descriptorの確定退役、callback終了を確認する。通常control処理を同期destroy待ちで止めず、有限の非同期停止として設計する。必要ならrenderer/QEMU側の契約追加も変更範囲として示す。
4. 他contextやscanoutが保持する共有allocationの参照を維持し、停止が確認できた資源だけを回収する。共有画像の内容・layoutの未定義化や再描画要求と、別deviceのobject/handleの寿命を区別し、無関係なsessionを故障扱いにしない。
5. 停止不能・不正な応答・本当のtransport停止では既存のquarantineと全体resetを維持する。local resetを名乗るために停止確認を省かない。

control/decoderの進捗は故障範囲を推定する材料になるが、GPU処理の進捗やDMA停止を証明しない。Vulkanもlogical deviceの喪失とphysical deviceの喪失を区別し、喪失deviceと共有するmemoryの内容が未定義になる場合を規定している。[Lost Device](https://docs.vulkan.org/spec/latest/chapters/devsandqueues.html#devsandqueues-lost-device)

検証の中心は、Aの長いjob中にBが描画・表示を継続できること、Aの期限超過後もBが継続すること、共通server/controlの進捗が失われれば全体故障へ進むことである。個別context workerの停止はcontext回復の対象として区別する。10秒を超えて正常終了するjob、予約後のSIGSTOP、COMMIT後のSIGSTOP、共有資源・late callback・終了確認失敗も含める。単に60秒へ延長しただけでは障害分離の完了にしない。

## R4: direct acquireを通知で起こす

direct displayの[platform定義](../../userland/base/libvulkan/wsi-display.c)は実際に`progress == NULL`であり、Wayland dispatchのための10ms起床は不要である。ただし現在はKの非同期故障・topologyを観測する機会もその周期に頼っており、単純に残り時間全体の条件変数待機へ変更すると、故障を見逃す場合がある。

[Acquire](../../userland/base/libvulkan/wsi-swapchain.c)ではerror検査がswapchain mutexの外にもある。errorのstoreとbroadcastだけを追加しても、検査とsleepの間に通知が入る取りこぼしを防げない。

追加のfence監視threadを作らず、**Acquireするthread自身が、GPU/表示fdの故障・topology通知と、画像releaseの起床fdを待つ**案を第一候補とする。既存の画像状態はmutexで保護し、wait登録・状態再検査・世代確認の順序を明示する。複数waiterの起床を一人が消費してしまわない仕組み、close/reuse、別render/display nodeにも対応する。既存のnonblocking pipeを待機呼出しごとに登録する方式を候補とし、通知時のlock逆転も検査する。fdイベントを使うためにGPU固有の新しい汎用kern機能は追加しない。

GPU fdは原則として故障、表示fdは故障とPOLLPRIを観測し、画像の完了処理は既存present workerへ残す。無関係な未回収completionのPOLLINを常時購読してbusy loopを作らない。表示変更は既存のQUERY→列挙→exact ACKで処理する。

全error公開経路を同じ通知へ接続し、timeout=0は即時判定、有限timeoutはmonotonicな絶対期限、無期限はrelease・error・切断で起きるようにする。この接続ができる前に10msのhealth確認を削除しない。Waylandの10ms dispatchは今回の低優先指摘に必要な範囲では維持可能であり、一般Waylandイベントループへの拡張は含めない。

検証は通常release、通知と待機開始の競合、複数waiter、contextだけのerror、Kだけのfault、表示切断、呼出しtimeoutを含める。idle direct経路の周期起床数が減り、故障通知の遅延が増えないことを測る。

## R5: private fenceの一括resetと測定

[queue_private_fence](../../userland/base/libvulkan/queue.c)は再利用のたびに1個の`vkResetFences`を呼ぶため、複数の完了slotをまとめてresetする余地がある。ただし今回比較した[vkdemo](../../userland/base/vkdemo/renderer.c)は`vkQueueSubmit(..., renderer.fence)`、WSIもjobの明示fenceを使う。従って[p008の0.36→0.48秒](phase008/results.md)を、NULL-fence経路のreset増加の結果と説明するのは適切ではない。

queue内をpending・terminal未reset・reset済みの状態に分け、既にterminalと確認できたprivate fence群を一回のnative resetへまとめる。次のsubmitはreset済みslotを使い、二重resetしない。batchを作るために未完了jobを待たない。pendingまたはerrorのslotは再利用しない。単一slotを順次再利用する負荷では往復削減がないことも受け入れる。

cache容量と回収をR1のadmissionに合わせ、空き待ちの再試行ごとにprivate fenceを増やさない。reset失敗時の状態・partial allocation・最終destroyは既存の所有権を維持する。fenceの意味を失うCOMMIT前倒しなどは往復削減の代用にしない。

測定はNULL fenceの連続submit、複数slot同時完了、明示fenceの対照負荷を用い、native reset回数、GPU ioctl回数、cache数、submit遅延とthroughputを比較する。QEMUとrendererのCPU時間を分け、同じ負荷を有限回測定してばらつきも残す。既存の短い一回の観測だけで性能退行の原因や改善率を断定しない。

## R6と実施順

pending fenceのreset/destroyはVulkanのvalid usageの問題として記録する。[vkResetFences](https://docs.vulkan.org/refpages/latest/refpages/source/vkResetFences.html)、[vkDestroyFence](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyFence.html)。`vkDestroyFence`はvoidなので独自の失敗値を追加せず、未完了資源を解放して返る修正もしない。正常経路では不要な待機を増やさず、故障・終了時はR3の有限な終端・回収規約に従う。

ただしnative GPU fenceが完了していても、host markerがそのhandleへ最後にアクセスするまで内部のquiesceが待つ場合がある。この内部寿命の待機まで一律にアプリのvalid usage違反とは分類しない。native完了とmarker退役を区別し、安全なreset/destroyのための待機は維持する。

実施順の提案は、**R1の再現と待機設計 → R3の期限分離・context停止契約 → R4の通知接続 → R5の測定と最適化**である。R2の互換契約調査は並行できるが、stock対応を有効にするのは必要能力と資源寿命を実証した後とする。これは作業の依存順であり、Phase分割を要求しない。

R1はU/K双方、R3はU/Kに加えhost側へ及ぶ変更として見積もる。R4は単純な待機時間変更より広く、R5は比較的局所的な最適化である。R2を「2箇所の変更」、R3を「destroy呼出しの追加」として小さく見積もらない。

通常のBLOB scanout、GPU内の画像共有、標準Vulkan/Wayland API、GPU framework所属のfence、既存の汎用handle/fdを維持する。zwl/libwaylandのテストドライバとしての制限はそのままとし、一般DE、native i915、新HAL変更は今回の対応案へ混ぜない。実装時は対象build、因果関係を確認できる限定fixtureとsanitizer、直接/Waylandの実QEMU描画・複数process・障害回復で検証する。今回の回答作成を、これらの試験成功として扱わない。
