<!-- awesome-plan project=zedbsd record=ws014-p008 -->

# WS014 p008: GPU完了責任・fence所属と描画資源の改善

<!-- awesome-plan-current:start -->
Status: cleared
Phase disposition: normal
Parent: [WS014](https://github.com/awemorris/zedBSD/issues/15)
Queue: q311 finished / q311-i01 cleared (whole Phase)
Execution: approved review A1-A8 and driver fence ownership accepted
Dependencies: cleared ws014-p007; accepted p002/p003/p005/p006 and completed WS030
Next: ws014-p009 in-progress (q312) → ws014-p004 planning; native i915 stays separate WS029
<!-- awesome-plan-current:end -->

Combined ID: `ws014-p008`
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2, O4

## 目標と承認

GPUの仕事の完了責任をGPUドライバフレームワークへ揃え、producerのユーザー空間workerが停止しても共有fenceを成功またはエラーへ終端できる描画経路にする。同時に局所表示エラー、queue容量、WSI待機・資源再利用を改善する。ユーザーがレビュー回答書を確認して、独立Phaseの定義と実行を指示した。p007のclearance・実測は保持し、今回の追加改善を過去の達成へ書き換えない。

承認済み回答書: local `plan/ws014/gpu-stack-review3-response.md`、SHA256 `eda89136ad50f98b9ba97b4fc9971c424ea03b9e69a931838ab6d969c9274f57`。本Issueの承認回答コメントへ全文を保存する。原レビューSHA256 `c9178d029afc1e2d70784a0fed8ff2febcb1d8b96da49730e1a08aa637c9bca6`。回答書の「今回実行しない」は回答作成時点の履歴であり、本Phaseの実行承認を妨げない。

## 実装・確認する内容

| 項目 | 処置と受入条件 |
| --- | --- |
| A1 | 局所display世代・切断とbackend故障を分離。errnoだけによる全session故障推定を廃し、callback不在/登録解除競合も含む明示故障通知を保証。正常な別出力・別processの描画を保持する。 |
| A2 | 現no-reply経路を維持。同一command bufferのEnd→Reset→再記録flush、合法な1MiB超recordを実コードfixtureで通す。 |
| A3 | Kをsignalerにする。仕事・generation・予約・host queue完了を結び、予約後/native受理後のU停止を含めproducer期限で終端。未投入のU-only pendingをK内部の無期限依存待ちへ入れない。CPU待機timeoutは維持。 |
| A4 | 初期上限64descriptor/32slot、実advertised queue最大値に従い縮小。ring重複・整列・範囲・DMA量と制御枠予約を見直す。複数context、飽和、out-of-order、wrap、IRQ、reset時quarantineを検証。 |
| A5 | acquireをmonotonic条件変数待機へ。release・errorの取りこぼしを防ぎ、Wayland socket dispatchの進行を確保。timeout0/有限/無限・遅いreleaseを検証。 |
| A6 | 同時進行slotごとにpool/cbを保持し、完了後のみ再利用。RESET_COMMAND_BUFFER＋Begin implicit resetにより記録の定常通信をBegin/Endへ。OOM/未受理/破棄/queue familyを検証。 |
| A7 | A3成立後にfenceごとのexternal workerを撤去。present workerはqueueごとの遅延起動を保持。Venus watchdogは遅延起動・idle睡眠を維持。consoleはprimary占有中の定期mutex取得をなくし、release/終了/テキスト更新で起床する。 |
| A8 | decoder reply/trailer/opcode/VkResultの検証を保持。切断・不正応答を待ち直しで成功扱いにしない。 |
| fence所属 | src/kern/fence.c→src/drivers/gpu/gpu-fence.c、include/kern/fence.h→include/drivers/gpu-fence.h、kernel_fence_*→drv_gpu_fence_*。kernは不透明fd/handle/refcount/poll/SCM_RIGHTSを所有し、GPU/fence型列挙を共通DRIVER＋driver ops識別へ。GPU共通層とfenceをbackend選択時だけbuildし、6platformの無GPU構成から除外する。 |

## 完了契約の具体化

既存hostのnonzero markerはdevice loss・proxy切断を正常retireへ流すため、そのまま成功signalへ使わない。isolated paired virglrendererでSTRICT_QUEUE能力を合意し、実VkFenceの成功だけをnonzero queue retireへ流す。native submit/host wait失敗・切断・未検証終了ではcontextをsticky failureとし、後続markerによるまとめretireも抑止する。異常は既存errorまたは独立した10秒watchdogでK fenceをERRORへ終端する。timeline0はdecoder通知のまま。stock/旧capabilityには新契約を広告しない。QEMU本体のstatus拡張を前提としない。

native submitより前にKで仕事・slot・generationを予約し、native応答後にcommitまたはcancelする契約を実装する。予約もwatchdog対象とし、U停止による監督の隙間を残さない。native仕事が出た可能性がある異常はtransport故障・DMA quarantineとして処理し、単なる予約解放にしない。確実な未受理のみrollback可。fd close/reuse、早い完了、reset世代、final close、callback drainを検証する。

## 検証と完了条件

1. 上記実装を限定normal/ASan・UBSan fixtureで確認。host側は実rendererコードに対する成功/失敗/追越し/切断の意味論fixture、guest側は権限・ops型識別・refcount・期限・通知・ringとWSI資源寿命を確認する。
2. make -j16の対象kernel/library/app build、Vulkan170公開API/Noct再生成、移動後の依存と6platformのGPU build選択を確認。aggregate make checkは実行しない。
3. private QEMU+i915/ANVで直接表示とWaylandの通常BLOB scanoutを画像oracle・実画面/traceで受入。GPU画像CPU readbackは診断用途と明示fallbackに限る。複数process、producer SIGSTOP/終了、renderer障害、有限回復、再open・console復帰を検証する。
4. queue capacity/DMA量、present資源作成数/通信数、監視thread数と同負荷の実測を記録し、未測定のFPS倍率を主張しない。差分の全適用coding-styleと公開契約をレビューして修正する。
5. 結果・失敗履歴・host/source/image hash・再現手順・制限をPhase資料へ保存し、GitHub Issues/Projectへ同期・読戻し後にcleared判定する。

## 実行境界

単一Phase/単一項目q311-i01で実行。720 active minutes見積、120 active minutesごとに成果と残件を点検。fixture120秒、build/転送1200秒、VM180秒を基本とし、各commandと失敗retryを有限化。同条件無変更retryは3回まで。p004やWS029を自動実行しない。

Guardrailとcoding-style全文に従う。HAL追加変更は個別承認が必要。guest dma-buf/DRM/SYNC_FD、一般Wayland/Toolkit・複数window/入力、callout、新HAL/native i915は追加しない。zwl/libwaylandの承認済みテストドライバ制約を維持する。private host/image/source転送・隔離renderer buildとGitHub同期は既存承認を使用し、system package/GDM/VFIO/rebootは変更しない。git add/commit/pushはユーザー担当。開始時点では実装・試験成功を主張しない。


## q311完了: GPU完了責任・driver fence・描画資源改善（2026-09-13）

WS014 p008 / q311-i01をcleared、q311をfinishedとする。active Queueなし。p007/q310の受入を保持し、WS014はincomplete、p001/p004はplanning、p004と別WS029 native i915は未queueのまま。

承認回答A1–A8とfence所属を実装した。fenceはdrv_gpuフレームワークへ移し、kernは不透明handle/fd/refcount/poll/SCM_RIGHTSを保持する。6platformでGPU共通層＋fenceをbackend選択時だけbuildし、GPUなしamd64実ELFでGPU symbol/object不在とgeneric handle/fd残存を確認した。

native投稿前のGPU_JOB予約から独立watchdogが監督し、strict paired rendererの実submission VkFence成功でKがexact generationを終端する。U-only/未commitのpendingをK内部で無期限に待たない。slotは最大64descriptor/32chain（28job＋4control）、外部fenceごとのworkerを撤去。acquireはmonotonic condition、present pool/cbは同時slotごとに再利用し、consoleは文字・所有権変更で起床する。局所表示エラーと全device故障も分離した。

最終5VMは同じkernel/base imageでPASS/QEMU exit0: direct-002 41.345秒、wayland-002 46.011秒、producer-stop-004 14.117秒、producer-exit-002 4.214秒、recovery-002 13.823秒。直接/Waylandの通常BLOB表示・独立画像oracle・複数process・再open/consoleを確認。SIGSTOP中fdを開いたproducerは9970msでDEVICE_LOST、renderer停止は10000msで故障通知後にchecked reset・新context往復を確認した。

K/U/transport/host/consoleの限定normal・sanitizer、170API/両ABI/Noct8file、対象build・規約・独立レビューを完了。U回収競合2件は修正前FAIL→修正後PASS。static analyzerの4警告は実callee/有効入力の前提と照合して記録し、全警告0とは扱わない。初期のbuild/harness失敗も保持する。

新libvulkanのVkDevice作成にはSTRICT_QUEUE対応のisolated paired rendererが必要。stock/旧pairは初期化で拒否する。host system packageとHALの追加変更なし。通常2秒sceneはp007再測定13frameからp00815frameだが、QEMU CPU時間は0.36秒から0.48秒の単発観測で、CPU削減や速度倍率は主張しない。一般Wayland/Toolkit、任意GPU間DMA、native i915、CTSは未受入。source/doc/patchのgit add/commit/pushはユーザー担当。


受入記録: [p008結果コメント](https://github.com/awemorris/zedBSD/issues/395#issuecomment-5652374702)。local/uncommittedの資料は plan/ws014/phase008/、Queue履歴は plan/history/queue-q311.md。

## review4への対応案（2026-09-13）

[レビュー回答と提案の全文](https://github.com/awemorris/zedBSD/issues/395#issuecomment-5652742665)を記録した。照合対象はユーザーcommit `cca12445`。R1は一時的容量不足とOOMを分け、Uの完了回収を止めない二段階admission＋device全体の容量通知を提案する。R3は予約/control/GPU実行の期限分離と、停止確認・DMA/descriptor退役を含むcontext単位の障害処理を提案する。R4はdirect acquireの通知接続、R5はprivate fence一括resetを専用負荷で測定、R6は内部寿命待ちとvalid usageを区別する。

R2のstock互換は能力別に実証してから有効化し、当面strictを維持する案。Vulkanはdevice loss時のfence SUCCESSを許すが、stockの非TIMEOUT retire/切断をそのまま資源再利用の証明にはしない。独自rendererの配布負担を記録した。

これは対応方針の提案であり、新実装・新試験の受入ではない。p008 cleared / q311 finished、WS014 incomplete、p004 planning/未queueを維持する。新Phase・Queueの作成と実行は行わず、既存BLOB表示・GPU framework所属fence・テストドライバ範囲を維持する。回答書はローカル `plan/ws014/gpu-stack-review4-response.md`（今回未commit）。git add/commit/pushはユーザー担当。

## q312開始: GPUレビュー対応とフレームワーク共通化（2026-09-13）

ユーザー指示により[WS014 p009](https://github.com/awemorris/zedBSD/issues/396)をq312-i01の単一Phaseとして実行する。承認範囲は[review4回答](https://github.com/awemorris/zedBSD/issues/395#issuecomment-5652742665)とGPU共通化の協議。job/fenceの状態・容量待機・期限・session故障と参照保持をdrv_gpuへ寄せ、backendは実資源の予約・投稿・完了と停止/DMA退役確認を担う。R1のU排他と容量通知、R3の期限/障害範囲、R4のdirectAcquire通知、R5のprivate fence reset再利用・測定、R6の寿命を改善する。

R2はstrictを当面維持し、stock互換の能力と退役条件を限定検証する。安全性が成立しなければstrictと具体的な不足・制約を記録する。context停止も能力と実確認が前提で、停止不能時はquarantine/全体resetを維持する。通常BLOB表示・GPU内共有・標準APIとzwl/libwaylandのテストドライバ範囲を保持する。

p008/q311のcleared/finishedを保持し、順序はp009 → p004 planning/未queue → 別WS029。720 active minutes見積・120分レビュー、有限fixture/build/VMで実装・受入する。追加HALや一般DE/native i915、git add/commit/push、system package/GDM/VFIO変更は含めない。private host/転送・隔離依存build・GitHub同期は既存承認を使用する。開始時点では新実装・試験の成功は主張しない。
