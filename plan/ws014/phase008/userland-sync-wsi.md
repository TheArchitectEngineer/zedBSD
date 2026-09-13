# U の GPU 完了・WSI 契約と検証

WS014 p008 / q311 の U 担当実装記録。Phase 全体の状態・実 QEMU 最終受入は [phase.md](phase.md) と root の結果記録を正本とし、この資料だけで cleared を宣言しない。K は [gpu-job-contract.md](gpu-job-contract.md)、Venus/host は [transport-sync.md](transport-sync.md) に従う。

## 処置

| review3 | U で行った処置 | 主な実装 | 検証・限界 |
| --- | --- | --- | --- |
| A1 | 接続喪失・世代不一致を局所 surface error とし、既存 device/context の DEVICE_LOST は健全な surface で上書きしない | [wsi-swapchain.c](../../../userland/base/libvulkan/wsi-swapchain.c) `swapchain_current`、既存 [wsi-display.c](../../../userland/base/libvulkan/wsi-display.c) の errno 分類 | ENXIO→SURFACE_LOST、ESTALE→OUT_OF_DATE で renderer の error 不変。context 単独の失敗でも Acquire が DEVICE_LOST。K の true transport fault 分類は root 担当 |
| A2 | 本番コマンドコードの opcode 修正は不要。現実装をそのまま通す回帰試験を追加 | [commands.c](../../../userland/base/libvulkan/commands.c)、[context.c](../../../userland/base/libvulkan/context.c)、[libvulkan-command-context.c](../../ws030/tests/libvulkan-command-context.c) | End→Reset→再記録→2個の合法64KiB UpdateBufferによるprefix flush、および48,000個の VkBufferCopy（payload 1,152,000 bytes）を実 commands/context と独立 native decoder で検証。不正な巨大 UpdateBuffer は使わない |
| A3 | 全 native submit を受理前から K 管理の予約へ結び付け、実 VkFence 成功に基づく K signaler へ移行 | [queue.c](../../../userland/base/libvulkan/queue.c)、[sync.c](../../../userland/base/libvulkan/sync.c)、[external-fence.c](../../../userland/base/libvulkan/external-fence.c) | public fence 有無、複数 submit、BindSparse、OOM rollback、native failure、shared generation、CPU wait/poll を検証。実 sparse 対応ハードウェアの受入は主張しない |
| A5 | Acquire の nanosleep を CLOCK_MONOTONIC の condition wait に変更。画像状態観測と待機登録を同じ mutex 内で行い、release/retire/rollback通知で起床 | [wsi-swapchain.c](../../../userland/base/libvulkan/wsi-swapchain.c) `vkAcquireNextImageKHR`、[wsi-wayland.c](../../../userland/base/libvulkan/wsi-wayland.c) release callback | timeout=0/有限期限、実 condition の通知起床、返却 image index を確認。Wayland入力処理と故障確認用の最大10ms進行間隔は残す。polling全廃とはしない |
| A6 | 実行中 job ごとに pool/command/fence の独立スロットを保有し、完了後に再利用 | `present_record`、`present_fence_prepare`、`present_finish`、`present_workers_stop` | 3並列jobは3個のpool/command。native fence が未完了の command への Begin を独立peerで禁止。後続frameで追加生成なし、最後に全poolを1回ずつ破棄 |
| A7 | 外部 fence ごとの completion pthread、condition、worker state を削除。独立 worker pool は追加しない | `external_enable`、`vulkan_external_fence_finish` | 初期signaled、未submit、import、submit、reset、destroyを含むfixtureで pthread_create が0回。present worker・K watchdog は維持 |
| A8 | decoder trailer、opcode、VkResult の既存検査を保持 | [wire.c](../../../userland/base/libvulkan/wire.c)、[context.c](../../../userland/base/libvulkan/context.c) | stale trailer、未完了通知、時計停止の有限上限、EPIPE、別opcode、native DEVICE_LOST、native OOMを区別。terminal後の再送なし、OOMのみ通常再試行可 |

