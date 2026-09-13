<!-- awesome-plan project=zedbsd record=queue -->

# Queue q312: GPUレビュー対応とフレームワーク共通化

<!-- awesome-plan-current:start -->
Status: active
Active Queue: q312
Executor: q312-root
Item: q312-i01 in-progress (whole ws014-p009)
Previous Queue: q311 finished / ws014-p008 cleared
<!-- awesome-plan-current:end -->

Authorization: current user: では、実装をお願いします。独立したphaseで、レビューコメント対応とフレームワークでの共通化ですね。
Start UTC: 2026-09-13T10:53:28.348806+00:00
Approved response SHA256: `47aaa27da92d331fff79ac9093186ed0f57f3c2a40bb8959a71f3447c691c453`
Implementation baseline: user commit `cca124456908acdb39512bc5d339e1a6f0e61a7e`。review4の同期済みlocal計画差分を保持。
Timebox: 720 active minutes estimate; review every120 active minutes. fixture120秒、build/転送1200秒、VM180秒を基本に必要な長時間GPU試験だけ有限期限を明記。同条件無変更retry3回まで。

| Order | Attempt | Phase | Status | Scope |
| --- | --- | --- | --- | --- |
| 1 | q312-i01 | [ws014-p009](https://github.com/awemorris/zedBSD/issues/396) | in-progress | review4 R1–R6とGPUフレームワーク共通化、限定試験・実QEMU・規約全文確認 |

## 依存と実行境界

p008 cleared → q312-i01/p009 → p004 planning/未queue → 別WS029 native i915。後続は自動実行しない。全体scopeと受入条件はp009本文に固定し、過去のp008/q311を再開しない。

q311履歴はlocal plan/history/queue-q311.md、直前Queue全文はローカル同期journalへ保存。[Guardrail](https://github.com/awemorris/zedBSD/issues/363)、plan/coding-style.md全文、make -j16対象buildを使用。新HALの個別承認、private host転送の既存許可、git公開はユーザー担当という境界を維持。

## q312開始: GPUレビュー対応とフレームワーク共通化（2026-09-13）

ユーザー指示により[WS014 p009](https://github.com/awemorris/zedBSD/issues/396)をq312-i01の単一Phaseとして実行する。承認範囲は[review4回答](https://github.com/awemorris/zedBSD/issues/395#issuecomment-5652742665)とGPU共通化の協議。job/fenceの状態・容量待機・期限・session故障と参照保持をdrv_gpuへ寄せ、backendは実資源の予約・投稿・完了と停止/DMA退役確認を担う。R1のU排他と容量通知、R3の期限/障害範囲、R4のdirectAcquire通知、R5のprivate fence reset再利用・測定、R6の寿命を改善する。

R2はstrictを当面維持し、stock互換の能力と退役条件を限定検証する。安全性が成立しなければstrictと具体的な不足・制約を記録する。context停止も能力と実確認が前提で、停止不能時はquarantine/全体resetを維持する。通常BLOB表示・GPU内共有・標準APIとzwl/libwaylandのテストドライバ範囲を保持する。

p008/q311のcleared/finishedを保持し、順序はp009 → p004 planning/未queue → 別WS029。720 active minutes見積・120分レビュー、有限fixture/build/VMで実装・受入する。追加HALや一般DE/native i915、git add/commit/push、system package/GDM/VFIO変更は含めない。private host/転送・隔離依存build・GitHub同期は既存承認を使用する。開始時点では新実装・試験の成功は主張しない。
