# Awesome Plan records

Follow the repository-root AGENTS.md, `config.md`, and the pinned Awesome Plan
skill. `guardrail.md` is the current project policy index. Old MWP-Q instructions
are archived and do not override this setup.

Keep wsXXX/phaseYYY paths and combined IDs wsXXX-pYYY. No descriptive suffixes,
renumbering, speculative Phases, blanket re-opening, or new Queue on migration.
GitHub Issues own published meaning; Project fields and local files are projections
and durable working state. Journal before changes and reconcile human edits.
Do not publish credentials or unavailable local paths as accessible evidence.

Before reporting synchronization complete, apply the lifecycle, current-state,
Project access/linkage and published-reference checks in `tools/README.md`.
The 2026-09-11 user-reported manual repairs supersede old publication snapshots;
reconcile them before replaying any pending outbox operation. Board publication
and repository publication are separate outcomes; never imply both are complete.


## WSの単一目標と終了後の扱い（2026-09-12ユーザー指示）

WSは一つの具体的な到達目標を持つ。目標を達成したWS、またはユーザーが終了したWSは再利用・再開して別の目標を追加しない。似た領域だからという理由で一つのWSへまとめない。機種対応などの上位分類・到達点はMGが担い、インストーラ実機動作、PowerPC移植などは別のWSを作る。
一つの目標に必要な依存作業をPhaseへ分解することは可能だが、独立した別目標をPhaseとして混ぜない。WS終了時は子Phaseを全件照合し、未完了は完了に改変せず、ユーザー指定の保留先または別WSへ引き継いで元Phaseを終了する。旧ID、結果、転送先を残す。今回WS003は終了・再利用禁止、PPC移植はWS027へ、その他の未完了はFuture Workへ移す。

Current PPC plan: plan/ws027/ws.md. Old WS003 references are historical; follow new WS027 p001-p007.

## q311開始: GPU完了責任・fence所属と描画資源の改善（2026-09-13）

