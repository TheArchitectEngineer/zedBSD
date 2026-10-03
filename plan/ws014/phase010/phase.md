<!-- awesome-plan project=zedbsd record=ws014-p010 -->

# WS014 p010: GPU監督の共通化仕上げと局所隔離

<!-- awesome-plan-current:start -->
Status: cleared
Phase disposition: normal
Parent: [WS014](https://github.com/awemorris/zedBSD/issues/15)
Queue: q313 finished / q313-i01 cleared (whole Phase)
Execution: self-review D1-D4/B3-B6, Venus-to-framework migration and per-context isolation accepted
Dependencies: cleared ws014-p009; accepted p008/p007/p006/p005 and completed WS030
Next: ws014-p004 planning, not queued; native i915 stays separate WS029
<!-- awesome-plan-current:end -->

Combined ID: `ws014-p010`
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2, O4

## 目標と承認

p009の自己レビュー（[gpu-stack-review5.md](../gpu-stack-review5.md)）で見つかった要修正点D1/D2と改善点D3/D4/B3–B6を解消し、Venus backendに残る汎用の判断（停止shortcut、fault cancel後のsession失敗、予約の後始末、停止状態の複製、control期限の定数）をdrv_gpuフレームワークへ移す。段階的に進め、各段階を限定fixtureで固定してから次へ進む。最終的に実QEMUで、正当な長いjobを持つprocessの終了が他sessionを落とさないこと、commit済みjobがproducer終了後も完了しfenceが成功すること、停止を確認できないcontextがdevice全体ではなくそのsessionだけを失うことを確認する。

ユーザー指示: 「では、セルフレビュー項目、GPUドライバフレームワークへの移管について、段階的にリファクタリングして達成する計画をphaseとして書いていただき、queueに入れて実行してください。」。前提はレビュー5本文と、その後の「framework側に入れられるか」「Venus側から移せる処理」の回答。既存UAPIのlayout・ioctl番号・sizeは変えない。内部opsは版9へ進め、Venus以外のbackendが無いことを前提に旧版を残さない。

## 段階と実装範囲

各段階は「共通層の変更 → backend/fixtureの追従 → 限定fixture PASS → 次段階」の順で進める。段階の途中で実QEMUへは進まない。

| 段階 | 項目 | 実装 | 固定する性質 |
| --- | --- | --- | --- |
| S1 期限 | D1, B3 | 停止期限（`CONFIG_GPU_JOB_STOP_MS`）はsession失敗時ではなく、その sessionのbackend所有のCOMMITTED/RESERVED jobが全て退役または期限切れになり、かつ admitted ioctl/job actionが退出して`stop_begin`を実際に呼んだ時点から数える。それまでは各jobの予約/実行期限だけが監督する。 | 既定60秒の範囲内で走る15秒jobを持つprocessの終了が10秒後にdevice全体を落とさない。期限切れで失敗したsessionは従来どおり停止10秒。busy中に停止期限が進まない。 |
| S2 close | D2 | final closeのENODEV失敗では、COMMITTEDでbackend所有のjobを終端せず監督を続け、完了時に`drv_gpu_complete`が通常どおり記録とbound fenceへ結果（成功を含む）を公開する。`gpu_fence_retire`は未完了jobのbindingを残す。RESERVED（未commit）は予約期限まで保持し、期限切れかquiesce確認で終端する。予約/実行期限やfaultによる失敗は従来どおり全pendingをERRORにする。 | 最後のframeをcommitして終了したclientのfenceがcompositor側で成功する。event待ちの未完了jobは実行期限でERROR。closeは対象jobの退役まで待つ（Linuxと異なりexitが遅れる制約として記録）。 |
| S3 移管 | 移管1–5 | (1) 共通層がopenごとの`native_submitted`（command/submit/job commitの直前に立てる）と backend所有記録の有無で「native仕事なし」を判定し、その場合は`stop_begin`/`stop_poll`を呼ばずFINISHEDにする。Venusの`native_commands_submitted`と`drv_venus_transport_idle`のshortcutを除く。(2) fault cancel成功後のsession失敗を共通層（`gpu_job_action_ioctl`）で行い、Venusの`drv_gpu_report_session_error`呼出しを除く。(3) `stop_poll`が0を返した後、共通層が残るRESERVED予約へ`jobs->cancel(fault=0)`を呼んで回収し、Venus quiesceのRESERVED終端loopを除く。`stop_poll`の契約を「native access停止と投稿済みcallbackの退役」に限定する。(4) Venusの`stopping`を除き（共通層のsticky errorが新規受付を拒む）、`quiesced`/`stop_request`だけ残す。(5) control通信の10秒watchdogを`CONFIG_GPU_CONTROL_MS`（既定10000、make/menuconfig）にし、Venus transportがそれを使う。 | Venusの`stop_begin`はquiesce要求の投稿、`stop_poll`はACK検証、`cancel`は予約返却だけになる。判断はすべて共通層。 |
| S4 監視 | D4, B6 | monitor起床を「job期限の登録/変更」「停止中sessionのbusy解除・job action退出」「unregister」「stop対象のcallback退役」に限定し、健全なsessionのioctl終了と容量変化では起こさない。未使用の`drv_gpu_recovery_ready`を`gpu.c`/`gpu.h`から除く。 | 通常描画中にmonitorが全session×64recordを走査しない。fixtureでwaitq sequenceの不変を確認。 |
| S5 隔離 | D3, B4, B5 | 停止期限超過または`stop_begin`/`stop_poll`の失敗を、device全体のfaultではなくsessionの隔離（`GPU_STOP_QUARANTINED`）にする。新しい任意op `recovery->isolate(device, session)`（ops版9）でbackendはそのcontextの投稿済みslotを隔離し、以後そのcontextへ命令を送らない。隔離sessionのcloseはdrain/resource_destroy/closeを呼ばず、記録storageを保持したままopen listから外して隔離listへ移す。backend callbackが全て退役（`backend_owned`==0）し、かつ「device fault後」または「他の生きたopen/shareが無い新規openでのchecked reset成功後」に隔離sessionを解放する（resetは `error==0` でも隔離sessionがあれば同じgateで実行する）。`isolate`が無いbackendは従来どおり全体fault。Venus: `venus_queue_collect`はsupervised markerのエラー応答でtransport全体を失敗させず、当該requestのerrorとして`stop_poll`がEIOを返す（B4）。隔離contextのCTX_DESTROY/display退役は送らず、resetで一括退役する。drainの無期限待ちは隔離sessionでは呼ばれない（B5）。 | 1つのrogue contextがGPUを全員から奪わない。隔離で失った容量は次のidle時のchecked resetで回収する。容量が回収されるまでの間の枯渇は制限として記録し、自動escalationは今回入れない。 |

## 停止・完了と所有権（p009からの変更点）

- 論理ERRORと物理停止の分離、`backend_owned`の保持、late successがERRORを上書きしない規則、共有allocation/mapping/scanoutの独立参照は変えない。
- close時のENODEV失敗だけがCOMMITTED jobを監督継続する。その他の失敗（ETIMEDOUT/EIO/device fault）はp009どおり全pendingをERRORにする。
- 停止期限の起点は`stop_begin`の実呼出し。`stop_begin`は、当該sessionにbackend所有の未終端jobが無く、admitted ioctl/job actionが退出した後にのみ呼ぶ。
- `stop_poll`==0 は「native accessの停止と投稿済みcallbackの退役」を意味し、未投稿の予約は共通層が`cancel(0)`で回収する。
- 隔離（S5）はdevice faultと別。隔離sessionは新規受付拒否・記録保持・backend資源保持で、他sessionは通常どおり進む。deviceの`error`は0のまま。

## 検証と完了条件

1. 限定fixture（実productionコード、通常＋ASan/UBSan）: `gpu-supervision`に S1（close時の15秒job・既定policyで全体faultなし、停止10秒の起点）、S2（COMMITTED継続とfence成功、RESERVEDの予約期限ERROR、他失敗の全終端）、S3（native_submittedによるshortcut、fault cancel後のsession失敗、RESERVED回収のcancel呼出し）、S4（健全ioctl後のmonitor waitq sequence不変、recovery_ready不在）、S5（隔離、隔離sessionのclose、idle openでのreset回収、fault後の解放、unregister）を追加。`venus-backend`/`venus-transport`にS3の除去とS5のisolate/collect/quarantineを追加。既存 gpu-job/fence/fence-close/fence-reuse/sharing/topology/scanout/placement/handle-fd/uapi-layout と ws030 libvulkan sync/notify/external-fence fixtureを回帰。
2. build: `make -j16` 対象kernel/library/appを `config-wayland-amd64.mk` と `config-wayland-context-timeout-amd64.mk` で。`run-gpu-build-selection-test.py` と GPUなしamd64実buildでGPU symbol不在を再確認。UAPI layout fixtureで既存要求の配置不変を確認。
3. 実QEMU（private host awe@10.0.10.25、isolated q312-quiesce / q312-quiesce-delay pair、同一最終artifact）: direct（vkdemo lifecycle）、wayland（lifecycle）、submit-load、completion-delay、context-timeout、producer-stop、recovery の7件を回帰。新規に `producer-exit-delayed`（delay pair、既定policy: 15秒遅延のcommit済みjobを持つproducerが終了し、consumerのfenceが約15秒でSUCCESS、consumer自身の後続submitが成功、device faultなし）と `producer-exit-hang`（event待ちjobを持つproducerが終了、consumerは実行期限60秒でDEVICE_LOST、consumerは停止期限後も自devicesで作業継続、全process終了後の新規openで回収reset、その後の通常gpu-fence-testがPASS。resetが成立しない場合は隔離継続と失敗内容を記録）。既存 `producer-exit` は「実行期限でERROR」に期待値を更新して実行。VM期限はhang系だけ300秒、他は180秒。
4. 資料: `plan/ws014/phase010/` に契約差分（stop期限起点、close継続、stop_poll契約、isolate、CONFIG_GPU_CONTROL_MS）、各段階のfixture結果、build、実QEMU結果、失敗履歴、hash、再現手順。`gpu.h`の契約コメントとp009資料への差分注記。code style全文の適用とdiff check。GitHub Issues/Projectへ結果を同期し読戻し後にPhaseを判定する。

## 実行境界

q313-i01の一項目/単一Phase。720 active minutes見積、120分ごとに残件と成果を点検。fixture120秒、build/転送1200秒、VM180秒（hang系300秒）を基本に有限化。同条件無変更retryは3回まで。S5でresetによる回収が成立しない場合は隔離までを受入とし、回収は制限として記録する。p004や別WS029を自動実行しない。

Guardrailとplan/coding-style.md全文に従う。HAL追加変更なし。UAPI layout/ioctl番号/size不変、libvulkanの変更は期待値の追従に限る（producer終了後のfence成功はU側の変更不要）。stock互換（R2）は対象外。既存private hostとimage/source転送、isolated host依存build、GitHub同期の承認を使用。system package/GDM/VFIO/reboot、git add/commit/push、aggregate make checkは含めない。

## checkpoint 001: S1/S2/S4/B6（2026-09-14）

S1（停止期限の起点をstop_begin実呼出しへ、D1/B3）、S2（close時のcommit済みjob監督継続、D2）、S4部分（monitor起床の限定）、B6（`drv_gpu_recovery_ready`除去）を実装した。実productionコードを含むGPU core fixture 14種を通常＋ASan/UBSanでPASS、`config-wayland-amd64.mk`の実kernel build（`amd64 vmunix check: PASS`）と`git diff --check`を確認した。gpu-jobにcommitted-job-survives-close（fence SIGNALED）と未commit予約のclose終端の2 sub-caseを追加した。詳細と失敗履歴は[results.md](results.md)、証拠は[core-verification/checkpoint-s1s2s4b6.json](core-verification/checkpoint-s1s2s4b6.json)。

残り: S3（Venus→framework移管）、S5（session隔離・ops版9、D3/B4/B5）、実QEMU受入。p010はin-progressのままclearedにしない。source/patchのgit add/commit/pushはユーザーが行う。

## q313完了: GPU監督の共通化仕上げと局所隔離（2026-09-14）

WS014 p010 / q313-i01をcleared、q313をfinishedとする。active Queueなし。p009/q312の受入を保持し、WS014はincomplete、p001/p004はplanning、p004と別WS029 native i915は未queueのまま。

S1: 停止期限の起点を`stop_begin`実呼出しへ移し、実行期限内のjobが残る間は停止しない。S2: graceful closeはcommit済みjobを終端せず実結果をfenceへ公開する。S3: native仕事の有無判定、fault cancel後のsession失敗、RESERVED回収、admission拒否、control期限定数を共通層へ移した。S4/B6: monitor起床の限定と`drv_gpu_recovery_ready`除去。S5: `recovery->isolate`（ops版9）で停止未確認contextをsession隔離し、device全体は継続、idle時のchecked resetで回収。libvulkanは自contextのPOLLERRだけでdevice lossをlatchする。UAPI/HALは不変。

最終10VMは同一最終sourceの2 build（既定policy・短縮policy）でPASS/QEMU exit0: exit-delayed-003 19.698秒（producer終了後にconsumer fenceが14750 msでSUCCESS）、exit-hang-006 28.178秒（8000 msでDEVICE_LOST、context隔離、peer継続、idle openでreset回収と通常試験PASS）、producer-exit-002 24.447秒（hostの実結果を公開）、direct-002 41.796秒、wayland-002 47.258秒、submit-load-002 10.886秒、completion-delay-002 25.213秒、context-timeout-002 25.296秒、producer-stop-002 19.341秒、recovery-002 13.926秒。限定fixture（GPU core 10種、Venus 5種、libvulkan 5種、build selection）を通常＋sanitizerでPASS。失敗履歴（exit-hang-001のU側latch、exit-hang-004のreset中open拒否、producer-exit-001の旧期待値）を保持し、初回成功とは扱わない。

隔離で失った容量はidle時のresetまで戻らず自動escalationは無い。隔離contextの表示状態はresetまで残る。closeはcommit済みjobの退役まで待つ。git add/commit/pushはユーザー担当。受入記録: [p010結果コメント](https://github.com/awemorris/zedBSD/issues/397#issuecomment-5655171051)。

受入記録: local `plan/ws014/phase010/results.md`、`runtime-verification/summary.json`、`gpu-supervision-contract.md`。
