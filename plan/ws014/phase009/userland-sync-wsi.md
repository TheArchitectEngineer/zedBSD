# p009: Uのadmission・WSI待機・private fence

この資料はq312のlibvulkan実装契約と限定検証を記録する。Phaseの完了判定、Kの停止確認能力、実QEMUの最終同artifact受入は[Phase](phase.md)と最終結果資料が管理する。p008の結果を新実装の受入へ流用しない。

## 実装と境界

| 対象 | 現在の実装 | 主なソース |
| --- | --- | --- |
| R1: 未受理admission | native fence準備→capacity QUERY→完了回収→非待機RESERVE。EAGAINだけは未受理の内部`VK_NOT_READY`としてqueue/device/context mutexを外し、capacity WAIT後に再取得・再符号化する。ENOMEMは実OOM、native受理後の失敗は通常のretryに戻さない。 | [queue.c](../../../userland/base/libvulkan/queue.c), [sync.c](../../../userland/base/libvulkan/sync.c) |
| R1: 通知競合 | QUERYはnative準備の後・reapの前。terminal回収前のsequenceを渡すので、未回収の自分のrecordが空きを塞いでもKの変化通知から再reapできる。WAITは250msでU局所errorも再確認する。実capacity通知なら直ちに復帰し、interval timeout/EINTRをOOMやdevice lossへ変換しない。 | [gpu-job.h](../../../include/uapi/gpu-job.h), [sync.c](../../../userland/base/libvulkan/sync.c) |
| R1: retryの所有権 | native未受理の間はtemporary wait配列を保持するが、software-signaled状態は毎回device mutex下で再読・再符号化する。external fenceのfd/generationも再照会する。native Resetは同じretryで一度だけ。locked内部submitは待機中queue mutexを外し、return時に元どおり保持してjob公開へ進む。 | [queue.c](../../../userland/base/libvulkan/queue.c), [external-fence.c](../../../userland/base/libvulkan/external-fence.c) |
| R3: 実GPU完了待機 | present workerのproducer fence待機は`UINT64_MAX`。10秒のU独自期限で長い正常jobを故障へ変換せず、accepted jobの期限・ERRORはKの共通監督から受ける。native display/Wayland固有の別期限は維持。 | [wsi-swapchain.c](../../../userland/base/libvulkan/wsi-swapchain.c) |
| R4: direct Acquire | 即時に画像を取得できる場合はwake fdを作らない。初めて画像不足を観測した場合だけ、mutex外でprivate nonblocking/CLOEXEC pipeを登録し、全predicateを再確認する。renderer fdのERR/HUP、独立display fdのPOLLPRI/ERR/HUP、各waiter固有pipeを`ppoll`で同時待機する。 | [objects.c](../../../userland/base/libvulkan/objects.c), [wsi-swapchain.c](../../../userland/base/libvulkan/wsi-swapchain.c) |
| R4: topology | leaseが保持する独立display openでQUERY→inventory/generation再検査→exact ACK。途中の新eventはreadyのまま残す。event mutexはnative present用mutexと別で、present待機に直列化しない。通常GPU POLLINは監視しないので、別observerの未回収completionによる反復起床は起こさない。 | [wsi-display.c](../../../userland/base/libvulkan/wsi-display.c) |
| R4: Wayland | 既存のmonotonic最大10ms間隔によるprivate queue進行とcondition通知を維持。monitor thread、外部fence監視thread、一般compositor機能は追加しない。 | [wsi-wayland.c](../../../userland/base/libvulkan/wsi-wayland.c), [wsi-swapchain.c](../../../userland/base/libvulkan/wsi-swapchain.c) |
| R5: fence=NULL | queueごとのcacheをEMPTY/READY/PREPARING/INFLIGHT/TERMINALで管理。PREPARINGはmutexを外したadmission待機中も専有し、他の内部submitが借りない。正確なK jobのSUCCESS退役を確認したTERMINALだけを最大64本ずつ1回のnative ResetFencesでREADYにする。64は一括transactionの上限でありcache全体の固定上限ではない。 | [queue.c](../../../userland/base/libvulkan/queue.c) |
| R6: 最後のnative参照 | private/public fenceともKの正確なjob退役前にReset/Destroyしない。ERRORでcontextが失われた場合はUのnative command gateがReset/Destroy送信を止める。host native objectの物理退役はK/backendの停止確認・context teardownへ残す。論理ERRORをhostの最後の参照終了と同一視しない。 | [sync.c](../../../userland/base/libvulkan/sync.c), [wire.c](../../../userland/base/libvulkan/wire.c) |