## submission と所有権

1. queue mutex 内で native VkFence を確保する。アプリが `VK_NULL_HANDLE` を指定した場合も queue が private fence を用意し、同時実行数だけキャッシュを増やす。保留中の native identity を次の仕事に転用しない。
2. U の ledger entry と exact shared fd/generation を準備し、`GPU_JOB_RESERVE` を呼ぶ。共有payloadのない通常fenceも fd=-1/generation=0 の job にする。K の record・marker用storage・監督期限は native 受理より前に存在する。
3. U ledger の transaction pin を保持して、native `vkQueueSubmit` または `vkQueueBindSparse` を非NULL fence付きで送る。decoder reply/trailerと、その呼出しのVkResultを確認する。
4. native SUCCESSなら同じ予約を COMMITする。OOMによる確実な未受理だけ CANCEL(0) とし、shared generation をpendingのまま戻す。受理不明・native lossは CANCEL_FAULT またはKのterminal errorへ進め、通常再試行へ戻さない。COMMIT失敗もDEVICE_LOSTとし、accepted workを無かったことにしない。
5. strict host は直前の成功したsubmitに渡された実native fenceを借用し、その成功と最後のaccess終了後だけmarkerを正常retireする。Kがexact job/generationをsignalする。CPU0 decoder通知・Uのcommit完了・通知が来ただけ、のいずれもGPU成功の代用にはしない。
6. native fenceのReset/Import/Destroyとprivate-cache再利用は、そのlocal jobが終端するまで `vulkan_sync_quiesce` で待つ。K待機中はcontext/device mutexを保持しない。public QueueSubmitの外部同期を支えるqueue mutexは呼出経路に応じ保持される。terminal errorはdevice/contextへ残す。

`GPU_JOB_RESERVE` 後にUが停止してもK watchdogが仕事を監督する。まだCOMMITしていない予約・単なる未submit共有fenceはK内GPU依存の根拠にしない。通常のCPU `vkWaitForFences` は、別processが後からsubmitするpending imported payloadを待てる。importしただけでlocal native markerが存在することにはならないため、そのaliasを破棄しても存在しないlocal仕事は待たない。

画像取得の完了はGPU jobとは別である。実際に再利用可能な画像を確保した後、既存の同期BIND/SIGNALで直ちにshared payloadを完了させる。初期signaled作成も同期的に終わり、どちらにも補助threadは不要。

同queueのstrict host markerは単一FIFOで先行実VkFenceから順に待つ。従って、全submit/sparseが必ずCOMMITされる本契約では、queue idleの最後のmarkerは先行sparse jobも追い越さない。空のVkQueueSubmit単体にsparse全体を覆う保証があるとは記述しない。

## WSI キャッシュと待機

WSIのslotはpoolを `TRANSIENT | RESET_COMMAND_BUFFER` で一度作り、primary command bufferを保持する。slotのbusyは、GPU完了とnative表示処理が終わるか、terminal cleanupになるまで戻さない。完了済みcommandへのBegin implicit resetを使うため、定常記録部分はBegin/Endの2取引となる。これはframe全体の取引数や実測FPSを意味しない。private fenceの世代・export fdも同じslotで再利用する。

通常のdirect/Wayland表示は引き続きGPU copy/composition→共有linear image→BLOB表示で、CPU readbackやGOPへ置き換えない。Waylandへのcommitは実GPU完了の後。zwlの同期的テストドライバという承認済み制限、libwaylandの対象範囲は維持する。汎用コンポジタ、seat/input、window toolkitの追加はない。

Acquireは最初から最後までmonotonicの呼出期限を使い、待機期限を残り時間と10msの小さい方にする。画像の選択・状態変更とcondition登録は同じmutexで行う。socket進行とnative能力照会はそのmutex外で実施する。release通知は即時に待機を起こすが、通知なしの未読Waylandイベントや非同期故障も有限間隔で再確認する。

## 検証結果

