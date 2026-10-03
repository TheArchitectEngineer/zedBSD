# 停止契約の適用前レビュー

これは適用前のレビュー記録である。最終差分、既存承認との対応、8通りの限定試験を[signoff](review-signoff.json)で確認後、同じexecツールでローカル適用を再試行し、2026-09-13 12:18:17Zに成功した。[適用結果](application.json)は3ファイルのhashと静的コンパイルPASSを保持する。host buildとVM受入は、このローカル適用からは独立した結果として記録する。

## 既存の実行承認との対応

今回のユーザー指示は「では、実装をお願いします。独立したphaseで、レビューコメント対応とフレームワークでの共通化ですね。」である。同期済み[p009](https://github.com/awemorris/zedBSD/issues/396)はR3としてcontext停止、実DMA退役、停止確認不能時のquarantineを含み、「必要なisolated renderer/QEMU契約を実装・検証する」と定義している。この提案は、raw streamの未追跡native仕事にもその契約を満たすための具体差分であり、新しい実機運用・配備の目標を追加しない。

自動承認レビューが拒否したcallは、ローカルguest source二つの変更とstatic syntax checkだけだった。SSH、host systemへのdriverロード、物理GPU reset、GDM/VFIO、HAL、git commit/pushは含まれていなかった。[拒否全文と実callの範囲](approval-context.md)を保持する。拒否後に同じ変更を別経路でproductionへ適用していない。再検討に先立ち、具体差分・適用ファイル・停止意味・試験範囲を確認する。

## 独立レビューで具体化した条件

| 条件 | 確認・修正方針 |
| --- | --- |
| 全native仕事 | managed jobのcallback0だけでは足りない。context内の全VkDeviceのDeviceWaitIdle SUCCESSを確認し、raw未追跡仕事も含める |
| native失敗 | DEVICE_LOST、OOM、その他失敗を停止成功ACKへ読み替えない。確認不能は共通停止期限へ委ねる |
| 最後のcallback | native idle後も、各queueのsync mutex下でcallbackを含む最後の借用が終わるまで待つ。queue workerはlist削除後のretire callbackまで同mutexを保持している |
| 後着marker | 停止開始後の非zero marker追加を拒否し、停止開始とmarker追加を同じmutexで直列化する。空を観測した後に新しいcallbackを生まない |
| CPU0 | 停止後のCPU0 watermarkを実停止まで保留する。数値の最大値ではなく受信順を使い、wrapと後着fenceを扱う。proxy切断時にforce-retireしない |
| 未対応ring | concurrent ring decoderを停止した証拠がない場合はACKを出さない。unsupportedを成功にしない |
| host寿命 | quiesce workerをjoinしてからcontext/native object/callback storageを破棄する。今回のisolated buildはprocess workerで固定する |
| guest寿命 | 専用control slotは非待機で投稿し、ACKと既投稿descriptor/callback退役を別々に確認する。論理ERRORで未知DMA資源を再利用しない |
| metadata-only close | native commandを一度も送っていないという保守的な記録を持つ場合だけ、空contextとして停止可能。native投稿前にdirtyを記録し、失敗・不明でも維持する |
| 旧pair | exact3の認識と、新libvulkanで利用可能なexact7を分ける。情報照会だけのcloseで別sessionを故障にしない。通常利用を開始してから能力不足を発見する構成にしない |

## 有限な検証と影響範囲

適用候補を一時コピーへ置き、実production関数を使うlocal fixtureのnormal/ASan/UBSan、target freestanding syntax、差分とhashを確認する。適用対象は明示されたguest sourceだけ。host成果物はprivate server上の新しいisolated prefixへ構築し、既存q311-strict/q312-delayとsystem packageを上書きしない。QEMUは新しい使い捨てimage・変数領域・capture directoryを使い、実行期限と終了確認を持つ。

ローカルのsource変更だけでhostのGPUドライバがロード・変更されることはない。統合VM試験は既存のユーザー承認済みprivate hostのi915/ANVを使うため、driver同期の欠陥をfixtureとstatic reviewで先に検出し、停止不能時には資源隔離を保つ。実試験とその結果は別途記録し、ここで成功を先取りしない。
