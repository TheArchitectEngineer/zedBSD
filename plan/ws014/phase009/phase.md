<!-- awesome-plan project=zedbsd record=ws014-p009 -->

# WS014 p009: GPUレビュー対応とフレームワーク共通化

<!-- awesome-plan-current:start -->
Status: in-progress
Phase disposition: normal
Parent: [WS014](https://github.com/awemorris/zedBSD/issues/15)
Queue: q312 active / q312-i01 in-progress (whole Phase)
Dependencies: cleared ws014-p008; accepted p007/p006/p005 and completed WS030
Next: ws014-p004 planning, not queued; native i915 stays separate WS029
<!-- awesome-plan-current:end -->

Combined ID: `ws014-p009`
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2, O4

## 目標と承認

通常の混雑でsubmitを失敗させず、GPUジョブの待機・期限・参照寿命・障害処理をdrv_gpuフレームワークで管理する。個別ドライバは実資源の予約/投稿、GPU完了、停止とDMA退役の確認を担い、同じ管理処理を複製しない。libvulkan側の排他とWSI資源再利用も接続し、実QEMUで描画・共有・複数processの進行を確認する。

ユーザー指示: 「では、実装をお願いします。独立したphaseで、レビューコメント対応とフレームワークでの共通化ですね。」。前提は[review4への回答](https://github.com/awemorris/zedBSD/issues/395#issuecomment-5652742665)と、その後の共通化の協議。回答書の「未着手」は回答作成時点の履歴であり、この実行承認を妨げない。p008/q311は当時の受入として保持する。

## 実装範囲

| 項目 | 実装と確認条件 |
| --- | --- |
| R1 | 一時的EAGAINと実OOMを分離。Uは完了回収して予約し、容量待機時はcontext/device/必要queue mutexを外す。再取得後は同期状態と世代を再検査。共通層はdevice全体/open固有の容量待機と通知・終了/割込み・公平性を扱い、backendはtry-reserveと実解放後の通知を担当する。POLLINの流用や無制限software queueは使わない。 |
| R2 | 当面strictを維持し、stock 1.1.0等の実コードと能力を限定検証。普通の描画/sparse/外部fence/OPAQUE共有を区別し、安全な資源再利用を証明できる範囲だけ有効化する。成立しなければstrictを維持し、具体不足・対応version・配布制約を記録することを回答済みの判断条件とする。 |
| R3 | 共通層へjob期限とsession失敗状態・通知・資源保持を移す。予約10秒、GPU実行60秒を既定案として設定と実効値照会を用意し、control監視は実通信の責任と分ける。optional context停止/回復の契約を定義し、Venusで独立sessionを継続できる安全な停止・pending descriptor退役を接続。停止確認不能はquarantine/全体resetへ進み、false ACKやerror通知だけでDMAを解放しない。 |
| R4 | direct Acquire自身が画像返却・U/K故障・topologyを通知で待ち、10ms定期起床を除く。状態検査/登録/再検査・複数waiter・fd寿命・lock順を保証。Waylandの有限dispatch間隔は保持可能。 |
| R5 | terminal private fenceの一括resetとreset済みcacheを実装・測定。pending/errorを再利用せず、再試行でcacheを増やさない。明示fenceの既存デモCPU値からNULL-fenceコストを推測しない。 |
| R6 | Vulkanのpending使用制約と、native完了後のhost marker内部寿命待ちを分ける。正常/故障/終了時の回収と有限終端を維持し、未停止資源を強制解放しない。 |
| 共通化 | 既存reserve/commit/cancelとdrv_gpu_completeを軸に、共通の状態/期限/待機/回復方針とbackendの実機操作を分離。timeline<64等のVenus制約をbackend検証へ移し、既存UAPI layoutを保持する。新契約は追加要求/能力/ops版で明示し、kernへGPU概念を増やさない。 |

## 停止・完了と所有権

論理的なjob ERRORと物理的なアクセス停止は別の事象。共通層は失敗したsessionの新規受付と待機を止め、backendは実停止・callback終了・descriptor退役を報告する。他session/mapping/scanoutが保持する共有allocationはその参照を維持する。個別停止に非対応または停止確認不能なbackendは全体回復へ進める。VenusのCTX_DESTROY OKだけを停止証拠にせず、必要なisolated renderer/QEMU契約を実装・検証する。

GPU期限はCOMMITからの単調時刻とし、予約期限もnative投稿前から監視する。RESERVEDでもnative受理済みの可能性を保持。容量待機timeout自体をdevice故障へ変換しない。frameworkはbackendの実停止能力を作り出せないため、試験では対応能力とfallbackの範囲を明示する。

## 検証と完了条件

1. 実productionコードを使う限定normal/ASan/UBSan fixtureで、global slot飽和、open64記録の未回収とobserver、別openの進行、通知競合、二段階admission、期限/故障範囲、callback/共有参照寿命を確認する。30回submitだけで枯渇再現と推定しない。
2. 新UAPIのILP32/LP64、GPU backendあり/なしのbuild、make -j16対象kernel/library/app、公開170APIと必要なNoct生成の整合を確認する。変更に無関係な試験は繰り返さない。
3. private host awe@10.0.10.25のisolated QEMU+i915/ANVで、直接/Waylandの通常BLOB scanout・独立画像oracle・再open/console、複数process負荷、長いjob/個別context障害中の独立session継続、共通transport障害と全体回復を確認する。native GPUの実停止とhost fixtureでの意味論検証を区別する。
4. NULL/明示fence負荷のreset往復、cache数、待機起床、CPU/throughputを同条件で有限測定し、観測と因果を区別して記録する。故障注入のevent未設定を正常なVulkan進行例として扱わない。
5. code style全文の適用と独立レビュー、失敗/修正理由、source/host/image hash、再現手順、残る能力制限を資料へ保存。GitHub Issues/Projectに結果と状態を同期・読戻し後にPhaseを判定する。

## 実行境界

q312-i01の一項目/単一Phase。720 active minutes見積、120分ごとに残件と成果を点検。fixture120秒、build/transfer1200秒、VM180秒を基本に有限化し、試験設計上長いjob試験は明示した有限VM期限を使う。同条件無変更retryは3回まで。p004や別WS029を自動実行しない。

Guardrailとplan/coding-style.md全文に従う。HAL追加変更は個別の具体許可が必要。通常BLOB/GPU内共有と標準API、承認済みzwl/libwaylandのテストドライバ範囲を維持する。一般DE/toolkit/native i915/guest DRM/dma-buf/汎用kern fenceは追加しない。既存private hostとimage/source転送、isolated host依存build、GitHub同期の承認を使用。system package/GDM/VFIO/reboot、git add/commit/push、aggregate make checkは含めない。
