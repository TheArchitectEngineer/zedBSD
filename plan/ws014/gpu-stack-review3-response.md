# GPUスタック追加レビューへの回答

- 作成日: 2026-09-13
- 対象レビュー: `gpu-stack-review3.md`「zedBSD GPU スタック 修正後監査 (機能性)」
- 照合対象: zedBSD `ccb686e8e6c8b8ae824bafddddfe87ec83e5c9dc`、virglrenderer 1.1.0
- レビュー原本のSHA256: `c9178d029afc1e2d70784a0fed8ff2febcb1d8b96da49730e1a08aa637c9bca6`

本書は追加レビューと、その後のユーザーとの協議に対する回答である。現在の実装を読んで確認した事実、採用する対応方針、実装時に満たす条件を記録する。今回の回答作成ではコード変更・実行試験を行っておらず、以下の対応を実装済みとは扱わない。新しいPhaseやQueueの作成・実行、既存Phaseの状態変更も本書には含めない。

本書のKはカーネル空間で動く側を指し、`src/kern/`の汎用機能を意味しない。ユーザーの追加方針により、fenceの実装・型・完了責任はGPUドライバフレームワークに置く。以下の「Kをsignalerにする」方針も、この所属で実装する。

## 回答一覧

| 項目 | 判断 | 対応方針 |
| --- | --- | --- |
| A1: 表示エラーのGPU全体への波及 | 修正する | 局所的な表示エラーとGPU・transport故障を分離する。errnoによる一律の故障推定を、backendからの明示的な故障通知へ置き換える。 |
| A2: 途中flush・巨大recordのopcode照合 | 指摘された原因は現HEADに該当しない | 既にno-reply経路を使用している。再記録・巨大recordの検証を補強する。 |
| A3: 依存fence待ちの期限 | producer側の完了保証へ見直す | Kが受理済みの仕事の成功・失敗を確定し、fenceをsignalする方式を採る。consumer timeoutは、この方式を採らない場合の代案とする。 |
| A4: queue・slot容量 | 改善する | descriptor 64・slot 32を初期候補とし、実際のqueue上限、ring配置、marker予約枠、メモリ使用量を合わせて見直す。 |
| A5: acquireの1ms polling | 改善する | monotonicな条件変数待機を使い、Waylandイベントの進行も確保する。 |
| A6: present用command資源の作成・破棄 | 改善する | 同時進行jobごとのslotにpool・command bufferを保持し、完了後に再利用する。 |
| A7: fenceごとのworker | A3に従属させる | Kによる完了通知が成立すれば専用workerを撤去する。worker集約は、それを採らない場合の資源・スケール改善として扱う。 |
| A8: decoder通知とreply trailer | 現在の異常検出を維持する | 固定版proxyの正常経路では順序を確認した。trailer欠落を待ち直しで隠さず、切断・不正応答の検証を補強する。 |
| ユーザー追補: fenceの所属とhandleの型識別 | GPUドライバフレームワークへ移す | `kernel_fence_*`を`drv_gpu_fence_*`へ移し、汎用kern層には不透明handle・fd・参照管理・poll・SCM_RIGHTSを残す。 |

## A1: 表示エラーとGPU故障の分離

指摘の中心は成立する。`gpu_ioctl`と`gpu_dependency_ioctl`が返り値の`ENODEV`・`ETIMEDOUT`からGPU全体の故障を推定すると、出力の切断などの局所的な失敗まで全sessionのfenceへ波及する。transportが正常な場合は、transport故障を条件にする再初期化経路にも入らない。

ただし「モニタを抜くと必ずGPU全体が使用不能になる」とまでは確認していない。通常の古い表示generationでは、切断判定より先に`ESTALE`が返る経路がある。実際の切断による再現試験は今後行う。

対応は一律通知の削除だけでは完結しない。現状の`drv_venus_transport_fail`は、保持しているcompletion callbackがある場合に共通層へ故障を通知する。同期controlだけが失敗する場合や、`transport.failed`を直接設定する経路も含め、真正な故障を確実に通知する必要がある。