[userland-verification/verification.json](userland-verification/verification.json) は最終12 jobのコマンド・終了コード・時間・全入力SHA256を保存する。全12 jobがexit=0、実行前後の入力hash一致。各runnerが通常とASan/UBSanを含み、native WSIのみ通常・ASan・UBSanを独立jobとして実行した。

| job | 主な実証 |
| --- | --- |
| sync | fence=NULL の3並列実native identity非共有、完了後20回の生成なし再利用、private cacheでnative GetFenceStatus呼出増分0、通常/sparse/複数submit、wait payloadとOOM巻戻し |
| external-fence | shared payload強参照・permanent/temporary import・世代・reset・初期完了・GPU失敗。外部fence補助pthread生成0 |
| notifications | K容量不足をnative受理前に拒否、all/any/zero/有限wait、exactsequence保持、lock外待機、terminal失敗 |
| job-race | 別reaperによるERROR消費とUへの公開の間を二つの順序で再現し、両observerがDEVICE_LOSTを得ることを確認 |
| context / command-context | A2の実2層統合、A8のreply/trailer/opcode/result、native OOMとterminal失敗の再試行差 |
| discovery | exactstrict profileとGPU_CAP_JOBがないとnative作成前にINITIALIZATION_FAILED。別display node対応の既存extension列挙も確認 |
| swapchain | 3実行中pool/cb、GPU完了前のBegin禁止、完了後再利用・最終解放、実monotonic condition通知起床、surface errorとcontext-only失敗 |
| native-wsi / asan / ubsan | ENXIO/ESTALEの局所分類、独立display admission、BLOB import/front lifetime、copy fallback既存契約 |
| wayland | private protocolの実release callbackからWSI通知境界まで、既存wire/ownership契約 |

回収競合はfixture追加で修正前に再現した。第一はlocal lookup後に別reaperがK ERRORをCONSUMEし、U error公開前のENOENTを成功扱いする順序。第二は最初のatomic error確認後・context mutex取得前に別reaperが失敗を公開しledgerを空にする順序。現在は初回mutex内とENOENT後のmutex内でerrorを再確認し、CONSUMEとerror公開を同じ順序で観測する。[第一の修正前ログ](userland-verification/job-race-before.log)、[第二の修正前ログ](userland-verification/job-race-before-lock-before.log)、[両修正後の通常/sanitizerログ](userland-verification/job-race.log) を残した。

[review.json](userland-verification/review.json) は限定syntax、規約§14レビュー、diffcheck、static analyzerの判定を記録する。syntaxとdiffcheckはPASS。clang analyzerはexternal-fence/queue/WSIが警告0、sync周辺に4警告を出す。3件は別翻訳単位の `vulkan_reply_finish` がnative失敗をSUCCESSへ変えると仮定した出力未初期化経路で、実wire.cは失敗を保持する。1件はVulkanの有効使用条件に反するfenceCount=0でNULL poll配列を辿る経路である。警告を隠したwarning-free全体検証とはせず、[原文ログ](userland-verification/analyzer.log)と読み取り判断を保存した。

## 適用条件と残る限界

- exact168B、magic=`0x5a424453`、flags=3のisolated strict host pairとGPU_CAP_JOBを必須にする。stock hostとp007旧pairでのVulkan device作成はINITIALIZATION_FAILED。kernelのlegacy 2Dと能力照会は維持する。
- nativeなGetFenceStatusの全呼出しを削除したわけではない。通常fenceの公開状態照会やまだsubmitされていないfenceのCPU待機は残る。削除したのは外部fenceごとの補助workerによるnative完了監視と、private cache再利用時のnative queryである。
- 12 fixtureは独立peerによる意味検証で、任意GPU・CTS・実sparse対応GPU・物理hotplugを実証しない。実QEMUの動作・性能はrootの最終同artifact受入を参照し、初回実行値からFPS改善を一般化しない。
- HAL、新Vulkan公開API、GOP代替、一般Wayland機能は追加していない。git add/commit/push、GitHub同期、Phase/Queue状態の変更はこの担当では行っていない。
