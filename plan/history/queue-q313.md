<!-- archived Queue cycle q313; standing Board remains issue #362 -->

# Queue q313: GPU監督の共通化仕上げと局所隔離

<!-- awesome-plan-current:start -->
Status: finished
Active Queue: none
Last Queue: q313
Executor: none
Item: q313-i01 cleared (whole ws014-p010)
Previous Queue: q312 finished / ws014-p009 cleared
<!-- awesome-plan-current:end -->

Authorization: current user: では、セルフレビュー項目、GPUドライバフレームワークへの移管について、段階的にリファクタリングして達成する計画をphaseとして書いていただき、queueに入れて実行してください。
Start UTC: 2026-09-13T15:59:50.484469+00:00
Review input SHA256: `05435c48bdfd9168184fc026a1a628ba5d392c91522fa0556b9ed662fa2a200c` (plan/ws014/gpu-stack-review5.md)
Implementation baseline: user commit `7bc3b972` (p009 final tree, working tree clean at start).
Timebox: 720 active minutes estimate; review every 120 active minutes. fixture120秒、build/転送1200秒、VM180秒（hang系300秒）。同条件無変更retry3回まで。

| Order | Attempt | Phase | Status | Scope |
| --- | --- | --- | --- | --- |
| 1 | q313-i01 | [ws014-p010](https://github.com/awemorris/zedBSD/issues/397) | cleared | S1期限・S2 close・S3移管・S4監視・S5隔離、限定fixture・build・実QEMU9件・規約全文確認 |

## 依存と実行境界

p009 cleared → q313-i01/p010 → p004 planning/未queue → 別WS029 native i915。後続は自動実行しない。全体scopeと受入条件はp010本文に固定し、過去のp009/q312を再開しない。

q312履歴はlocal plan/history/queue-q312.md。[Guardrail](https://github.com/awemorris/zedBSD/issues/363)、plan/coding-style.md全文、make -j16対象buildを使用。新HALの個別承認、private host転送の既存許可、git公開はユーザー担当という境界を維持。GitHub公開（Issue作成・本文・Project）は保留中で、plan/.sync/drafts/q313-start/summary.md に記録。

## q313開始: GPU監督の共通化仕上げと局所隔離（2026-09-14）

ユーザー指示により[WS014 p010](https://github.com/awemorris/zedBSD/issues/397)をq313-i01の単一Phaseとして実行する。前提はp009の自己レビュー（plan/ws014/gpu-stack-review5.md、SHA256 `05435c48bdfd9168184fc026a1a628ba5d392c91522fa0556b9ed662fa2a200c`）と、その後のframework側実装可否・Venusから移せる処理の回答。S1 停止期限の起点をstop_begin実呼出しへ（D1/B3）、S2 close時のcommit済みjob監督継続（D2）、S3 停止shortcut・fault cancel後のsession失敗・RESERVED回収・停止flag・control期限定数のframework移管、S4 monitor起床の限定とrecovery_ready除去（D4/B6）、S5 停止未確認contextのsession隔離とidle時reset回収（D3/B4/B5）の順に、各段階を限定fixtureで固定してから進める。

既存UAPIのlayout/ioctl番号/sizeは変えず、内部opsは版9へ進める。実QEMUは既存7件の回帰に加え、producer-exit-delayed（既定policyで15秒jobを持つproducer終了後にconsumer fenceが成功）とproducer-exit-hang（event待ちjobで実行期限DEVICE_LOST、他sessionの継続、idle時のreset回収）を新設し、producer-exitは実行期限ERRORへ期待値を更新する。

p009/q312のcleared/finishedを保持し、順序はp010 → p004 planning/未queue → 別WS029。720 active minutes見積・120分レビュー、fixture120秒/build・転送1200秒/VM180秒（hang系300秒）で有限化。追加HAL、stock互換、一般DE/native i915、git add/commit/push、system package/GDM/VFIO変更は含めない。private host/転送・隔離依存build・GitHub同期は既存承認を使用する。開始時点では新実装・試験の成功は主張しない。GitHub Issues/Projectへのq312完了とq313開始の公開は、このsessionでは自動承認レビューにより保留され、outbox/draftsに記録した。

## q313完了: GPU監督の共通化仕上げと局所隔離（2026-09-14）

WS014 p010 / q313-i01をcleared、q313をfinishedとする。active Queueなし。p009/q312の受入を保持し、WS014はincomplete、p001/p004はplanning、p004と別WS029 native i915は未queueのまま。

S1: 停止期限の起点を`stop_begin`実呼出しへ移し、実行期限内のjobが残る間は停止しない。S2: graceful closeはcommit済みjobを終端せず実結果をfenceへ公開する。S3: native仕事の有無判定、fault cancel後のsession失敗、RESERVED回収、admission拒否、control期限定数を共通層へ移した。S4/B6: monitor起床の限定と`drv_gpu_recovery_ready`除去。S5: `recovery->isolate`（ops版9）で停止未確認contextをsession隔離し、device全体は継続、idle時のchecked resetで回収。libvulkanは自contextのPOLLERRだけでdevice lossをlatchする。UAPI/HALは不変。

最終10VMは同一最終sourceの2 build（既定policy・短縮policy）でPASS/QEMU exit0: exit-delayed-003 19.698秒（producer終了後にconsumer fenceが14750 msでSUCCESS）、exit-hang-006 28.178秒（8000 msでDEVICE_LOST、context隔離、peer継続、idle openでreset回収と通常試験PASS）、producer-exit-002 24.447秒（hostの実結果を公開）、direct-002 41.796秒、wayland-002 47.258秒、submit-load-002 10.886秒、completion-delay-002 25.213秒、context-timeout-002 25.296秒、producer-stop-002 19.341秒、recovery-002 13.926秒。限定fixture（GPU core 10種、Venus 5種、libvulkan 5種、build selection）を通常＋sanitizerでPASS。失敗履歴（exit-hang-001のU側latch、exit-hang-004のreset中open拒否、producer-exit-001の旧期待値）を保持し、初回成功とは扱わない。

隔離で失った容量はidle時のresetまで戻らず自動escalationは無い。隔離contextの表示状態はresetまで残る。closeはcommit済みjobの退役まで待つ。git add/commit/pushはユーザー担当。受入記録: [p010結果コメント](https://github.com/awemorris/zedBSD/issues/397#issuecomment-5655171051)。

受入記録: local `plan/ws014/phase010/results.md`、`runtime-verification/summary.json`、`gpu-supervision-contract.md`。