- 出力の世代変更は`ESTALE`、出力喪失は`ENXIO`等で区別し、WSIの`OUT_OF_DATE`・`SURFACE_LOST`へ対応させる。
- GPU・transportの故障はbackendが明示通知し、全sessionの終端を必要な場合に限る。
- callback不在時、登録解除との競合時にも、通知先のGPU参照寿命を保証する。
- 既に失敗したfenceや`VkDevice`を、正常な表示操作で成功へ戻さない。

根拠: [GPU共通層](../../src/drivers/gpu/gpu.c)の`gpu_ioctl`・`gpu_dependency_ioctl`・`drv_gpu_report_error`、[Venus transport](../../src/drivers/gpu/venus/transport.c)の`drv_venus_transport_fail`、[表示処理](../../src/drivers/gpu/venus/display.c)の`display_present`。

## A2: no-reply経路の確認と検証補強

現HEADでは、`command_record_finish`の巨大単一record経路と`command_record_flush`は、どちらも`vulkan_context_execute(..., 0, ...)`を直接呼ぶ。返信opcodeを照合する`vulkan_command_execute`は通らず、`vulkan_reply_finish`もopcodeを比較しない。

End後に`recording.opcode`が残ることは事実だが、この送信経路では使用しない。従って、レビューの説明したopcode不一致を理由にno-reply経路を新設する修正は行わない。

既存fixtureにも、転送上限を69,632 byteへ下げて途中flushを起こす検証がある。追加する検証は、同じcommand bufferのEnd→Reset→再記録でのflushと、1MiBを超える合法な単一recordを実contextへ通すケースとする。例えば、独立したbuffer間の多数の非重複`VkBufferCopy`を使う。`vkCmdUpdateBuffer`のデータを64KiB超にするような、APIの制約に反する入力は再現手段にしない。

根拠: [commands.c](../../userland/base/libvulkan/commands.c)、[wire.c](../../userland/base/libvulkan/wire.c)、[既存command fixture](../ws030/tests/libvulkan-commands.c)。

## A3: Kをsignalerにする

### 判断の理由

無期限待機だけを不具合とはしない。問題は、現在の共有fenceが、ゲストUのworkerによるnative `vkGetFenceStatus`確認と`GPU_FENCE_SIGNAL`を必要とする点にある。producerプロセスがSIGSTOPされた場合やworkerが停止した場合、fdもtransportも正常なままsignalだけが来なくなり、consumerの依存待ちが残る。

