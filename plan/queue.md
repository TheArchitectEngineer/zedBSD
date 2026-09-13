<!-- awesome-plan project=zedbsd record=queue -->

# Queue q311: GPU完了責任・fence所属と描画資源の改善

<!-- awesome-plan-current:start -->
Status: finished
Active Queue: none
Last Queue: q311
Executor: none
Item: q311-i01 cleared (whole ws014-p008)
Previous Queue: q310 finished / ws014-p007 cleared
<!-- awesome-plan-current:end -->

Authorization: current user: レビューしました。問題ないです。独立したphaseとして定義し、実行してください。
Start UTC: 2026-09-13T08:09:38.959846+00:00
Approved response SHA256: `eda89136ad50f98b9ba97b4fc9971c424ea03b9e69a931838ab6d969c9274f57`
Implementation baseline: user commit `0769082e` (initial inspection `ccb686e8`, user formatting preserved).
Timebox: 720 active minutes estimate, review every120 active minutes. fixture120秒、build/転送1200秒、VM180秒を基本に有限化。同条件無変更retryは3回まで。

| Order | Attempt | Phase | Status | Scope |
| --- | --- | --- | --- | --- |
| 1 | q311-i01 | [ws014-p008](https://github.com/awemorris/zedBSD/issues/395) | cleared | 承認回答全体: A1–A8、GPU fence所属、限定fixture/build/実QEMU受入と規約全文確認 |

## 依存と実行境界

p007 cleared → q311-i01 / p008 → p004 planning/未queue → WS029 native i915。後続は自動実行しない。p008の全実装項目と完了条件を本Queueの範囲にする。kernへのGPU抽象追加ではなくdriver所属へ整理し、成功/失敗のproducer保証を検証する。BLOB表示・標準OPAQUE_FDと承認済みzwl/libwayland制約を維持する。

[Guardrail](https://github.com/awemorris/zedBSD/issues/363)とcoding-style全文、make -j16対象buildを適用。新HALは個別承認、.internal/・aggregate make check・git add/commit/pushは対象外。private host/転送・隔離renderer buildを使用し、system package/GDM/VFIO/rebootは変更しない。

q310の実行scope/結果はlocal plan/history/queue-q310.mdと[Queue既存公開履歴](https://github.com/awemorris/zedBSD/issues/362)に保持。GitHubはIssue/Project同期でありrepository公開はユーザー担当。

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