## 故障範囲とwakeの寿命

`vulkan_device_error`は一つの論理device、`vulkan_context_error`はそのrenderer contextを共有するdevice群へ、従来のerror値をrelease公開して通知する。関係する読出はacquireで取得する。初期化中の未公開storageを除き、device/context errorの非atomic直接読出・代入は残していない。reader/writer/command-buffer自身の局所decode/recording errorは別の状態であり、無条件にGPU故障へ広げない。

独立display nodeの消失はsurface lossであり、正常なrenderer contextをpoisonしない。renderer fdの故障はcontext loss、pipe/condition等のU待機機構自身の異常はdevice-local lossを通知する。既にcontext-only lossがあるという観測だけで、別途device.errorを書き換える処理は追加しない。

wake registryは専用mutexを持ち、その保持中はnonblocking writeだけを行う。device/context/queue/display/WSI mutex、アプリcallbackへ入らない。既存のそれらのlockを保持したerror公開も、逆向きのlock取得を作らない。画像状態通知は既存WSI conditionと同じ対象へbroadcastし、各fd waiterには独立tokenを残す。error通知はdevice/context identityで選別する。

登録後は必ずerror・画像状態を再観測し、pipeの過去tokenを消費してからpredicateを調べる。画像の観測・状態変更はWSI mutex下で行う。最後の観測と実ppollの間の通知はpipeに残る。解除はregistry mutex下でlistから除外してからfdをcloseし、途中のpublisherが再利用されたfdへwriteすることを防ぐ。swapchain/leaseをAcquire終了まで保持するVulkanの有効なhost lifetime条件を前提とし、破棄済みswapchainへの並行アクセスを合法化するものではない。

## 能力交渉

libvulkanがnative Instanceを作成する前に、hostのOPAQUE+STRICT+QUIESCE契約と、別々のK能力`GPU_CAP_JOB`・`GPU_CAP_JOB_CAPACITY`を確認する。最終的に利用できるhost suffixはexact168B、magic=`0x5a424453`、flags7だけとする。旧Kを「空き待機できる」とみなした暗黙fallbackはしない。

| capset profile | parserによる認識 | このlibvulkanでのnative利用 |
| --- | --- | --- |
| flags1 | OPAQUE | 不可。strict完了とnative停止の契約がない。 |
| flags3 | OPAQUE+STRICT | 不可。既知の旧profileだがnative停止を証明できない。 |
| flags7 | OPAQUE+STRICT+QUIESCE | KのJOB/CAPACITY能力も満たす場合に利用する。 |
| flags4/5/15、未知magic、サイズ不一致 | 利用可能な完全profileとは認識しない | 不可。将来の未知flagsを推測しない。 |

旧profileのnodeはmetadata照会後、native Instance・reply/command storage生成前に閉じて列挙対象から除く。他の対応GPUは利用でき、全nodeが非対応なら既存の`vkCreateInstance`成功・physical-device列挙0件という動作を維持する。既に物理handleがある内部境界でも`vkCreateDevice`が同条件を再検査し、不足ならnative device生成前に`VK_ERROR_INITIALIZATION_FAILED`を返す。既存のlibvulkanにはログ出力機構がないため、新しいstderr出力は追加しない。

flags7のnative停止能力とKのcapacity能力は別のもの。旧flags3はcapset認識とraw Kクライアント向けの保守的fallbackを残すが、このlibvulkanの通常互換profileとは呼ばない。旧profileを開いた情報照会だけのsessionはnative command未投稿の根拠により通常closeできる必要があり、そのK/backend契約は[quiescence提案と検証](renderer-quiesce/README.md)で別途確認する。実native workを投稿した旧rawクライアントの停止確認不能は引き続きglobal quarantineとなり得る。

stock virglrendererをstrictと偽装しない。hostのraw未追跡native workまで停止確認できるかはhost/backend契約の結果資料を参照し、Uのfence待機やerror公開だけをその証明にしない。

## 限定検証と修正経緯