LinuxのDMA fenceには、producerが合理的な時間内に完了させ、hang時にもエラーで終端する契約がある。ユーザ空間が完了時期を制御する無期限fenceを、その契約へ混ぜないことも明記されている。zedBSDではこの完了責任の置き方を参考にする。guestへのdma-buf採用を意味しない。[Linuxの同期契約](https://docs.kernel.org/driver-api/dma-buf.html#dma-fence-cross-driver-contract)、[Indefinite DMA Fences](https://docs.kernel.org/driver-api/dma-buf.html#indefinite-dma-fences)

採用する構成は、host GPUの完了結果をVenus driverが受け、カーネル空間のGPUドライバフレームワークが対応するfence generationを成功またはエラーへ終端する形とする。ゲストUのworkerによる再確認を完了条件から外す。submitの全経路を非同期job schedulerへ作り替える案や、consumer側の固定期限による故障判定を、この変更の前提にはしない。

### 現markerの保証と不足

| 通知 | 固定版rendererで確認した意味 | Kの成功signalに利用する条件 |
| --- | --- | --- |
| timeline 0 | decoder側の到達通知。GPUを待たずretireする | GPU完了の根拠には使わない。reply/trailer検証は維持する。 |
| 登録済みの非zero queue timeline | 同じVkQueueへ空submitと実VkFenceを投入し、そのfenceを待つ | 対象の仕事・queueとの対応に加え、正常完了と異常終端を区別できること。 |
| device loss・proxy切断時のretire | 現在は通常と同じ、成否を伴わないcallbackへ流れる | そのまま描画成功に変換しない。 |

virglrenderer 1.1.0の`src/venus/vkr_queue.c`では、`vkr_queue_sync_submit`が実VkFenceを投入する一方、`vkr_queue_thread`は`VK_ERROR_DEVICE_LOST`を含む非timeout結果を、同じ`vkr_queue_sync_retire`へ渡す。callbackに成否の引数はない。`src/proxy/proxy_context.c`もrenderer切断時に未完了fenceを強制retireする。

従って、既存markerをそのまま成功signalへ接続する変更では成立しない。host→QEMU→Kへ成功・失敗を運ぶ完了契約、またはKが同等の成否を確定できる仕組みを補う。具体的な表現は実装設計時に確定する。host側を拡張する場合は対応能力を明示して合意し、従来hostで誤って有効にしない。障害時に正常retireを抑止する方式を使う場合も、後続markerによる一括retireが失敗した仕事を成功扱いしないことが必要である。

参照した固定版は[virglrenderer 1.1.0](https://gitlab.freedesktop.org/virgl/virglrenderer/-/tree/1.1.0)。現行のゲスト側契約は[transport・同期の記録](phase007/transport-sync.md)にある。本書はその契約を変更する方針であり、変更完了の記録ではない。

### 仕事の受理と有限時間内の終端

次の条件を一体で実装する。

1. **完了登録を必須にする。** 現在はnative submit後にmarkerを確保し、`EAGAIN`・`ENOMEM`なら省略できる。Kをsignalerにする経路では、必要な資源を事前に予約し、受理済みの仕事を監督対象から外さない。
2. **Uの再実行を必要とする隙間をなくす。** native送信・marker・対象generationの接続を、Kが完了責任を持てる形にする。`sequence=0`の予約だけを残してproducerが停止する状態を、受理済み仕事と混同しない。
3. **カーネル内の依存待ち対象を限定する。** pendingなら、Kが受理して成功・失敗への終端を保証する仕事のgenerationに限る。作成・reset直後の未投入fenceや、Uだけがsignalするfenceは、その待機へ持ち込まない。未受理の結果を返す方法はUAPI設計で定める。
4. **producer側の監視で終端する。** 現在の10秒watchdogはPOSTED requestをKスレッドが独立して監視するので再利用できる。未投入・予約だけの状態までは現在の監視対象ではない。producerのfinal close、device loss、transport故障、device reset時にも対応する未完了の仕事をエラーで終端する。
5. **競合を一度だけ処理する。** marker完了がbinding接続に先行する場合、fdのclose/reuse、reset後の旧generation通知、device退役との競合を処理する。失敗通知を出したことだけで、まだDMAが参照する資源を解放しない。
6. **容量待ちが依存解除を妨げない。** marker枠を増やすA4と合わせ、未完了markerだけで後続の制御・依存解除操作を塞がない。受理・予約・backpressureの契約を合わせて検証する。

既存の`GPU_FENCE_WAIT`、`poll`、標準`vkWaitForFences`等のCPU待機は、timeoutや未signal状態の観測を引き続き提供する。K内部の依存待ち対象を限定することと、アプリが未signal fenceを明示的に待つことは別の契約である。

Kによる終端保証を備える方式を採らない場合は、ユーザー指定に従いA3のconsumer timeout案を採る。その場合は既存ioctl layoutを維持し、期限付き形式を追加する。期限切れでは待機先・signal先のpayloadを変更せず、未受理として再試行可能にする。対象は依存待ち部分の期限であり、ioctl全体の時間上限やGPU故障の判定とは区別する。これは代案であり、両方式を同時に必須実装とはしない。

### U側に残る役割

通常の`vkQueueSubmit`・空submit・複数submit・`vkQueueBindSparse`では、対応する仕事全体を覆う完了をK payloadへ結ぶ。sparseを含むqueueごとの順序保証も確認する。初期signaledや終端済みimportに監視workerは不要で、受理済み仕事に結び付いたpending importは生成元のK所有の仕事が終端する。

acquireのように、Uが画像利用可能を確認してその場で完了させる処理は別に残る。その未完了状態を、Uの将来実行に依存するK内部の無期限待機対象として公開しない。reset・temporary import・destroyの世代、native object、参照寿命は維持する。

Wayland WSIの内部private fenceには現在通常fenceを使う経路もある。GPU完了確認をKへ揃える対象にこれも含め、K-backed fenceまたは同等の確定したK完了を用いる。表示・Wayland commit・後始末を行うpresent worker自体は維持する。

根拠: [sync.c](../../userland/base/libvulkan/sync.c)、[external-fence.c](../../userland/base/libvulkan/external-fence.c)、[queue.c](../../userland/base/libvulkan/queue.c)、[K fence](../../src/kern/fence.c)、[GPU fence UAPI](../../include/uapi/gpu-fence.h)。

## A4〜A6: 転送・待機・資源再利用

### A4: queue・slot容量

descriptor 64・slot 32、marker用をslotの半分程度とする案を初期候補にする。実際に提示されたqueue最大値に従って設定する。QEMU 10.0.0の公式実装では制御queueは2D用が64、virgl有効時が256であり、「QEMUは常に64」とは固定しない。[QEMUのqueue作成](https://github.com/qemu/qemu/blob/v10.0.0/hw/display/virtio-gpu-base.c)

現在のavailable/used ring位置は128/256 byteに固定されている。descriptorだけを64へ増やすと配置が重なるので、ringの配置・整列・範囲検査を同時に変更する。slotごとのcommand/response DMAメモリも増えるため、実際の確保量を記録する。

slot拡大だけでcontroller mutexによる同期controlの直列化は消えない。複数context、marker飽和中の制御要求、out-of-order完了、ring wrap、reset時の未完了資源を検証し、性能は変更前後の同じ負荷で測る。

### A5: acquire待機

libcの既存条件変数待機を利用する。Vulkanの待機期限に合わせ、monotonic clockを使用し、画像状態の検査とsleep登録の間で起床を取りこぼさない形にする。

Waylandの`wl_buffer.release`はsocketからdispatchしなければ状態へ反映されない。条件変数だけの無期限待機へ置き換えず、イベント進行のための有限周期、またはsocketと接続した起床経路を残す。有限周期を残す段階ではpolling全廃とは呼ばない。timeout 0・有限・無限、遅れて届くrelease、surface/device lossを確認する。

### A6: present用pool・command bufferの再利用

workerが逐次処理していても、その前に呼出側で複数jobをGPUへsubmitできる。そのためworker全体でpool・command bufferを1組だけ共有せず、既存private fence cacheと同様に、同時進行slotごとに予約して完了後に再利用する。

明示的な`ResetCommandPool → Begin → End`は定常時3トランザクションであり、レビューにある2往復にはならない。`RESET_COMMAND_BUFFER_BIT`を指定したpoolと、完了済みbufferに対するBeginのimplicit resetを使う方式なら、記録処理をBegin＋Endの2トランザクションにできる。これはsubmit・表示・fence操作を除いた数であり、フレーム全体の往復数や速度倍率ではない。[Khronosのcommand buffer仕様](https://docs.vulkan.org/spec/latest/chapters/cmdbuffers.html)

queue family、allocator、未受理時のrollback、device loss、最後の資源回収を維持し、pending slotを再利用しないことを検証する。

根拠: [transport.c](../../src/drivers/gpu/venus/transport.c)、[wsi-swapchain.c](../../userland/base/libvulkan/wsi-swapchain.c)、[pthread.c](../../userland/base/libc/pthread.c)。

## A7とworkerの扱い

A7は正しさの修正ではなく、資源消費とスケールの改善である。現実装ではexport可能なfenceを作るたびに、初期signaledや未使用の場合も専用threadを作る。idle中は条件変数で眠るためCPUを回し続けないが、fenceが存在する間はthread・stack・TLS等を保持し、作成・破棄にはcreate/joinを伴う。

libcの標準stack確保は1 threadにつき1MiBと4KiBのguardである。例えば100個のexport可能fenceなら、監視用に100 thread分のmappingを要求する。これはコード上の確保要求量であり、常駐物理メモリの実測値ではない。WSIの少数の再利用fenceだけでは、集約によるFPS改善は未確認である。

Kによる完了通知を採る場合は、この専用workerを撤去する。採らない場合には、deviceごとに1 workerを置き、完了sequenceに対応するnative fenceを確認する構成を検討する。同時完了した複数の`vkGetFenceStatus`をbatch化すれば往復削減も可能だが、単にthreadを集約しただけで自動的にbatch化されるわけではない。未完了の1件で他の完了通知を止めず、自分がsignalするK fenceを待つ循環待機も避ける。

| worker | 今回の扱い | 理由・注意 |
| --- | --- | --- |
| transportの`venus_worker` | 現在の遅延起動・idle中の無期限睡眠を維持 | 最初の非同期submitで起動し、POSTED requestの期限をUから独立して監視する。idle停止による主な利益はthread資源の削減。 |
| `display_console_worker` | 主画面の占有中は待機し、releaseで起床。テキスト更新通知へ接続する方向で改善 | 現在の100msごとのmutex取得を減らす。claimごとの停止・再作成は基本方針にしない。 |
| libvulkanのpresent worker | queueごとの遅延起動を維持 | 表示・Wayland commit・後始末を担当する。A3で完了確認先は変わるが、worker自体は必要。 |
| external fence worker | A3のK主体方式が成立した時点で撤去 | device workerへの集約を先行して実装しない。 |

watchdogの現停止処理はtransport全体の`stopping`を立てるteardown用なので、idle停止へそのまま流用できない。将来idle停止を追加する場合は、停止・再開・request公開・reapの競合を扱う。最終closeの検知に新しいGPU opsは必須ではなく、既存Venus open/closeで内部session数を管理する方法もある。汎用callout機構の追加は別課題とする。

console workerが担当するのは主画面だけである。主画面の通常leaseと旧表示経路の所有権を見て待機し、release・所有session終了・停止要求で起こす。別出力のclaimでは止めない。現在のclaimはcontroller mutexを保持するので、その中から既存stop/joinを呼ばない。text側には世代番号があるが通知登録APIはないため、完全なイベント駆動には軽い通知接続を追加する。まず主画面占有中のpollをなくす段階も独立して検証できる。

present workerも同じpthreadなので、1本当たりの標準stackコストはexternal fence workerと同じである。維持する理由は、数が使用queueに限られ、表示処理を担当するためである。

## A8: decoder replyの異常検出

固定版virglrenderer 1.1.0の正常proxy経路では、同じsocket上のcommandを逐次処理し、同期decodeでreplyを共有メモリへ書いた後に、後続のtimeline 0 fenceを通知する順序を確認した。

一方、socket切断ではreplyを書かずにfenceを強制retireする経路がある。この場合のtrailer欠落を`DEVICE_LOST`にする処理は必要であり、無条件の待ち直しや猶予時間で隠さない。別renderer・別アーキテクチャまで同じ順序を検証済みとは扱わない。

A3のGPU queue完了契約と、A8のdecoder reply検証は別の境界である。A3でKをsignalerにしても、通常のnative API応答のtrailer・opcode・`VkResult`の検証は維持する。

根拠: [context.c](../../userland/base/libvulkan/context.c)と、virglrenderer 1.1.0の`server/render_context.c`、`src/venus/vkr_context.c`、`src/venus/vkr_cs.h`、`src/proxy/proxy_context.c`。

## ユーザー追補: fenceをGPUドライバフレームワークへ移す

### 所属と理由

`kernel_fence`を汎用kern機能として維持せず、`drv_gpu_fence_*`をGPUドライバフレームワークの機能として実装する方針を採用する。GPUの世代、producerの所有権、仕事の完了、表示との依存関係は、その利用と契約を持つドライバ側が管理する。

目的は、UNIX-likeなfd・poll・SCM_RIGHTSの受け口を維持しながら、GPU固有の抽象を汎用kern APIへ広げないことである。内部ファイルの配置だけでPOSIX互換性が決まるわけではないが、汎用層が知る型と責務を限定し、GPUを組み込まない構成からGPU同期実装を外せる境界にする。

将来camera・ISP・codec等が同じbufferを共有する場合は、必要となった時点でドライバ間の契約として`drivers/gpu-fence.h`を利用する。将来の再利用を理由に、先に汎用kern機能へ拡張しない。他ドライバから使う場合も、正しいdevice・generation・producer権限と完了保証を満たす必要があり、任意の共有fdを自由にsignalできる契約にはしない。

### 他OSを参考にする範囲

| OS | 確認できる実態 | zedBSDへの適用 |
| --- | --- | --- |
| Linux | 実装は`drivers/dma-buf/dma-fence.c`にあり、GPU描画・映像処理・表示等のDMAを同期するドライバ間の契約として提供される。Linux自身はこれをkernel internalな同期機能と説明している。[Linux v6.12の実装](https://github.com/torvalds/linux/blob/v6.12/drivers/dma-buf/dma-fence.c) | カーネル空間で使う同期と、汎用kern層が所有する抽象を区別する参考にする。Linuxのdma-buf ABIは導入しない。 |
| Windows | monitored/native GPU fenceはWDDMとDirectX graphics kernel subsystemの契約であり、DxgkrnlがGPU側の状態と待機を管理する。[DirectX graphics kernel subsystem](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/directx-graphics-kernel-subsystem)、[GPU fence objects](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/native-gpu-fence-objects) | GPU同期をgraphics subsystemへ置く責務分担を参考にする。 |
| macOS / Metal | 公開仕様ではfence・eventをMetalのGPU resource synchronizationとして提供する。[Appleの同期資料](https://developer.apple.com/documentation/metal/resource-synchronization) | GPU側のAPIとしての位置づけを参考にする。IOGPU内部の非公開実装と汎用kernelの依存関係までは、本調査で検証済みとしない。 |

この判断に「他用途が長年存在しない」という全利用者の網羅調査や、V4L2 explicit syncの採用状況の断定は必要ない。zedBSDで現在必要な責務と、ユーザーの層分け方針を根拠に所属を決める。

### 汎用kern層とGPU側の境界

| 所属 | 残す・移す責務 |
| --- | --- |
| 汎用kern層 | fd table上の不透明オブジェクト、handleのrefcountと最終release、fd alias・close、`ops->poll`への委譲、unix socketのSCM_RIGHTSによる参照受け渡し。既存のlock・waitq等の基盤は引き続き使う。 |
| GPUドライバフレームワーク | fence payload、device identity、generation、producer所有権、bind・wait・signal・reset、poll readinessの判定、成功・失敗による終端。GPU allocationとfenceの実型もここで識別する。 |
| Venus等のbackend | 対応するGPU仕事の受理・完了・故障をフレームワークへ通知する。A3のhost成否契約とwatchdogを接続する。 |

汎用層にはGPU/fence専用の分岐を追加せず、`kernel_handle_ops`を通してドライバへ処理を委譲する。A3のK主体化とこの移動は両立し、汎用kernへのfence機能追加を必要としない。

### 移動・名前・型識別

| 現在 | 変更後・扱い |
| --- | --- |
| `src/kern/fence.c` | `src/drivers/gpu/gpu-fence.c`へ移動する。 |
| `include/kern/fence.h` | `include/drivers/gpu-fence.h`へ移動する。 |
| `kernel_fence_*`、関連する内部型・定数 | `drv_gpu_fence_*`等のGPUフレームワーク名前空間へ揃える。 |
| `include/uapi/gpu-fence.h`の`GPU_FENCE_*` | この移動による名前・ioctl番号・構造体layoutの変更は行わない。A3で必要な契約追加とは分けて扱う。 |
| `KERNEL_HANDLE_GPU` / `KERNEL_HANDLE_FENCE` | kernのGPU専用列挙を廃し、まず`KERNEL_HANDLE_DRIVER`相当の共通分類とdriver所有のops識別を使う。 |
| 呼出元・build・fixture・現行契約資料 | 移動先のinclude、symbol、link対象、型識別に合わせる。過去の試験結果・履歴は当時の事実として保持する。 |

現在のproduction codeでfence APIを直接利用するのはGPU共通層の`gpu.c`であり、libvulkan・zwlはUAPI経由である。この所属変更のためにアプリ向けAPIを追加しない。現行fixture・runnerとremote試験のsource snapshot定義は移動先へ追随させる。payload単体試験を改名する場合は、既存の`gpu-fence.c`試験と衝突しない名前を使う。

初期案では新しい汎用型登録機構を追加せず、共通のdriver handle分類を使う。handle取得で強参照を得た後、GPU側が不変のops identityを照合してからpayloadを解釈する。現行の`fence_resolve`と`gpu_shared_operations`の照合を維持・整理し、fence・image・allocationの取り違えを拒否する。imageとallocationのenvelopeの検査も残す。tag統合を理由に型検証を削除しない。

実装移動・名前変更は比較的機械的に進められるが、型tagの統合とbuild条件は意味を持つ変更である。「名前の付け替えだけ」として型拒否・参照寿命の確認を省略しない。

### build条件と確認範囲

現在の`vmunix.mk`はamd64・arm64・sparcv9・x68kではsource list、pc98・pcatではobject listへfenceを無条件に含める。6構成ともGPUフレームワークの`gpu.c` / `gpu.o`も無条件で、`CONFIG_DRIVER_PCI_VENUS`が選択しているのはVenus backend側である。

従って、6箇所のkern listからfenceを外してGPUの隣へ移すだけでは、GPUを使わない構成からの除外にはならない。`gpu.c`と新しい`gpu-fence.c`を同じGPUフレームワークの組込み条件で選択するbuild上の単位を用意する。Venus等のbackendが有効な場合はその依存としてフレームワークを組み込み、不要な構成では両方を外せるようにする。条件名と各platformの既定値は実装時に明記する。

確認は、6構成の入力listと依存関係、GPUなし構成でのobject/symbol不在、GPUあり構成でのlink、旧include・symbol参照の残存、fdの型取り違え拒否を含める。既存のfence・handle・GPU・SCM_RIGHTS・pollの限定fixtureを移動後の実装へ接続し、generation、owner、close/reset、異常終端、最後のreleaseを確認する。一般fd・socketの処理にGPU固有の型知識が残らないことも確認する。

根拠: [handle.h](../../include/kern/handle.h)、[handle.c](../../src/kern/handle.c)、[移動前のfence実装](../../src/kern/fence.c)、[GPU共通層](../../src/drivers/gpu/gpu.c)、[amd64 build](../../platform/amd64/vmunix.mk)、[pc98 build](../../platform/pc98/vmunix.mk)。本追補は移動方針の記録であり、作業中のfenceソース差分へ適用した結果ではない。

## 維持する範囲と検証

直接表示・Wayland表示とも、通常のGPU描画→共有linear画像→BLOB scanout経路を維持する。通常表示をGOP framebufferへのCPUコピーへ戻さない。既存の明示的なCPU fallback、表示nodeの組合せ、標準OPAQUE_FDの互換条件は、対応能力と失敗条件を区別したまま維持する。[既存の表示契約](phase007/display-wsi.md)

`zwl`の1パス1surface同期presentと、`libwayland`の限定protocol対応は、ユーザー承認のテストドライバの範囲として維持する。一般Wayland環境、複数window合成、入力、既存toolkit対応、native i915、汎用calloutは今回の回答範囲に追加しない。現時点では新しいHAL変更を前提にしない。

| 対応 | 実装後に確認する主要な条件 |
| --- | --- |
| A1 | 局所的な切断・世代変更が他sessionを故障扱いにしない。同期controlのみの故障も通知される。 |
| A2・A6 | 再記録・途中flush・合法な巨大record、複数job同時進行、pending資源の再利用禁止、完了後の再利用と回収。 |
| A3・A7 | producerのSIGSTOP後も受理済み仕事がKで終端する。renderer停止・切断・host device lossを成功扱いしない。未投入・予約だけのfenceを内部依存待ちへ入れない。 |
| A3・A4 | marker容量不足、受理と完了の競合、複数contextの独立進行、旧generation通知、close/reset、最後の資源回収。 |
| A4 | ring配置・wrap・out-of-order、marker飽和中の制御要求、実メモリ使用量。 |
| A5・console | 起床取りこぼし、monotonic期限、遅延Wayland release、主画面のclaim/release・所有者終了・console復帰。 |
| A8 | 正常replyの順序と、reply欠落・renderer切断の異常検出。 |
| fenceの所属変更 | GPUあり・なしのbuild条件、driver側の型識別、汎用fd/poll/SCM_RIGHTSの参照寿命、移動後の世代・所有権・終端契約。 |
| 性能・表示回帰 | 同じQEMU負荷で往復数・thread数・CPU負荷・フレーム時間を比較し、BLOB scanoutと描画内容も確認する。 |

着手順は、fenceのGPUフレームワークへの所属・型識別・build条件を整理し、A1の障害範囲、A3の完了契約・受理保証とA4の容量設計、A3成立に伴うA7 worker撤去を具体化する。A5・A6・console待機改善を接続し、A2・A8の検証を合わせる。これは対応の依存関係を示すもので、Phase分割や実行開始の記録ではない。実装結果と性能値は、今後の限定試験・実QEMU検証で確認する。