ユーザーがレビュー回答を承認し、独立Phaseの作成・実行を指示した。[WS014 p008](https://github.com/awemorris/zedBSD/issues/395)を単一項目q311-i01で実行する。p007/q310のcleared/finishedを保持し、p008 → p004 planning/未queue → 別WS029 native i915の順とする。

A1の局所表示エラー分離、A3のGPUドライバによるfence終端、A4のqueue容量、A5のacquire待機、A6の同時進行slot別pool/cb再利用、A7のexternal worker撤去・console通知、A2/A8の検証補強を含む。fenceはdrv_gpuフレームワークへ移し、kernには不透明handle/fd/refcount/poll/SCM_RIGHTSを残す。共通DRIVER分類＋ops識別とGPU組込時だけのbuildを用いる。

isolated paired rendererのSTRICT_QUEUE能力を合意し、実GPU成功だけを正常retireへ流す。失敗はsticky化して後続まとめretireを抑止し、K watchdogでERROR終端する。native投入前の予約も期限管理し、U停止による未監督仕事を残さない。通常BLOB表示と標準OPAQUE_FDを維持する。

720 active minutes見積・120分レビュー、有限fixture/build/VMで完了まで進める。HAL追加変更・一般Wayland・native i915・git add/commit/pushは含めない。既存private host/転送とGitHub同期の承認を使用する。開始時点で新実装や検証成功は主張しない。詳細はp008本文と承認回答コメント、local plan/ws014/phase008/に残す。


## q311完了: GPU完了責任・driver fence・描画資源改善（2026-09-13）

WS014 p008 / q311-i01をcleared、q311をfinishedとする。active Queueなし。p007/q310の受入を保持し、WS014はincomplete、p001/p004はplanning、p004と別WS029 native i915は未queueのまま。

承認回答A1–A8とfence所属を実装した。fenceはdrv_gpuフレームワークへ移し、kernは不透明handle/fd/refcount/poll/SCM_RIGHTSを保持する。6platformでGPU共通層＋fenceをbackend選択時だけbuildし、GPUなしamd64実ELFでGPU symbol/object不在とgeneric handle/fd残存を確認した。

native投稿前のGPU_JOB予約から独立watchdogが監督し、strict paired rendererの実submission VkFence成功でKがexact generationを終端する。U-only/未commitのpendingをK内部で無期限に待たない。slotは最大64descriptor/32chain（28job＋4control）、外部fenceごとのworkerを撤去。acquireはmonotonic condition、present pool/cbは同時slotごとに再利用し、consoleは文字・所有権変更で起床する。局所表示エラーと全device故障も分離した。

最終5VMは同じkernel/base imageでPASS/QEMU exit0: direct-002 41.345秒、wayland-002 46.011秒、producer-stop-004 14.117秒、producer-exit-002 4.214秒、recovery-002 13.823秒。直接/Waylandの通常BLOB表示・独立画像oracle・複数process・再open/consoleを確認。SIGSTOP中fdを開いたproducerは9970msでDEVICE_LOST、renderer停止は10000msで故障通知後にchecked reset・新context往復を確認した。

K/U/transport/host/consoleの限定normal・sanitizer、170API/両ABI/Noct8file、対象build・規約・独立レビューを完了。U回収競合2件は修正前FAIL→修正後PASS。static analyzerの4警告は実callee/有効入力の前提と照合して記録し、全警告0とは扱わない。初期のbuild/harness失敗も保持する。

新libvulkanのVkDevice作成にはSTRICT_QUEUE対応のisolated paired rendererが必要。stock/旧pairは初期化で拒否する。host system packageとHALの追加変更なし。通常2秒sceneはp007再測定13frameからp00815frameだが、QEMU CPU時間は0.36秒から0.48秒の単発観測で、CPU削減や速度倍率は主張しない。一般Wayland/Toolkit、任意GPU間DMA、native i915、CTSは未受入。source/doc/patchのgit add/commit/pushはユーザー担当。


受入記録: [p008結果コメント](https://github.com/awemorris/zedBSD/issues/395#issuecomment-5652374702)。local/uncommittedの資料は plan/ws014/phase008/、Queue履歴は plan/history/queue-q311.md。

## q312開始: GPUレビュー対応とフレームワーク共通化（2026-09-13）

ユーザー指示により[WS014 p009](https://github.com/awemorris/zedBSD/issues/396)をq312-i01の単一Phaseとして実行する。承認範囲は[review4回答](https://github.com/awemorris/zedBSD/issues/395#issuecomment-5652742665)とGPU共通化の協議。job/fenceの状態・容量待機・期限・session故障と参照保持をdrv_gpuへ寄せ、backendは実資源の予約・投稿・完了と停止/DMA退役確認を担う。R1のU排他と容量通知、R3の期限/障害範囲、R4のdirectAcquire通知、R5のprivate fence reset再利用・測定、R6の寿命を改善する。

R2はstrictを当面維持し、stock互換の能力と退役条件を限定検証する。安全性が成立しなければstrictと具体的な不足・制約を記録する。context停止も能力と実確認が前提で、停止不能時はquarantine/全体resetを維持する。通常BLOB表示・GPU内共有・標準APIとzwl/libwaylandのテストドライバ範囲を保持する。

p008/q311のcleared/finishedを保持し、順序はp009 → p004 planning/未queue → 別WS029。720 active minutes見積・120分レビュー、有限fixture/build/VMで実装・受入する。追加HALや一般DE/native i915、git add/commit/push、system package/GDM/VFIO変更は含めない。private host/転送・隔離依存build・GitHub同期は既存承認を使用する。開始時点では新実装・試験の成功は主張しない。


## q312完了: GPUレビュー対応とフレームワーク共通化（2026-09-13）

WS014 p009 / q312-i01をcleared、q312をfinishedとする。active Queueなし。p008/q311の受入を保持し、WS014はincomplete、p001/p004はplanning、p004と別WS029 native i915は未queueのまま。

R1: 容量不足をOOMにせず、`GPU_JOB_CAPACITY` QUERY/WAIT（ioctl 37）と`GPU_JOB_POLICY`（38）を追加。libvulkanはnative準備→QUERY→回収→非待機RESERVEとし、EAGAINではqueue/device/context mutexを外して待つ。R3: 予約10秒・実行60秒・停止10秒をmake/menuconfigの設定と実効値照会にし、session単位のsticky errorと`drv_gpu_recovery_ops`（stop_begin/stop_poll/fault/reset）でcontext単位の停止確認を導入。Venusはflags7のquiescence契約（全native VkDeviceWaitIdleを確認したCPU0 ACK）を持つisolated pairで実停止を証明し、確認不能なら従来のquarantine/全体resetへ進む。R4: direct acquireは画像返却・故障・topologyをwaiter固有pipeと`ppoll`で待ち、10 ms周期起床を除いた。R5: terminal private fenceを最大64本ずつ一括resetしREADYを再利用。R2はstrict（flags7）維持、stock 1.1.0の情報欠落をstock-compat/で記録。fenceとjob監督はdrv_gpu内に保持し、汎用kernへの追加なし。

最終8VMは同一最終artifactでPASS/QEMU exit0: direct-003 41.935秒、wayland-002 47.342秒、submit-load-005 11.833秒（2process 576 submit、OOM 0）、completion-delay-003 25.316秒（15秒遅延完了、peer継続）、context-timeout-003 25.138秒（短縮期限でDEVICE_LOST、peer継続）、producer-stop-002 19.438秒（SIGSTOP中に7770 msで終端）、producer-exit-002 4.165秒、recovery-002 14.051秒（10000 ms watchdog後checked reset）。限定fixture（K 12 suite、U 10+5 job、transport/host/console）、170 API/両ABI/Noct、6platform×GPU有無のbuild入力、GPUなしamd64実ELF、規約確認を完了。失敗履歴（submit-load-001の能力bit漏れ、producer-stop-001の旧期待値、recovery-001のerrno期待値）を保持し、初回成功とは扱わない。

新libvulkanのVkDevice作成にはflags7（OPAQUE+STRICT+QUIESCE）のisolated paired rendererが必要で、stock/旧pairは初期化で拒否する。実行期限60秒は正当な長時間computeにも適用される。任意GPU間DMA、native i915、一般Wayland/toolkit、CTSは未受入。HAL・host system package・git add/commit/pushは行っていない。

受入記録: local `plan/ws014/phase009/results.md`、`runtime-verification/summary.json`。GitHub Issues/Projectへの同期はユーザー確認後に行う。