| fixture | 確認する意味 |
| --- | --- |
| [submission-verification](submission-verification/verification.json) | EAGAIN→3mutex解放WAIT→同じ要求の再試行、実OOMの区別、別内部submitによるPREPARING専有、3pendingのnative identity非共有、3terminalを1resetで再利用。rootが最終sourceで保存する結果。 |
| [wsi-acquire-fd.c](../tests/wsi-acquire-fd.c) | 即時Acquireのpipe2=0、predicate確認後・ppoll前のrelease、finite期限、2waiterの独立fd、present workerなしのcontext errorとGPU fd HUP、解除後fd寿命。実ppoll/pipe2を使い、通常+ASan/UBSanで確認。 |
| [wsi-native.c](../../ws030/tests/wsi-native.c) | 実adapterのQUERY→inventory→exact ACK、inventory途中の後続event保持、失敗QUERY未ACK、renderer/displayのerror範囲。通常・ASan・UBSan。 |
| [wayland-wsi-swapchain.c](../tests/wayland-wsi-swapchain.c) | shared/shared-fence/fallbackの3profile。GPU完了前のnative present禁止、BLOB所有権、GPU crop/scale、Wayland release待ち、context-only error、OOM/teardown。通常+ASan/UBSan。 |
| [libvulkan-context.c](../../ws030/tests/libvulkan-context.c) / [libvulkan-discovery.c](../../ws030/tests/libvulkan-discovery.c) | exact host suffix13profile、recognizedとusableの区別、旧nodeと新nodeの両順混在、全旧nodeでnative投稿0、strict/quiescence/capacity不足をnative device作成前に拒否。既存copy/wire/storage lifetimeを含む通常+ASan/UBSan。最終追加記録は[profile7.json](userland-verification/profile7.json)。 |
| command-context / job-race / Wayland WSI | 既存の合法な1,152,000byte command batching、End/Reset/rerecord、ERROR record消費とerror公開の競合、private Wayland wire/ownershipを保持。 |

[userland-verification/verification.json](userland-verification/verification.json)の10 jobはすべてexit=0で、追跡したlibvulkan source/header/packageの実行前後hashが一致する。最終のretry境界強化後は、関係するsync/notify/external-fence/job-raceとdirect fixtureの[補足5 job](userland-verification/admission-boundary.json)もすべてexit=0・同hash一致。新しいpublic helperでerrorを通知する以外の各API familyの追加回帰と、target build/API/ABI整合はrootの結果を参照する。

[review.json](userland-verification/review.json)に規約§14、error全域、lock順序、fd退役、syntax/diffcheck、限定static analyzerの監査を保存した。新queue警告は、opaque writerがNOT_READYを返す仮定でcapacity sequenceが未設定になる経路だった。RESERVE EAGAINのときだけsequenceを公開し、他のNOT_READYを容量待機へ送らない境界にして、[queue最終解析](userland-verification/queue-analyzer-final.log)は警告0となった。syncの4警告はp008と同じ分類（opaque calleeが失敗を成功に変える仮定3件、有効使用条件外のfenceCount=0でNULL poll配列を辿る仮定1件）として[原文](userland-verification/analyzer-initial.log)を残す。objects/external-fence/WSIはこの限定解析で警告0。全sourceがwarning-freeだったという主張ではない。

K担当の独立read-onlyレビューでは、PREPARING専有、全producer mutexを外したWAIT、native準備一回→QUERY→reap→RESERVE、再取得後のhealth/payload再確認についてK契約と一致し、この範囲のblocking defectは見つからなかった。WSI全体や実host停止能力をそのレビュー結果へ含めない。

初稿ではAcquire wrapperが既存のcontext-only errorを再観測した際にもdevice.errorへ重ねて公開し、Wayland回帰の既存assertが失敗した。[初回結果](userland-verification/verification-initial.json)と[失敗ログ](userland-verification/wayland-swapchain-initial.log)を保持した。余分な公開を削除し、U待機機構自体が失敗したときだけdevice-local公開を行う修正後、元のassertを維持して3profileすべて通過した。

実GPU性能について、fixtureの「3terminal→reset1回」「20 NULL submit→新規Create0/reset7回」は選択した有限条件でのnative transaction計数。vkdemoや通常WSIの明示fence付きframeへ、そのまま改善率を外挿しない。実QEMUの2process・NULL/明示fence・CPU時間・long jobについては、rootが保存する[runtime集計](runtime-verification/summary.json)と対応するhost/image/source hashを参照する。

新規公開Vulkan API、HAL、GOP描画、一般Wayland/input/toolkitは追加していない。この担当はsource・限定fixture・資料のみで、git add/commit/push、GitHub書込、Phase/Queue状態編集を行っていない。
